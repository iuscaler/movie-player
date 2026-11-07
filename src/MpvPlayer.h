#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
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
    std::wstring codecDescription;
    std::wstring codecProfile;
    std::wstring externalFileName;  // Empty for tracks inside the video file.
    int channels = 0;               // Audio only; 0 if unknown.
    std::wstring channelLayout;     // Audio only, such as "5.1(side)".
    int sampleRate = 0;             // Audio only; 0 if unknown.
    int width = 0;                  // Video only; 0 if unknown.
    int height = 0;
    double fps = 0.0;
    int64_t bitrate = 0;            // 0 if unknown.
    bool selected = false;
    bool isDefault = false;
    bool forced = false;
};

struct Chapter {
    double time = 0.0;
    std::wstring title;
};

// How to read subtitle files that aren't Unicode.
enum class SubtitleEncoding {
    Korean,  // Treat them as CP949.
    Auto,    // Let mpv guess. It handles Shift-JIS and others, but misreads CP949.
};

enum class AspectMode { Original, Ratio16x9, Ratio4x3, Ratio235x100, Stretch };

inline constexpr int kDefaultSubtitleFontSize = 38;  // mpv's own default.
inline constexpr double kDefaultSubtitleOutlineSize = 1.65;  // Likewise.

struct SubtitleStyle {
    std::wstring font;  // Empty means mpv's default.
    int fontSize = kDefaultSubtitleFontSize;
    double outlineSize = kDefaultSubtitleOutlineSize;

    bool operator==(const SubtitleStyle&) const = default;
};

// Picture settings, each from -100 to 100 with 0 meaning unchanged.
enum class VideoAdjustment { Brightness, Contrast, Saturation, Hue, Gamma };
inline constexpr size_t kVideoAdjustmentCount = 5;

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
    std::vector<Chapter> chapters;
    std::optional<int64_t> chapter;  // Index of the current one. Negative before the first.
    std::vector<std::pair<std::wstring, std::wstring>> metadata;
    double subtitleDelay = 0.0;
    SubtitleEncoding subtitleEncoding = SubtitleEncoding::Korean;
    double speed = 1.0;
    double audioDelay = 0.0;
    std::optional<double> loopStart;
    std::optional<double> loopEnd;
    AspectMode aspect = AspectMode::Original;
    int rotation = 0;
    std::array<int, kVideoAdjustmentCount> adjustments{};
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
        // A newly opened video is ready to show, at this size in pixels. Not called for audio.
        std::function<void(int width, int height)> videoReady;
    };

    // Renders into videoWindow. Throws std::runtime_error if mpv can't start.
    MpvPlayer(HWND videoWindow, Callbacks callbacks, double volume, SubtitleEncoding subtitleEncoding,
              const std::array<int, kVideoAdjustmentCount>& videoAdjustments, const SubtitleStyle& subtitleStyle);
    ~MpvPlayer();

    MpvPlayer(const MpvPlayer&) = delete;
    MpvPlayer& operator=(const MpvPlayer&) = delete;

    // Subtitle files are offered in the given order, and mpv favors the first.
    void Open(const std::wstring& path, const std::vector<std::wstring>& subtitleFiles);
    void TogglePause();
    void SeekBy(int seconds);
    // An exact seek lands on the requested time. Otherwise it lands on a nearby
    // keyframe, which is much faster on heavy video.
    void SeekTo(double seconds, bool exact);
    void ChangeVolume(int delta);
    void SetVolume(double volume);

    // Both keep the speed within the supported range. ChangeSpeed returns the result.
    void SetSpeed(double speed);
    double ChangeSpeed(double delta);
    // These mark the current position. With only one end marked, the file's own start or end is the other.
    void SetLoopStart();
    void SetLoopEnd();
    void ClearLoop();
    void SetLoop(double start, double end);
    // Pauses if playing, then moves by one frame.
    void StepFrame(bool forward);

    void SetAspect(AspectMode aspect);
    void SetRotation(int degrees);
    // Returns the setting's new value.
    int ChangeVideoAdjustment(VideoAdjustment adjustment, int delta);
    void ResetVideoAdjustments();

    // Returns the new delay, in seconds.
    double ChangeAudioDelay(double seconds);
    void ResetAudioDelay();

    // Adds a subtitle track to the current file, and optionally switches to it.
    void AddSubtitleFile(const std::wstring& path, bool select);
    // An empty id turns subtitles off.
    void SelectSubtitle(std::optional<int64_t> trackId);
    void SelectAudio(int64_t trackId);
    void ChangeSubtitleDelay(double seconds);
    void ResetSubtitleDelay();
    void SetSubtitleEncoding(SubtitleEncoding encoding);
    void SetSubtitleStyle(const SubtitleStyle& style);

    // Shows a short message over the video.
    void ShowMessage(const std::wstring& text);

    void ProcessEvents();

    // Current values of mpv's properties, for display: empty if there's no such value
    // right now. Unlike the control methods these wait for mpv, so call them only occasionally.
    std::wstring PropertyText(const char* name) const;
    std::optional<double> PropertyNumber(const char* name) const;

private:
    void Command(std::initializer_list<const char*> arguments, uint64_t replyId = 0);
    void UpdateState(const mpv_event_property& property);
    void UpdateTracks(const mpv_node& list);
    void UpdateChapters(const mpv_node& list);
    void UpdateMetadata(const mpv_node& map);
    void ReportVideoSizeIfPending();

    mpv_handle* mpv_ = nullptr;
    Callbacks callbacks_;
    PlaybackState state_;
    bool videoSizePending_ = false;
};
