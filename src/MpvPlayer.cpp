#include "MpvPlayer.h"

#include <mpv/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr uint64_t kAddSubtitleReply = 1;
constexpr double kMinSpeed = 0.25;
constexpr double kMaxSpeed = 4.0;
constexpr int kMaxAdjustment = 100;
constexpr std::array<const char*, kVideoAdjustmentCount> kAdjustmentProperties{"brightness", "contrast", "saturation", "hue", "gamma"};

// mpv's API uses UTF-8 strings, while Windows paths are UTF-16.
std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int length = static_cast<int>(text.size());
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), length, result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring ToWide(const char* text) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), size);
    result.resize(static_cast<size_t>(size) - 1);  // Drop the terminator.
    return result;
}

void Check(int status, const char* action) {
    if (status < 0) {
        throw std::runtime_error(std::string(action) + ": " + mpv_error_string(status));
    }
}

// Set this environment variable to a file path to get mpv's log for debugging.
void EnableLogFileIfRequested(mpv_handle* mpv) {
    wchar_t path[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"MOVIE_PLAYER_MPV_LOG", path, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        Check(mpv_set_property_string(mpv, "log-file", ToUtf8(path).c_str()), "set log-file");
    }
}

std::optional<double> DoubleValue(const mpv_event_property& property) {
    if (property.format != MPV_FORMAT_DOUBLE) {
        return std::nullopt;
    }
    return *static_cast<const double*>(property.data);
}

bool FlagValue(const mpv_event_property& property) {
    return property.format == MPV_FORMAT_FLAG && *static_cast<const int*>(property.data) != 0;
}

// mpv counts from the top, with 100 as the bottom.
std::string PositionOption(const SubtitleStyle& style) {
    return std::to_string(100 - style.heightPercent);
}

std::string FontOption(const SubtitleStyle& style) {
    return style.font.empty() ? "sans-serif" : ToUtf8(style.font);
}

const char* EncodingOption(SubtitleEncoding encoding) {
    return encoding == SubtitleEncoding::Korean ? "cp949" : "auto";
}

MediaTrack TrackFromNode(const mpv_node& node) {
    MediaTrack track;
    const mpv_node_list& fields = *node.u.list;
    for (int i = 0; i < fields.num; ++i) {
        const char* key = fields.keys[i];
        const mpv_node& value = fields.values[i];
        if (value.format == MPV_FORMAT_INT64) {
            if (std::strcmp(key, "id") == 0) {
                track.id = value.u.int64;
            } else if (std::strcmp(key, "demux-channel-count") == 0) {
                track.channels = static_cast<int>(value.u.int64);
            } else if (std::strcmp(key, "demux-samplerate") == 0) {
                track.sampleRate = static_cast<int>(value.u.int64);
            } else if (std::strcmp(key, "demux-w") == 0) {
                track.width = static_cast<int>(value.u.int64);
            } else if (std::strcmp(key, "demux-h") == 0) {
                track.height = static_cast<int>(value.u.int64);
            } else if (std::strcmp(key, "demux-bitrate") == 0) {
                track.bitrate = value.u.int64;
            }
        } else if (value.format == MPV_FORMAT_DOUBLE) {
            if (std::strcmp(key, "demux-fps") == 0) {
                track.fps = value.u.double_;
            }
        } else if (value.format == MPV_FORMAT_FLAG) {
            if (std::strcmp(key, "selected") == 0) {
                track.selected = value.u.flag != 0;
            } else if (std::strcmp(key, "default") == 0) {
                track.isDefault = value.u.flag != 0;
            } else if (std::strcmp(key, "forced") == 0) {
                track.forced = value.u.flag != 0;
            }
        } else if (value.format == MPV_FORMAT_STRING) {
            if (std::strcmp(key, "type") == 0) {
                track.type = std::strcmp(value.u.string, "audio") == 0 ? TrackType::Audio
                           : std::strcmp(value.u.string, "sub") == 0   ? TrackType::Subtitle
                                                                       : TrackType::Other;
            } else if (std::strcmp(key, "title") == 0) {
                track.title = ToWide(value.u.string);
            } else if (std::strcmp(key, "lang") == 0) {
                track.language = ToWide(value.u.string);
            } else if (std::strcmp(key, "codec") == 0) {
                track.codec = ToWide(value.u.string);
            } else if (std::strcmp(key, "codec-desc") == 0) {
                track.codecDescription = ToWide(value.u.string);
            } else if (std::strcmp(key, "codec-profile") == 0) {
                track.codecProfile = ToWide(value.u.string);
            } else if (std::strcmp(key, "demux-channels") == 0) {
                track.channelLayout = ToWide(value.u.string);
            } else if (std::strcmp(key, "external-filename") == 0) {
                const std::wstring path = ToWide(value.u.string);
                const size_t slash = path.find_last_of(L"\\/");
                track.externalFileName = slash == std::wstring::npos ? path : path.substr(slash + 1);
            }
        }
    }
    return track;
}

