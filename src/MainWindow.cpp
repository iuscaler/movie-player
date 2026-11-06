#include "MainWindow.h"

#include <imm.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include "ContextMenu.h"
#include "FileAssociation.h"
#include "FileDialogs.h"
#include "SmiSubtitles.h"
#include "TimeFormat.h"

namespace {

constexpr wchar_t kVideoWindowClass[] = L"MoviePlayer.VideoWindow";
constexpr wchar_t kAppTitle[] = L"Movie Player";
constexpr UINT WM_MPV_WAKEUP = WM_APP + 1;
constexpr int kAppIconId = 1;

constexpr int kSeekSeconds = 5;
constexpr int kVolumeStep = 5;
constexpr double kSubtitleDelayStep = 0.1;
constexpr UINT_PTR kIdleTimer = 1;
constexpr UINT kIdleDelayMs = 2000;

// After a seek is released, keep showing its target until mpv gets there, so the
// bar doesn't jump back to the old position. The expiry covers a seek that lands
// somewhere else, and the tolerance covers one that lands slightly off target.
constexpr ULONGLONG kSeekPreviewExpiryMs = 3000;
constexpr double kSeekPreviewToleranceSeconds = 2.0;

void RegisterWindowClass(HINSTANCE instance, const wchar_t* name, WNDPROC proc, UINT style) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = style;
    windowClass.lpfnWndProc = proc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(kAppIconId));
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = name;
    if (!RegisterClassExW(&windowClass)) {
        throw std::runtime_error("RegisterClassEx failed");
    }
}

std::filesystem::path SubtitleTempDirectory() {
    wchar_t temp[MAX_PATH + 1];
    const DWORD length = GetTempPathW(MAX_PATH + 1, temp);
    const std::filesystem::path root = length > 0 ? std::filesystem::path(temp) : std::filesystem::path();
    return root / L"MoviePlayer" / std::to_wstring(GetCurrentProcessId());
}

POINT PointFromLParam(LPARAM lParam) {
    return {static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
}

}  // namespace

MainWindow::MainWindow(HINSTANCE instance, const Settings& settings) : subtitleTempDirectory_(SubtitleTempDirectory()) {
    playback_.volume = settings.volume;
    playback_.subtitleEncoding = settings.subtitleEncoding;

    RegisterWindowClass(instance, kClassName, &MainWindow::WindowProc, CS_DBLCLKS);
    RegisterWindowClass(instance, kVideoWindowClass, &DefWindowProcW, 0);

    window_ = CreateWindowExW(
        WS_EX_ACCEPTFILES, kClassName, kAppTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720,
        nullptr, nullptr, instance, this);
    if (!window_) {
        throw std::runtime_error("CreateWindowEx failed for the main window");
    }

    RECT client{};
    GetClientRect(window_, &client);
    videoWindow_ = CreateWindowExW(
        0, kVideoWindowClass, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, client.right, client.bottom,
        window_, nullptr, instance, nullptr);
    if (!videoWindow_) {
        throw std::runtime_error("CreateWindowEx failed for the video window");
    }
    // With a Korean input mode on, letter keys would arrive as VK_PROCESSKEY
    // instead of as themselves, and shortcuts like S and A would stop working.
    ImmAssociateContextEx(window_, nullptr, 0);

    // mpv's window lives inside this one, on mpv's own thread. Windows skips a
    // disabled child when deciding who gets mouse input, so disabling it sends
    // clicks, the wheel and dropped files to the main window instead of to mpv.
    EnableWindow(videoWindow_, FALSE);

    controlBar_ = std::make_unique<ControlBar>(window_);
    RestoreWindowPosition(settings);
    LayoutChildren();

    MpvPlayer::Callbacks callbacks;
    callbacks.wakeup = [window = window_] { PostMessageW(window, WM_MPV_WAKEUP, 0, 0); };
    callbacks.playbackError = [window = window_](const std::wstring& reason) {
        const std::wstring text = L"동영상을 재생할 수 없습니다.\n\n" + reason;
        MessageBoxW(window, text.c_str(), kAppTitle, MB_OK | MB_ICONERROR);
    };
    callbacks.subtitleLoadFailed = [this] { player_->ShowMessage(L"자막 파일을 열 수 없습니다"); };
    callbacks.stateChanged = [this](const PlaybackState& state) { HandleStateChanged(state); };
    player_ = std::make_unique<MpvPlayer>(videoWindow_, std::move(callbacks), settings.volume, settings.subtitleEncoding);
}

