#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

struct mpv_handle;
struct mpv_event_property;
struct mpv_node;

enum class TrackType { Audio, Subtitle, Other };

struct MediaTrack {
    int64_t id = 0;
    TrackType type = TrackType::Other;
    std::wstring title;
    std::wstring language;
    std::wstring codec;
    std::wstring externalFileName;  // Empty for tracks inside the video file.
    int channels = 0;               // Audio only; 0 if unknown.
    bool selected = false;
};

// How to read subtitle files that aren't Unicode.
enum class SubtitleEncoding {
    Korean,  // Treat them as CP949.
    Auto,    // Let mpv guess. It handles Shift-JIS and others, but misreads CP949.
};

// What the UI needs to know about playback. Values mpv doesn't know yet (for
// example while a file is loading) are empty.
struct PlaybackState {
    std::wstring fileName;
    std::optional<double> position;
    std::optional<double> duration;
    double volume = 100.0;
    bool paused = false;
    bool atEnd = false;
    std::vector<MediaTrack> tracks;
    double subtitleDelay = 0.0;
    SubtitleEncoding subtitleEncoding = SubtitleEncoding::Korean;
};

// Owns the libmpv instance. All libmpv calls in the app go through this class,
// so new playback features are added here.
//
// Control methods are asynchronous: they never block the UI thread and don't
// report errors, since a request such as seeking with no file open is harmless.
// Problems opening a file arrive through the callbacks.
class MpvPlayer {
public:
    struct Callbacks {
        // Called on an mpv thread when events are waiting. It must only signal
        // the UI thread, which then calls ProcessEvents().
        std::function<void()> wakeup;
        // The rest are called on the thread running ProcessEvents().
        std::function<void(const std::wstring& message)> playbackError;
        std::function<void()> subtitleLoadFailed;
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
    // An exact seek lands on the requested time. Otherwise it lands on a nearby
    // keyframe, which is much faster on heavy video.
    void SeekTo(double seconds, bool exact);
    void ChangeVolume(int delta);

    void AddSubtitleFile(const std::wstring& path);
    // An empty id turns subtitles off.
    void SelectSubtitle(std::optional<int64_t> trackId);
    void SelectAudio(int64_t trackId);
    void ChangeSubtitleDelay(double seconds);
    void ResetSubtitleDelay();
    void SetSubtitleEncoding(SubtitleEncoding encoding);
    // Where subtitles sit, as a percentage of the window height. 100 is the bottom.
    void SetSubtitlePosition(int percent);

    // Shows a short message over the video.
    void ShowMessage(const std::wstring& text);

    void ProcessEvents();

private:
    void Command(std::initializer_list<const char*> arguments, uint64_t replyId = 0);
    void UpdateState(const mpv_event_property& property);
    void UpdateTracks(const mpv_node& list);

    mpv_handle* mpv_ = nullptr;
    Callbacks callbacks_;
    PlaybackState state_;
};