Chapter ChapterFromNode(const mpv_node& node) {
    Chapter chapter;
    const mpv_node_list& fields = *node.u.list;
    for (int i = 0; i < fields.num; ++i) {
        const mpv_node& value = fields.values[i];
        if (std::strcmp(fields.keys[i], "time") == 0 && value.format == MPV_FORMAT_DOUBLE) {
            chapter.time = value.u.double_;
        } else if (std::strcmp(fields.keys[i], "title") == 0 && value.format == MPV_FORMAT_STRING) {
            chapter.title = ToWide(value.u.string);
        }
    }
    return chapter;
}

}  // namespace

MpvPlayer::MpvPlayer(HWND videoWindow, Callbacks callbacks, double volume, SubtitleEncoding subtitleEncoding,
                     const std::array<int, kVideoAdjustmentCount>& videoAdjustments, const SubtitleStyle& subtitleStyle)
    : callbacks_(std::move(callbacks)) {
    state_.volume = volume;
    state_.subtitleEncoding = subtitleEncoding;
    state_.adjustments = videoAdjustments;
    mpv_ = mpv_create();
    if (!mpv_) {
        throw std::runtime_error("mpv_create failed");
    }

    try {
        EnableLogFileIfRequested(mpv_);
        int64_t windowId = reinterpret_cast<int64_t>(videoWindow);
        Check(mpv_set_property(mpv_, "wid", MPV_FORMAT_INT64, &windowId), "set wid");
        Check(mpv_set_property_string(mpv_, "hwdec", "auto-safe"), "set hwdec");
        // Hold the last frame at the end, so seeking past the end doesn't close the file.
        Check(mpv_set_property_string(mpv_, "keep-open", "yes"), "set keep-open");
        // Also pick up subtitles named like "movie.ko.srt", and prefer Korean tracks inside files.
        Check(mpv_set_property_string(mpv_, "sub-auto", "fuzzy"), "set sub-auto");
        Check(mpv_set_property_string(mpv_, "slang", "ko,kor"), "set slang");
        // mpv's detection misreads CP949, the usual encoding of older Korean subtitles.
        // Unicode files are still recognized with this setting.
        Check(mpv_set_property_string(mpv_, "sub-codepage", EncodingOption(state_.subtitleEncoding)), "set sub-codepage");
        Check(mpv_set_property_string(mpv_, "sub-font", FontOption(subtitleStyle).c_str()), "set sub-font");
        Check(mpv_set_property_string(mpv_, "sub-font-size", std::to_string(subtitleStyle.fontSize).c_str()), "set sub-font-size");
        Check(mpv_set_property_string(mpv_, "sub-outline-size", std::to_string(subtitleStyle.outlineSize).c_str()), "set sub-outline-size");
        Check(mpv_set_property_string(mpv_, "sub-pos", PositionOption(subtitleStyle).c_str()), "set sub-pos");
        Check(mpv_set_property(mpv_, "volume", MPV_FORMAT_DOUBLE, &state_.volume), "set volume");
        for (size_t i = 0; i < kAdjustmentProperties.size(); ++i) {
            const std::string value = std::to_string(state_.adjustments[i]);
            Check(mpv_set_property_string(mpv_, kAdjustmentProperties[i], value.c_str()), "set picture adjustment");
        }
        Check(mpv_initialize(mpv_), "mpv_initialize");

        // The app finds SMI files itself, since they may need splitting by language.
        Command({"change-list", "sub-auto-exts", "remove", "smi"});

        Check(mpv_observe_property(mpv_, 0, "filename", MPV_FORMAT_STRING), "observe filename");
        Check(mpv_observe_property(mpv_, 0, "time-pos", MPV_FORMAT_DOUBLE), "observe time-pos");
        Check(mpv_observe_property(mpv_, 0, "duration", MPV_FORMAT_DOUBLE), "observe duration");
        Check(mpv_observe_property(mpv_, 0, "volume", MPV_FORMAT_DOUBLE), "observe volume");
        Check(mpv_observe_property(mpv_, 0, "pause", MPV_FORMAT_FLAG), "observe pause");
        Check(mpv_observe_property(mpv_, 0, "eof-reached", MPV_FORMAT_FLAG), "observe eof-reached");
        Check(mpv_observe_property(mpv_, 0, "track-list", MPV_FORMAT_NODE), "observe track-list");
        Check(mpv_observe_property(mpv_, 0, "chapter-list", MPV_FORMAT_NODE), "observe chapter-list");
        Check(mpv_observe_property(mpv_, 0, "chapter", MPV_FORMAT_INT64), "observe chapter");
        Check(mpv_observe_property(mpv_, 0, "metadata", MPV_FORMAT_NODE), "observe metadata");
        Check(mpv_observe_property(mpv_, 0, "sub-delay", MPV_FORMAT_DOUBLE), "observe sub-delay");
        Check(mpv_observe_property(mpv_, 0, "ab-loop-a", MPV_FORMAT_DOUBLE), "observe ab-loop-a");
        Check(mpv_observe_property(mpv_, 0, "ab-loop-b", MPV_FORMAT_DOUBLE), "observe ab-loop-b");
    } catch (...) {
        mpv_terminate_destroy(mpv_);
        throw;
    }

    mpv_set_wakeup_callback(
        mpv_,
        [](void* player) { static_cast<MpvPlayer*>(player)->callbacks_.wakeup(); },
        this);
}

