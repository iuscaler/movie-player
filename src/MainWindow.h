#pragma once

#include <windows.h>

#include <shellapi.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "ControlBar.h"
#include "MpvPlayer.h"
#include "Settings.h"

// The top-level window. It takes all keyboard and mouse input and turns it into
// player commands, and decides when the control bar is shown. mpv draws into a
// dedicated child window; the control bar is composited above it.
class MainWindow {
public:
    // How a second copy of the app finds this one and hands it a file: a
    // WM_COPYDATA message with this id, holding a UTF-16 path that may be empty.
    static constexpr wchar_t kClassName[] = L"MoviePlayer.MainWindow";
    static constexpr ULONG_PTR kOpenPathMessageId = 0x4D505631;

    // Throws std::runtime_error if the window or the player can't be created.
    MainWindow(HINSTANCE instance, const Settings& settings);

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    void Show(int showCommand);
    // Opens a video, or adds a subtitle to the current one, depending on the file type.
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
    void HandleContextMenu(POINT screenPoint);
    void HandleCopyData(const COPYDATASTRUCT& data);
    void HandleRegisterFileAssociation();
    void HandleStateChanged(const PlaybackState& state);

    void OpenVideo(const std::wstring& path);
    void AddSubtitle(const std::wstring& path);
    void RestoreWindowPosition(const Settings& settings);
    void SaveCurrentSettings();
    void LayoutChildren();
    void ShowControls();
    void CycleSubtitle();
    void CycleAudio();
    void AdjustSubtitleDelay(double seconds);
    void ChangeSpeed(double delta);
    void SetSpeed(double speed);
    void ToggleLoop();
    void StepFrame(bool forward);
    void SetAspect(AspectMode aspect);
    void SetRotation(int degrees);
    void AdjustVideo(VideoAdjustment adjustment, int delta);
    void ResetVideo();
    void AdjustAudioDelay(double seconds);
    void ResetAudioDelay();
    void SetVolumeFromPointer(int clientX);
    void ApplyDefaultWindowSize();
    void FitWindowToVideo(int videoWidth, int videoHeight);
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

    std::filesystem::path subtitleTempDirectory_;

    bool restoringPosition_ = false;
    bool startMaximized_ = false;
    bool centerOnFirstFit_ = false;

    bool fullscreen_ = false;
    WINDOWPLACEMENT restorePlacement_{sizeof(WINDOWPLACEMENT)};

    bool cursorHidden_ = false;
    bool trackingMouseLeave_ = false;
    POINT lastCursorPosition_{};
    ControlBar::Part hoveredPart_ = ControlBar::Part::None;
    int wheelRemainder_ = 0;

    bool menuOpen_ = false;
    bool announceSubtitleDelay_ = false;
    bool announceLoop_ = false;

    enum class Drag { None, Seek, Volume };
    Drag drag_ = Drag::None;
    int lastDragX_ = 0;
    std::optional<double> seekPreview_;
    ULONGLONG seekPreviewExpiry_ = 0;

    std::wstring title_;
};
