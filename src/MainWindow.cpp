#include "MainWindow.h"

#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

#include "TimeFormat.h"

namespace {

constexpr wchar_t kMainWindowClass[] = L"MoviePlayer.MainWindow";
constexpr wchar_t kVideoWindowClass[] = L"MoviePlayer.VideoWindow";
constexpr wchar_t kAppTitle[] = L"Movie Player";
constexpr UINT WM_MPV_WAKEUP = WM_APP + 1;

constexpr int kSeekSeconds = 5;
constexpr int kVolumeStep = 5;
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
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = name;
    if (!RegisterClassExW(&windowClass)) {
        throw std::runtime_error("RegisterClassEx failed");
    }
}

POINT PointFromLParam(LPARAM lParam) {
    return {static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
}

}  // namespace

MainWindow::MainWindow(HINSTANCE instance) {
    RegisterWindowClass(instance, kMainWindowClass, &MainWindow::WindowProc, CS_DBLCLKS);
    RegisterWindowClass(instance, kVideoWindowClass, &DefWindowProcW, 0);

    window_ = CreateWindowExW(
        WS_EX_ACCEPTFILES, kMainWindowClass, kAppTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
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
    // mpv's window lives inside this one, on mpv's own thread. Windows skips a
    // disabled child when deciding who gets mouse input, so disabling it sends
    // clicks, the wheel and dropped files to the main window instead of to mpv.
    EnableWindow(videoWindow_, FALSE);

    controlBar_ = std::make_unique<ControlBar>(window_);
    LayoutChildren();

    MpvPlayer::Callbacks callbacks;
    callbacks.wakeup = [window = window_] { PostMessageW(window, WM_MPV_WAKEUP, 0, 0); };
    callbacks.playbackError = [window = window_](const std::wstring& reason) {
        const std::wstring text = L"동영상을 재생할 수 없습니다.\n\n" + reason;
        MessageBoxW(window, text.c_str(), kAppTitle, MB_OK | MB_ICONERROR);
    };
    callbacks.stateChanged = [this](const PlaybackState& state) { HandleStateChanged(state); };
    player_ = std::make_unique<MpvPlayer>(videoWindow_, std::move(callbacks));
}

void MainWindow::Show(int showCommand) {
    ShowWindow(window_, showCommand);
    UpdateWindow(window_);
}

void MainWindow::Open(const std::wstring& path) {
    player_->Open(path);
    ShowControls();
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
    case WM_DPICHANGED: {
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
    case WM_DROPFILES:
        HandleDroppedFiles(reinterpret_cast<HDROP>(wParam));
        return 0;
    case WM_CLOSE:
        // mpv renders into videoWindow_, so stop it before the window goes away.
        player_.reset();
        controlBar_.reset();
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
}

void MainWindow::ShowControls() {
    controlBar_->SetVisible(true);
    SetTimer(window_, kIdleTimer, kIdleDelayMs, nullptr);
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
    default:
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
    if (!draggingSeek_ && !playback_.paused) {
        KillTimer(window_, kIdleTimer);
        controlBar_->SetVisible(false);
    }
    // The next move into the window must count as movement, even at the same spot.
    lastCursorPosition_ = {-1, -1};
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
    if (draggingSeek_ || hoveredPart_ != ControlBar::Part::None) {
        return;  // Leaving the bar or finishing the drag starts the timer again.
    }
    if (!playback_.paused) {
        controlBar_->SetVisible(false);
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
    playback_ = state;
    UpdateTitle(state);

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