MpvPlayer::~MpvPlayer() {
    mpv_set_wakeup_callback(mpv_, nullptr, nullptr);
    mpv_terminate_destroy(mpv_);
}

void MpvPlayer::Open(const std::wstring& path, const std::vector<std::wstring>& subtitleFiles) {
    std::vector<std::string> utf8Files;
    for (const std::wstring& file : subtitleFiles) {
        utf8Files.push_back(ToUtf8(file));
    }
    std::vector<mpv_node> items(utf8Files.size());
    for (size_t i = 0; i < utf8Files.size(); ++i) {
        items[i].format = MPV_FORMAT_STRING;
        items[i].u.string = utf8Files[i].data();
    }
    mpv_node_list list{static_cast<int>(items.size()), items.data(), nullptr};
    mpv_node files{};
    files.format = MPV_FORMAT_NODE_ARRAY;
    files.u.list = &list;
    // Replaces the list from the previous file. Requests run in order, so this applies to the load below.
    mpv_set_property_async(mpv_, 0, "sub-files", MPV_FORMAT_NODE, &files);

    videoSizePending_ = true;
    const std::string utf8Path = ToUtf8(path);
    // These belong to the file they were made for. Speed and picture settings carry over.
    Command({"set", "sub-delay", "0"});
    ResetAudioDelay();
    ClearLoop();
    SetRotation(0);
    SetAspect(AspectMode::Original);
    Command({"loadfile", utf8Path.c_str()});
    // Reaching the end of the previous file leaves mpv paused, and so does the user.
    Command({"set", "pause", "no"});
}

void MpvPlayer::SetSubtitleStyle(const SubtitleStyle& style) {
    const std::string font = FontOption(style);
    const std::string size = std::to_string(style.fontSize);
    Command({"set", "sub-font", font.c_str()});
    Command({"set", "sub-font-size", size.c_str()});
    const std::string outline = std::to_string(style.outlineSize);
    Command({"set", "sub-outline-size", outline.c_str()});
    const std::string position = PositionOption(style);
    Command({"set", "sub-pos", position.c_str()});
}

