#pragma once

#include <windows.h>

#include <shellapi.h>

#include <memory>
#include <optional>
#include <string>

#include "ControlBar.h"
#include "MpvPlayer.h"

// The top-level window. It takes all keyboard and mouse input and turns it into
// player commands, and decides when the control bar is shown. mpv draws into a
// dedicated child window; the control bar is composited above it.
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
    void HandleMouseMove(POINT point);
    void HandleMouseLeave();
    void HandleLeftButtonDown(POINT point);
    void HandleLeftButtonUp(POINT point);
    void HandleCaptureLost();
    void HandleIdleTimeout();
    void HandleDroppedFiles(HDROP drop);
    void HandleStateChanged(const PlaybackState& state);

    void LayoutChildren();
    void ShowControls();
    void SeekToPointer(int clientX, bool isFinal);
    void ToggleFullscreen();
    void SetFullscreen(bool fullscreen);
    void SetCursorHidden(bool hidden);
    void UpdateTitle(const PlaybackState& state);

    HWND window_ = nullptr;
    HWND videoWindow_ = nullptr;
    std::unique_ptr<ControlBar> controlBar_;
    std::unique_ptr<MpvPlayer> player_;
    PlaybackState playback_;

    bool fullscreen_ = false;
    WINDOWPLACEMENT restorePlacement_{sizeof(WINDOWPLACEMENT)};

    bool cursorHidden_ = false;
    bool trackingMouseLeave_ = false;
    POINT lastCursorPosition_{};
    ControlBar::Part hoveredPart_ = ControlBar::Part::None;
    int wheelRemainder_ = 0;

    bool draggingSeek_ = false;
    int lastDragX_ = 0;
    std::optional<double> seekPreview_;
    ULONGLONG seekPreviewExpiry_ = 0;

    std::wstring title_;
};
