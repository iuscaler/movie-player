#pragma once

#include <windows.h>

#include <shellapi.h>

#include <filesystem>
#include <functional>
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
    // The same for a file given from outside, by Explorer or the command line. There a
    // subtitle means "play this", so it opens its video, not just adds to what's playing.
    void OpenFromExplorer(const std::wstring& path);

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    // Returns whether the key was a shortcut.
    bool HandleKeyDown(WPARAM key, bool isRepeat);
    void RunShortcut(ShortcutAction action);
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
    void HandleStateChanged(const PlaybackState& state);

    // Runs a blocking window over the video, keeping the bar and cursor out of its way.
    void RunDialog(const std::function<void()>& show);
    void OpenSettings();
    void ShowChapterList();
    void SeekWithPreview(double target, bool exact);
    void ApplyPreferences(const Preferences& preferences);
    // preferredSubtitle, if given, is selected over any other subtitle once the video is ready.
    void OpenVideo(const std::wstring& path, const std::wstring& preferredSubtitle = {});
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
    void SetLoopStart();
    void SetLoopEnd();
    void ClearLoop();
    void StepFrame(bool forward);
    void SetAspect(AspectMode aspect);
    void SetRotation(int degrees);
    void AdjustVideo(VideoAdjustment adjustment, int delta);
    void ResetVideo();
    void AdjustAudioDelay(double seconds);
    void ResetAudioDelay();
    void SetVolumeFromPointer(int clientX);
    void ApplyDefaultWindowSize();
    void HandleVideoReady(int width, int height);
    void HandlePlaybackError(const std::wstring& reason, bool unrecognizedFormat);
    // Sizes the window to show the video at this scale, as far as the monitor allows.
    void FitWindowToVideo(double requestedScale, bool center);
    void SetWindowSizePercent(int percent);
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
    Preferences preferences_;

    std::filesystem::path subtitleTempDirectory_;

    std::wstring currentVideoPath_;
    std::wstring pendingSubtitleName_;  // The file name of the subtitle to select once the video is ready.

    std::wstring dropRetryPath_;  // The dropped video being opened. Empty once it's settled.
    size_t dropRetryCount_ = 0;

    bool restoringPosition_ = false;
    bool startMaximized_ = false;
    bool centerOnFirstFit_ = false;
    int videoWidth_ = 0;  // The open video's own size. 0 if it has none, or isn't ready yet.
    int videoHeight_ = 0;

    bool fullscreen_ = false;
    WINDOWPLACEMENT restorePlacement_{sizeof(WINDOWPLACEMENT)};

    bool cursorHidden_ = false;
    bool trackingMouseLeave_ = false;
    POINT lastCursorPosition_{};
    ControlBar::Part hoveredPart_ = ControlBar::Part::None;
    int wheelRemainder_ = 0;

    bool menuOpen_ = false;
    bool dialogOpen_ = false;
    bool swallowingSystemKeys_ = false;  // An Alt shortcut ran, so releasing Alt mustn't open the system menu.
    bool announceSubtitleDelay_ = false;
    bool announceLoop_ = false;
    bool restartLoopPending_ = false;  // Playback is at the loop's new end, so it should go back to the start.

    enum class Drag { None, Seek, Volume, Window };
    Drag drag_ = Drag::None;
    POINT dragStart_{};
    int lastDragX_ = 0;
    std::optional<double> seekPreview_;
    ULONGLONG seekPreviewExpiry_ = 0;

    std::wstring title_;
};