void MainWindow::Show(int showCommand) {
    const bool minimized = showCommand == SW_SHOWMINIMIZED || showCommand == SW_MINIMIZE || showCommand == SW_SHOWMINNOACTIVE;
    ShowWindow(window_, startMaximized_ && !minimized ? SW_SHOWMAXIMIZED : showCommand);
    UpdateWindow(window_);
}

void MainWindow::Open(const std::wstring& path) {
    if (IsSubtitleFile(path)) {
        AddSubtitle(path);
    } else {
        OpenVideo(path);
    }
    ShowControls();
}

void MainWindow::OpenVideo(const std::wstring& path) {
    // Language parts split out for the previous video aren't needed any more.
    std::error_code error;
    std::filesystem::remove_all(subtitleTempDirectory_, error);

    std::vector<std::wstring> subtitles;
    for (const std::filesystem::path& file : PrepareSmiFilesFor(path, subtitleTempDirectory_)) {
        subtitles.push_back(file.wstring());
    }
    player_->Open(path, subtitles);
}

void MainWindow::AddSubtitle(const std::wstring& path) {
    std::vector<std::filesystem::path> files{path};
    if (IsSmiFile(path)) {
        files = PrepareSmiFile(path, subtitleTempDirectory_);
    }
    // The preferred language comes first: add the others quietly, and switch to it last.
    for (size_t i = files.size(); i-- > 1;) {
        player_->AddSubtitleFile(files[i].wstring(), false);
    }
    player_->AddSubtitleFile(files.front().wstring(), true);
}

void MainWindow::RestoreWindowPosition(const Settings& settings) {
    if (!settings.windowPosition) {
        return;
    }
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    placement.showCmd = SW_HIDE;
    placement.rcNormalPosition = *settings.windowPosition;
    // Windows moves a window back on screen if its saved monitor is gone. It also
    // sends WM_DPICHANGED if the monitor differs, but the saved size is already right.
    restoringPosition_ = true;
    SetWindowPlacement(window_, &placement);
    restoringPosition_ = false;
    startMaximized_ = settings.windowMaximized;
}

void MainWindow::SaveCurrentSettings() {
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    if (fullscreen_) {
        placement = restorePlacement_;
    } else if (!GetWindowPlacement(window_, &placement)) {
        return;
    }
    Settings settings;
    settings.volume = playback_.volume;
    settings.subtitleEncoding = playback_.subtitleEncoding;
    settings.windowPosition = placement.rcNormalPosition;
    settings.windowMaximized = placement.showCmd == SW_SHOWMAXIMIZED ||
                               (placement.showCmd == SW_SHOWMINIMIZED && (placement.flags & WPF_RESTORETOMAXIMIZED) != 0);
    SaveSettings(settings);
}

void MainWindow::HandleCopyData(const COPYDATASTRUCT& data) {
    if (data.dwData != kOpenPathMessageId) {
        return;
    }
    if (IsIconic(window_)) {
        ShowWindow(window_, SW_RESTORE);
    }
    SetForegroundWindow(window_);
    const size_t length = data.cbData / sizeof(wchar_t);
    if (data.lpData && length > 0) {
        Open(std::wstring(static_cast<const wchar_t*>(data.lpData), length));
    }
}