void MpvPlayer::TogglePause() {
    if (state_.atEnd) {
        Command({"seek", "0", "absolute"});
        Command({"set", "pause", "no"});
    } else {
        Command({"cycle", "pause"});
    }
}

void MpvPlayer::SeekBy(int seconds) {
    const std::string amount = std::to_string(seconds);
    Command({"seek", amount.c_str(), "relative+exact"});
}

void MpvPlayer::SeekTo(double seconds, bool exact) {
    const std::string target = std::to_string(seconds);
    Command({"seek", target.c_str(), exact ? "absolute+exact" : "absolute+keyframes"});
}

void MpvPlayer::ChangeVolume(int delta) {
    const std::string amount = std::to_string(delta);
    Command({"add", "volume", amount.c_str()});
}

void MpvPlayer::SetVolume(double volume) {
    const std::string value = std::to_string(volume);
    Command({"set", "volume", value.c_str()});
}

void MpvPlayer::SetSpeed(double speed) {
    state_.speed = std::clamp(speed, kMinSpeed, kMaxSpeed);
    const std::string value = std::to_string(state_.speed);
    Command({"set", "speed", value.c_str()});
    callbacks_.stateChanged(state_);
}

double MpvPlayer::ChangeSpeed(double delta) {
    // Rounding keeps repeated steps from drifting to values like 1.3000000000000003.
    SetSpeed(std::round((state_.speed + delta) * 100.0) / 100.0);
    return state_.speed;
}

// mpv fills in the position itself. The last one it reported can be slightly behind,
// and an end point that playback has already passed never triggers the loop.
void MpvPlayer::SetLoopStart() {
    Command({"expand-properties", "set", "ab-loop-a", "${=time-pos}"});
}

void MpvPlayer::SetLoopEnd() {
    Command({"expand-properties", "set", "ab-loop-b", "${=time-pos}"});
}

void MpvPlayer::ClearLoop() {
    Command({"set", "ab-loop-a", "no"});
    Command({"set", "ab-loop-b", "no"});
}

void MpvPlayer::SetLoop(double start, double end) {
    const std::string startValue = std::to_string(start);
    const std::string endValue = std::to_string(end);
    Command({"set", "ab-loop-a", startValue.c_str()});
    Command({"set", "ab-loop-b", endValue.c_str()});
}

void MpvPlayer::StepFrame(bool forward) {
    Command({forward ? "frame-step" : "frame-back-step"});
}

void MpvPlayer::SetAspect(AspectMode aspect) {
    state_.aspect = aspect;
    const char* ratio = "no";
    if (aspect == AspectMode::Ratio16x9) {
        ratio = "16:9";
    } else if (aspect == AspectMode::Ratio4x3) {
        ratio = "4:3";
    } else if (aspect == AspectMode::Ratio235x100) {
        ratio = "2.35:1";
    }
    Command({"set", "video-aspect-override", ratio});
    Command({"set", "keepaspect", aspect == AspectMode::Stretch ? "no" : "yes"});
    callbacks_.stateChanged(state_);
}

void MpvPlayer::SetRotation(int degrees) {
    state_.rotation = degrees;
    const std::string value = std::to_string(degrees);
    Command({"set", "video-rotate", value.c_str()});
    callbacks_.stateChanged(state_);
}

int MpvPlayer::ChangeVideoAdjustment(VideoAdjustment adjustment, int delta) {
    const size_t index = static_cast<size_t>(adjustment);
    state_.adjustments[index] = std::clamp(state_.adjustments[index] + delta, -kMaxAdjustment, kMaxAdjustment);
    const std::string value = std::to_string(state_.adjustments[index]);
    Command({"set", kAdjustmentProperties[index], value.c_str()});
    callbacks_.stateChanged(state_);
    return state_.adjustments[index];
}

void MpvPlayer::ResetVideoAdjustments() {
    state_.adjustments.fill(0);
    for (const char* property : kAdjustmentProperties) {
        Command({"set", property, "0"});
    }
    callbacks_.stateChanged(state_);
}

