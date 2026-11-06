#pragma once

#include <windows.h>

#include <shellapi.h>

#include <memory>
#include <string>

#include "MpvPlayer.h"

// The top-level window. It takes all keyboard and mouse input and turns it into
// player commands. mpv draws into a dedicated child window, which leaves room to
// add a controls overlay above the video later.
class MainWindow {
public:
    // Throws std::runtime_error if the window or the player can't be created.
    explicit MainWindow(HINSTANCE instance);

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    void Show(int showCommand);
    void Open(const std::wstring& path);

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void HandleKeyDown(WPARAM key, bool isRepeat);
    void HandleMouseWheel(int delta);
    void HandleMouseMove();
    void HandleDroppedFiles(HDROP drop);

    void ToggleFullscreen();
    void SetFullscreen(bool fullscreen);
    void SetCursorHidden(bool hidden);
    void UpdateTitle(const PlaybackState& state);

    HWND window_ = nullptr;
    HWND videoWindow_ = nullptr;
    std::unique_ptr<MpvPlayer> player_;

    bool fullscreen_ = false;
    WINDOWPLACEMENT restorePlacement_{sizeof(WINDOWPLACEMENT)};

    bool cursorHidden_ = false;
    POINT lastCursorPosition_{};
    int wheelRemainder_ = 0;
    std::wstring title_;
};