void MainWindow::HandleRegisterFileAssociation() {
    if (!RegisterFileAssociation()) {
        MessageBoxW(window_, L"연결 프로그램에 등록하지 못했습니다.", kAppTitle, MB_OK | MB_ICONERROR);
        return;
    }
    const int answer = MessageBoxW(
        window_,
        L"\"연결 프로그램\" 목록에 등록했습니다.\n\n"
        L"영상을 더블클릭해서 바로 열려면 Windows 기본 앱 설정에서 Movie Player를 직접 지정해야 합니다.\n\n"
        L"지금 기본 앱 설정을 열까요?",
        kAppTitle, MB_YESNO | MB_ICONINFORMATION);
    if (answer == IDYES) {
        OpenDefaultAppsSettings();
    }
}

LRESULT CALLBACK MainWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!self) {
        return DefWindowProcW(window, message, wParam, lParam);
    }
    self->window_ = window;
    return self->HandleMessage(message, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SIZE:
        LayoutChildren();
        return 0;
    case WM_COPYDATA:
        HandleCopyData(*reinterpret_cast<const COPYDATASTRUCT*>(lParam));
        return TRUE;
    case WM_DPICHANGED: {
        if (restoringPosition_) {
            LayoutChildren();
            return 0;
        }
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(window_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutChildren();
        return 0;
    }
    case WM_MPV_WAKEUP:
        if (player_) {
            player_->ProcessEvents();
        }
        return 0;
    case WM_KEYDOWN:
        HandleKeyDown(wParam, (lParam & (1 << 30)) != 0);
        return 0;
    case WM_LBUTTONDOWN:
        HandleLeftButtonDown(PointFromLParam(lParam));
        return 0;
    case WM_LBUTTONDBLCLK:
        // On the controls, the second click of a double-click is just another click.
        if (controlBar_->HitTest(PointFromLParam(lParam)) == ControlBar::Part::None) {
            ToggleFullscreen();
        } else {
            HandleLeftButtonDown(PointFromLParam(lParam));
        }
        return 0;
    case WM_LBUTTONUP:
        HandleLeftButtonUp(PointFromLParam(lParam));
        return 0;
    case WM_CAPTURECHANGED:
        HandleCaptureLost();
        return 0;
    case WM_MOUSEWHEEL:
        HandleMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;
    case WM_MOUSEMOVE:
        HandleMouseMove(PointFromLParam(lParam));
        return 0;
    case WM_MOUSELEAVE:
        HandleMouseLeave();
        return 0;
    case WM_TIMER:
        if (wParam == kIdleTimer) {
            KillTimer(window_, kIdleTimer);
            HandleIdleTimeout();
        }
        return 0;
    case WM_SETCURSOR:
        if (cursorHidden_ && LOWORD(lParam) == HTCLIENT) {
            SetCursor(nullptr);
            return TRUE;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    case WM_CONTEXTMENU:
        HandleContextMenu(PointFromLParam(lParam));
        return 0;
    case WM_DROPFILES:
        HandleDroppedFiles(reinterpret_cast<HDROP>(wParam));
        return 0;
    case WM_CLOSE:
        SaveCurrentSettings();
        // mpv renders into videoWindow_, so stop it before the window goes away.
        player_.reset();
        controlBar_.reset();
        {
            std::error_code error;
            std::filesystem::remove_all(subtitleTempDirectory_, error);
        }
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window_, message, wParam, lParam);
    }
}

void MainWindow::LayoutChildren() {
    RECT client{};
    GetClientRect(window_, &client);
    if (videoWindow_) {
        MoveWindow(videoWindow_, 0, 0, client.right, client.bottom, TRUE);
    }
    if (controlBar_) {
        controlBar_->Resize(client.right, client.bottom, GetDpiForWindow(window_));
    }
    UpdateSubtitlePosition();
}

void MainWindow::ShowControls() {
    SetControlsVisible(true);
    SetTimer(window_, kIdleTimer, kIdleDelayMs, nullptr);
}

void MainWindow::SetControlsVisible(bool visible) {
    controlsVisible_ = visible;
    controlBar_->SetVisible(visible);
    UpdateSubtitlePosition();
}

