#include "MainWindow.h"

#include <stdexcept>
#include <utility>

namespace {

constexpr wchar_t kMainWindowClass[] = L"MoviePlayer.MainWindow";
constexpr wchar_t kVideoWindowClass[] = L"MoviePlayer.VideoWindow";
constexpr UINT WM_MPV_WAKEUP = WM_APP + 1;

void RegisterWindowClass(HINSTANCE instance, const wchar_t* name, WNDPROC proc, HBRUSH background) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = proc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = background;
    windowClass.lpszClassName = name;
    if (!RegisterClassExW(&windowClass)) {
        throw std::runtime_error("RegisterClassEx failed");
    }
}

}  // namespace

MainWindow::MainWindow(HINSTANCE instance) {
    const auto black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterWindowClass(instance, kMainWindowClass, &MainWindow::WindowProc, black);
    RegisterWindowClass(instance, kVideoWindowClass, &DefWindowProcW, black);

    window_ = CreateWindowExW(
        0, kMainWindowClass, L"Movie Player", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
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

    MpvPlayer::Callbacks callbacks;
    callbacks.wakeup = [window = window_] { PostMessageW(window, WM_MPV_WAKEUP, 0, 0); };
    callbacks.playbackError = [window = window_](const std::wstring& reason) {
        const std::wstring text = L"동영상을 재생할 수 없습니다.\n\n" + reason;
        MessageBoxW(window, text.c_str(), L"Movie Player", MB_OK | MB_ICONERROR);
    };
    player_ = std::make_unique<MpvPlayer>(videoWindow_, std::move(callbacks));
}

void MainWindow::Show(int showCommand) {
    ShowWindow(window_, showCommand);
    UpdateWindow(window_);
}

void MainWindow::Open(const std::wstring& path) {
    player_->Open(path);
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
        if (videoWindow_) {
            MoveWindow(videoWindow_, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        }
        return 0;
    case WM_MPV_WAKEUP:
        if (player_) {
            player_->ProcessEvents();
        }
        return 0;
    case WM_CLOSE:
        // mpv renders into videoWindow_, so stop it before the window goes away.
        player_.reset();
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window_, message, wParam, lParam);
    }
}
