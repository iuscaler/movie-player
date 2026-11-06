#pragma once

#include <windows.h>

#include <memory>
#include <string>

#include "MpvPlayer.h"

// The top-level window. mpv draws into a dedicated child window, which leaves
// room to add a controls overlay above the video later.
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

    HWND window_ = nullptr;
    HWND videoWindow_ = nullptr;
    std::unique_ptr<MpvPlayer> player_;
};
