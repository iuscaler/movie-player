#pragma once

#include <windows.h>

#include <functional>
#include <initializer_list>
#include <optional>
#include <string>

struct mpv_handle;
struct mpv_event_property;

// What the UI needs to know about playback. Values mpv doesn't know yet (for
// example while a file is loading) are empty.
struct PlaybackState {
    std::wstring fileName;
    std::optional<double> position;
    std::optional<double> duration;
    double volume = 100.0;
    bool paused = false;
    bool atEnd = false;
};

// Owns the libmpv instance. All libmpv calls in the app go through this class,
// so new playback features (subtitles, tracks, ...) are added here.
//
// Control methods are asynchronous: they never block the UI thread and don't
// report errors, since a request such as seeking with no file open is harmless.
// Problems opening a file arrive through Callbacks::playbackError.
class MpvPlayer {
public:
    struct Callbacks {
        // Called on an mpv thread when events are waiting. It must only signal
        // the UI thread, which then calls ProcessEvents().
        std::function<void()> wakeup;
        // These two are called on the thread running ProcessEvents().
        std::function<void(const std::wstring& message)> playbackError;
        std::function<void(const PlaybackState& state)> stateChanged;
    };

    // Renders into videoWindow. Throws std::runtime_error if mpv can't start.
    MpvPlayer(HWND videoWindow, Callbacks callbacks);
    ~MpvPlayer();

    MpvPlayer(const MpvPlayer&) = delete;
    MpvPlayer& operator=(const MpvPlayer&) = delete;

    void Open(const std::wstring& path);
    void TogglePause();
    void SeekBy(int seconds);
    void ChangeVolume(int delta);

    void ProcessEvents();

private:
    void Command(std::initializer_list<const char*> arguments);
    void UpdateState(const mpv_event_property& property);

    mpv_handle* mpv_ = nullptr;
    Callbacks callbacks_;
    PlaybackState state_;
};