void MainWindow::UpdateSubtitlePosition() {
    if (!player_) {
        return;
    }
    // While the controls show, lift subtitles so the bar doesn't cover them.
    int position = 100;
    RECT client{};
    GetClientRect(window_, &client);
    if (controlsVisible_ && client.bottom > 0) {
        const int covered = 100 * controlBar_->ControlsHeight() / client.bottom;
        position = std::max(100 - covered, 50);
    }
    if (position != subtitlePosition_) {
        subtitlePosition_ = position;
        player_->SetSubtitlePosition(position);
    }
}

void MainWindow::HandleKeyDown(WPARAM key, bool isRepeat) {
    switch (key) {
    case VK_SPACE:
        if (!isRepeat) {
            player_->TogglePause();
            ShowControls();
        }
        break;
    case VK_LEFT:
        player_->SeekBy(-kSeekSeconds);
        ShowControls();
        break;
    case VK_RIGHT:
        player_->SeekBy(kSeekSeconds);
        ShowControls();
        break;
    case VK_UP:
        player_->ChangeVolume(kVolumeStep);
        ShowControls();
        break;
    case VK_DOWN:
        player_->ChangeVolume(-kVolumeStep);
        ShowControls();
        break;
    case VK_RETURN:
        if (!isRepeat) {
            ToggleFullscreen();
        }
        break;
    case VK_ESCAPE:
        SetFullscreen(false);
        break;
    case 'S':
        if (!isRepeat) {
            CycleSubtitle();
        }
        break;
    case 'A':
        if (!isRepeat) {
            CycleAudio();
        }
        break;
    case VK_OEM_COMMA:
        AdjustSubtitleDelay(-kSubtitleDelayStep);
        break;
    case VK_OEM_PERIOD:
        AdjustSubtitleDelay(kSubtitleDelayStep);
        break;
    case VK_OEM_2:  // The '/' key.
        if (!isRepeat) {
            announceSubtitleDelay_ = true;
            player_->ResetSubtitleDelay();
        }
        break;
    default:
        break;
    }
}

void MainWindow::CycleSubtitle() {
    // Goes through the tracks in order, with "off" between the last and the first.
    const MediaTrack* first = nullptr;
    const MediaTrack* next = nullptr;
    bool passedSelected = false;
    for (const MediaTrack& track : playback_.tracks) {
        if (track.type != TrackType::Subtitle) {
            continue;
        }
        if (!first) {
            first = &track;
        }
        if (passedSelected && !next) {
            next = &track;
        }
        passedSelected = passedSelected || track.selected;
    }
    if (!first) {
        player_->ShowMessage(L"자막 없음");
    } else if (next || !passedSelected) {
        const MediaTrack& target = next ? *next : *first;
        player_->SelectSubtitle(target.id);
        player_->ShowMessage(L"자막: " + TrackLabel(target));
    } else {
        player_->SelectSubtitle(std::nullopt);
        player_->ShowMessage(L"자막 끔");
    }
}

void MainWindow::CycleAudio() {
    const MediaTrack* first = nullptr;
    const MediaTrack* next = nullptr;
    bool passedSelected = false;
    for (const MediaTrack& track : playback_.tracks) {
        if (track.type != TrackType::Audio) {
            continue;
        }
        if (!first) {
            first = &track;
        }
        if (passedSelected && !next) {
            next = &track;
        }
        passedSelected = passedSelected || track.selected;
    }
    if (const MediaTrack* target = next ? next : first) {
        player_->SelectAudio(target->id);
        player_->ShowMessage(L"오디오: " + TrackLabel(*target));
    } else {
        player_->ShowMessage(L"오디오 없음");
    }
}

void MainWindow::AdjustSubtitleDelay(double seconds) {
    announceSubtitleDelay_ = true;
    player_->ChangeSubtitleDelay(seconds);
}