double MpvPlayer::ChangeAudioDelay(double seconds) {
    state_.audioDelay = std::round((state_.audioDelay + seconds) * 1000.0) / 1000.0;
    const std::string value = std::to_string(state_.audioDelay);
    Command({"set", "audio-delay", value.c_str()});
    callbacks_.stateChanged(state_);
    return state_.audioDelay;
}

void MpvPlayer::ResetAudioDelay() {
    state_.audioDelay = 0.0;
    Command({"set", "audio-delay", "0"});
    callbacks_.stateChanged(state_);
}

void MpvPlayer::AddSubtitleFile(const std::wstring& path, bool select) {
    const std::string utf8Path = ToUtf8(path);
    Command({"sub-add", utf8Path.c_str(), select ? "select" : "auto"}, kAddSubtitleReply);
}

void MpvPlayer::SelectSubtitle(std::optional<int64_t> trackId) {
    const std::string id = trackId ? std::to_string(*trackId) : "no";
    Command({"set", "sid", id.c_str()});
}

void MpvPlayer::SelectAudio(int64_t trackId) {
    const std::string id = std::to_string(trackId);
    Command({"set", "aid", id.c_str()});
}

void MpvPlayer::ChangeSubtitleDelay(double seconds) {
    const std::string amount = std::to_string(seconds);
    Command({"add", "sub-delay", amount.c_str()});
}

void MpvPlayer::ResetSubtitleDelay() {
    Command({"set", "sub-delay", "0"});
}

void MpvPlayer::SetSubtitleEncoding(SubtitleEncoding encoding) {
    state_.subtitleEncoding = encoding;
    Command({"set", "sub-codepage", EncodingOption(encoding)});
    // Files already loaded were decoded with the old setting.
    Command({"sub-reload"});
    callbacks_.stateChanged(state_);
}

void MpvPlayer::ShowMessage(const std::wstring& text) {
    const std::string utf8Text = ToUtf8(text);
    Command({"show-text", utf8Text.c_str()});
}

void MpvPlayer::Command(std::initializer_list<const char*> arguments, uint64_t replyId) {
    std::vector<const char*> command(arguments);
    command.push_back(nullptr);
    mpv_command_async(mpv_, replyId, command.data());
}

void MpvPlayer::UpdateTracks(const mpv_node& list) {
    state_.tracks.clear();
    if (list.format != MPV_FORMAT_NODE_ARRAY) {
        return;
    }
    for (int i = 0; i < list.u.list->num; ++i) {
        const mpv_node& item = list.u.list->values[i];
        if (item.format == MPV_FORMAT_NODE_MAP) {
            state_.tracks.push_back(TrackFromNode(item));
        }
    }
}

void MpvPlayer::UpdateChapters(const mpv_node& list) {
    state_.chapters.clear();
    if (list.format != MPV_FORMAT_NODE_ARRAY) {
        return;
    }
    for (int i = 0; i < list.u.list->num; ++i) {
        const mpv_node& item = list.u.list->values[i];
        if (item.format == MPV_FORMAT_NODE_MAP) {
            state_.chapters.push_back(ChapterFromNode(item));
            if (state_.chapters.back().title.empty()) {
                state_.chapters.back().title = L"챕터 " + std::to_wstring(i + 1);
            }
        }
    }
}

void MpvPlayer::UpdateMetadata(const mpv_node& map) {
    state_.metadata.clear();
    if (map.format != MPV_FORMAT_NODE_MAP) {
        return;
    }
    for (int i = 0; i < map.u.list->num; ++i) {
        const mpv_node& value = map.u.list->values[i];
        if (value.format == MPV_FORMAT_STRING) {
            state_.metadata.emplace_back(ToWide(map.u.list->keys[i]), ToWide(value.u.string));
        }
    }
}

std::wstring MpvPlayer::PropertyText(const char* name) const {
    char* value = mpv_get_property_string(mpv_, name);
    if (!value) {
        return {};
    }
    std::wstring text = ToWide(value);
    mpv_free(value);
    return text;
}

std::optional<double> MpvPlayer::PropertyNumber(const char* name) const {
    double value = 0.0;
    if (mpv_get_property(mpv_, name, MPV_FORMAT_DOUBLE, &value) < 0) {
        return std::nullopt;
    }
    return value;
}

