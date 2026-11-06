#pragma once

#include <windows.h>

#include <functional>
#include <string>

struct mpv_handle;

// Owns the libmpv instance. All libmpv calls in the app go through this class,
// so new playback features (seek, volume, subtitles, ...) are added here.
class MpvPlayer {
public:
    struct Callbacks {
        // Called on an mpv thread when events are waiting. It must only signal
        // the UI thread, which then calls ProcessEvents().
        std::function<void()> wakeup;
        // Called on the thread running ProcessEvents().
        std::function<void(const std::wstring& message)> playbackError;
    };

    // Renders into videoWindow. Throws std::runtime_error if mpv can't start.
    MpvPlayer(HWND videoWindow, Callbacks callbacks);
    ~MpvPlayer();

    MpvPlayer(const MpvPlayer&) = delete;
    MpvPlayer& operator=(const MpvPlayer&) = delete;

    void Open(const std::wstring& path);
    void ProcessEvents();

private:
    mpv_handle* mpv_ = nullptr;
    Callbacks callbacks_;
};