void MainWindow::HandleContextMenu(POINT screenPoint) {
    if (screenPoint.x == -1 && screenPoint.y == -1) {
        GetCursorPos(&screenPoint);  // Opened from the keyboard.
    }
    menuOpen_ = true;
    SetCursorHidden(false);
    const MenuChoice choice = ShowContextMenu(window_, screenPoint, playback_, IsFileAssociationRegistered());
    menuOpen_ = false;
    ShowControls();

    switch (choice.action) {
    case MenuChoice::Action::OpenVideo: {
        const std::wstring path = PickVideoFile(window_);
        if (!path.empty()) {
            Open(path);
        }
        break;
    }
    case MenuChoice::Action::OpenSubtitle: {
        const std::wstring path = PickSubtitleFile(window_);
        if (!path.empty()) {
            AddSubtitle(path);
        }
        break;
    }
    case MenuChoice::Action::SelectSubtitle:
        player_->SelectSubtitle(choice.trackId);
        break;
    case MenuChoice::Action::SelectAudio:
        if (choice.trackId) {
            player_->SelectAudio(*choice.trackId);
        }
        break;
    case MenuChoice::Action::ResetSubtitleDelay:
        announceSubtitleDelay_ = true;
        player_->ResetSubtitleDelay();
        break;
    case MenuChoice::Action::SetSubtitleEncoding:
        player_->SetSubtitleEncoding(choice.encoding);
        break;
    case MenuChoice::Action::RegisterFileAssociation:
        HandleRegisterFileAssociation();
        break;
    case MenuChoice::Action::UnregisterFileAssociation:
        UnregisterFileAssociation();
        player_->ShowMessage(L"연결 프로그램 등록을 해제했습니다");
        break;
    case MenuChoice::Action::None:
        break;
    }
}

void MainWindow::HandleMouseWheel(int delta) {
    // Precision wheels report small deltas, so only act on whole notches.
    wheelRemainder_ += delta;
    const int notches = wheelRemainder_ / WHEEL_DELTA;
    wheelRemainder_ %= WHEEL_DELTA;
    if (notches != 0) {
        player_->ChangeVolume(notches * kVolumeStep);
        ShowControls();
    }
}

void MainWindow::HandleMouseMove(POINT point) {
    if (draggingSeek_) {
        lastDragX_ = point.x;
        SeekToPointer(point.x, false);
        return;
    }

    // Windows also sends this when nothing moved, for example after a window
    // changes under the cursor. Those must not reveal a hidden cursor or the bar.
    POINT position{};
    GetCursorPos(&position);
    if (position.x == lastCursorPosition_.x && position.y == lastCursorPosition_.y) {
        return;
    }
    lastCursorPosition_ = position;

    if (!trackingMouseLeave_) {
        TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window_, 0};
        trackingMouseLeave_ = TrackMouseEvent(&track) != FALSE;
    }
    SetCursorHidden(false);
    ShowControls();
    hoveredPart_ = controlBar_->HitTest(point);
    controlBar_->SetHoveredPart(hoveredPart_);
}

void MainWindow::HandleMouseLeave() {
    trackingMouseLeave_ = false;
    hoveredPart_ = ControlBar::Part::None;
    controlBar_->SetHoveredPart(hoveredPart_);
    if (!draggingSeek_ && !menuOpen_ && !playback_.paused) {
        KillTimer(window_, kIdleTimer);
        SetControlsVisible(false);
    }
    // A real return to the window is always from elsewhere, so this keeps spurious
    // move messages from counting as movement while the pointer is outside.
    GetCursorPos(&lastCursorPosition_);
}

void MainWindow::HandleLeftButtonDown(POINT point) {
    switch (controlBar_->HitTest(point)) {
    case ControlBar::Part::PlayPause:
        player_->TogglePause();
        ShowControls();
        break;
    case ControlBar::Part::SeekTrack:
        draggingSeek_ = true;
        lastDragX_ = point.x;
        SetCapture(window_);
        SeekToPointer(point.x, false);
        break;
    default:
        break;
    }
}

void MainWindow::HandleLeftButtonUp(POINT point) {
    if (!draggingSeek_) {
        return;
    }
    draggingSeek_ = false;  // First, so losing the capture below isn't treated as a cancelled drag.
    SeekToPointer(point.x, true);
    ReleaseCapture();
    ShowControls();
}

void MainWindow::HandleCaptureLost() {
    if (draggingSeek_) {
        draggingSeek_ = false;
        SeekToPointer(lastDragX_, true);
        ShowControls();
    }
}