void MpvPlayer::UpdateState(const mpv_event_property& property) {
    const char* name = property.name;
    if (std::strcmp(name, "filename") == 0) {
        state_.fileName = property.format == MPV_FORMAT_STRING
            ? ToWide(*static_cast<const char* const*>(property.data))
            : std::wstring();
    } else if (std::strcmp(name, "time-pos") == 0) {
        state_.position = DoubleValue(property);
    } else if (std::strcmp(name, "duration") == 0) {
        state_.duration = DoubleValue(property);
    } else if (std::strcmp(name, "volume") == 0) {
        state_.volume = DoubleValue(property).value_or(state_.volume);
    } else if (std::strcmp(name, "pause") == 0) {
        state_.paused = FlagValue(property);
    } else if (std::strcmp(name, "eof-reached") == 0) {
        state_.atEnd = FlagValue(property);
    } else if (std::strcmp(name, "sub-delay") == 0) {
        state_.subtitleDelay = DoubleValue(property).value_or(0.0);
    } else if (std::strcmp(name, "ab-loop-a") == 0) {
        state_.loopStart = DoubleValue(property);
    } else if (std::strcmp(name, "ab-loop-b") == 0) {
        state_.loopEnd = DoubleValue(property);
    } else if (std::strcmp(name, "track-list") == 0) {
        if (property.format == MPV_FORMAT_NODE) {
            UpdateTracks(*static_cast<const mpv_node*>(property.data));
        } else {
            state_.tracks.clear();
        }
    } else if (std::strcmp(name, "chapter-list") == 0) {
        if (property.format == MPV_FORMAT_NODE) {
            UpdateChapters(*static_cast<const mpv_node*>(property.data));
        } else {
            state_.chapters.clear();
        }
    } else if (std::strcmp(name, "chapter") == 0) {
        state_.chapter = property.format == MPV_FORMAT_INT64
            ? std::optional<int64_t>(*static_cast<const int64_t*>(property.data))
            : std::nullopt;
    } else if (std::strcmp(name, "metadata") == 0) {
        if (property.format == MPV_FORMAT_NODE) {
            UpdateMetadata(*static_cast<const mpv_node*>(property.data));
        } else {
            state_.metadata.clear();
        }
    }
}

void MpvPlayer::ReportVideoSizeIfPending() {
    if (!videoSizePending_) {
        return;
    }
    int64_t width = 0;
    int64_t height = 0;
    if (mpv_get_property(mpv_, "dwidth", MPV_FORMAT_INT64, &width) < 0 ||
        mpv_get_property(mpv_, "dheight", MPV_FORMAT_INT64, &height) < 0) {
        return;  // Nothing to show, as with an audio file.
    }
    // The size ignores rotation stored in the file, which mpv applies when drawing.
    int64_t rotation = 0;
    if (mpv_get_property(mpv_, "video-params/rotate", MPV_FORMAT_INT64, &rotation) >= 0 && rotation % 180 != 0) {
        std::swap(width, height);
    }
    videoSizePending_ = false;
    callbacks_.videoReady(static_cast<int>(width), static_cast<int>(height));
}

void MpvPlayer::ProcessEvents() {
    bool stateChanged = false;
    while (true) {
        const mpv_event* event = mpv_wait_event(mpv_, 0);
        if (event->event_id == MPV_EVENT_NONE) {
            break;
        }
        if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            UpdateState(*static_cast<const mpv_event_property*>(event->data));
            stateChanged = true;
        } else if (event->event_id == MPV_EVENT_PLAYBACK_RESTART) {
            ReportVideoSizeIfPending();
        } else if (event->event_id == MPV_EVENT_COMMAND_REPLY) {
            if (event->reply_userdata == kAddSubtitleReply && event->error < 0) {
                callbacks_.subtitleLoadFailed();
            }
        } else if (event->event_id == MPV_EVENT_END_FILE) {
            const auto* endFile = static_cast<const mpv_event_end_file*>(event->data);
            if (endFile->reason == MPV_END_FILE_REASON_ERROR) {
                callbacks_.playbackError(ToWide(mpv_error_string(endFile->error)));
            }
        }
    }
    if (stateChanged) {
        callbacks_.stateChanged(state_);
    }
}