void MainWindow::SeekToPointer(int clientX, bool isFinal) {
    if (!playback_.duration) {
        return;
    }
    const double target = controlBar_->SeekFractionAt(clientX) * *playback_.duration;
    // Keyframe seeks keep dragging responsive on heavy video; the final one lands exactly.
    player_->SeekTo(target, isFinal);
    seekPreview_ = target;
    seekPreviewExpiry_ = GetTickCount64() + kSeekPreviewExpiryMs;
    controlBar_->SetSeekPreview(seekPreview_);
}

void MainWindow::HandleIdleTimeout() {
    if (draggingSeek_ || menuOpen_ || hoveredPart_ != ControlBar::Part::None) {
        return;  // Leaving the bar, finishing the drag or closing the menu starts the timer again.
    }
    if (!playback_.paused) {
        SetControlsVisible(false);
    }
    if (fullscreen_) {
        SetCursorHidden(true);
    }
}

void MainWindow::HandleDroppedFiles(HDROP drop) {
    std::wstring path;
    const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
    if (length > 0) {
        std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1);
        DragQueryFileW(drop, 0, buffer.data(), length + 1);
        path.assign(buffer.data(), length);
    }
    DragFinish(drop);

    if (!path.empty()) {
        Open(path);
        SetForegroundWindow(window_);
    }
}

void MainWindow::HandleStateChanged(const PlaybackState& state) {
    const bool delayChanged = state.subtitleDelay != playback_.subtitleDelay;
    playback_ = state;
    UpdateTitle(state);

    if (delayChanged && announceSubtitleDelay_) {
        announceSubtitleDelay_ = false;
        wchar_t message[64];
        swprintf_s(message, L"자막 싱크 %+.1f초", state.subtitleDelay);
        player_->ShowMessage(message);
    }

    if (seekPreview_ && !draggingSeek_) {
        const bool arrived = state.position && std::abs(*state.position - *seekPreview_) < kSeekPreviewToleranceSeconds;
        if (arrived || GetTickCount64() >= seekPreviewExpiry_) {
            seekPreview_.reset();
            controlBar_->SetSeekPreview(std::nullopt);
        }
    }
    controlBar_->SetPlayback(state);
}

void MainWindow::ToggleFullscreen() {
    SetFullscreen(!fullscreen_);
}

void MainWindow::SetFullscreen(bool fullscreen) {
    if (fullscreen == fullscreen_) {
        return;
    }

    const LONG_PTR style = GetWindowLongPtrW(window_, GWL_STYLE);
    if (fullscreen) {
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if (!GetWindowPlacement(window_, &restorePlacement_) ||
            !GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor)) {
            return;
        }
        SetWindowLongPtrW(window_, GWL_STYLE, style & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW));
        const RECT& area = monitor.rcMonitor;
        SetWindowPos(window_, HWND_TOP, area.left, area.top, area.right - area.left, area.bottom - area.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetCursorHidden(false);
        SetWindowLongPtrW(window_, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(window_, &restorePlacement_);
        SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    fullscreen_ = fullscreen;
    ShowControls();
}

void MainWindow::SetCursorHidden(bool hidden) {
    if (hidden == cursorHidden_) {
        return;
    }
    cursorHidden_ = hidden;
    SetCursor(hidden ? nullptr : LoadCursorW(nullptr, IDC_ARROW));
}

void MainWindow::UpdateTitle(const PlaybackState& state) {
    std::wstring title = kAppTitle;
    if (!state.fileName.empty()) {
        // The file name goes last: long names get cut off in the title bar.
        title = FormatTime(state.position.value_or(0.0));
        if (state.duration) {
            title += L" / " + FormatTime(*state.duration);
        }
        title += L" | 볼륨 " + std::to_wstring(std::lround(state.volume)) + L"%";
        if (state.paused) {
            title += L" | 일시정지";
        }
        title += L" | " + state.fileName;
    }
    if (title != title_) {
        title_ = std::move(title);
        SetWindowTextW(window_, title_.c_str());
    }
}
