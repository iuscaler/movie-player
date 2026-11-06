#include "MpvPlayer.h"

#include <mpv/client.h>

#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr uint64_t kAddSubtitleReply = 1;

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
            }
        } else if (value.format == MPV_FORMAT_FLAG) {
            if (std::strcmp(key, "selected") == 0) {
                track.selected = value.u.flag != 0;
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
            } else if (std::strcmp(key, "external-filename") == 0) {
                const std::wstring path = ToWide(value.u.string);
                const size_t slash = path.find_last_of(L"\\/");
                track.externalFileName = slash == std::wstring::npos ? path : path.substr(slash + 1);
            }
        }
    }
    return track;
}

}  // namespace

MpvPlayer::MpvPlayer(HWND videoWindow, Callbacks callbacks, double volume, SubtitleEncoding subtitleEncoding)
    : callbacks_(std::move(callbacks)) {
    state_.volume = volume;
    state_.subtitleEncoding = subtitleEncoding;
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
        Check(mpv_set_property(mpv_, "volume", MPV_FORMAT_DOUBLE, &state_.volume), "set volume");
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
        Check(mpv_observe_property(mpv_, 0, "sub-delay", MPV_FORMAT_DOUBLE), "observe sub-delay");
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

    const std::string utf8Path = ToUtf8(path);
    // A sync adjustment belongs to the file it was made for.
    Command({"set", "sub-delay", "0"});
    Command({"loadfile", utf8Path.c_str()});
    // Reaching the end of the previous file leaves mpv paused, and so does the user.
    Command({"set", "pause", "no"});
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

void MpvPlayer::SetSubtitlePosition(int percent) {
    const std::string value = std::to_string(percent);
    Command({"set", "sub-pos", value.c_str()});
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
    } else if (std::strcmp(name, "track-list") == 0) {
        if (property.format == MPV_FORMAT_NODE) {
            UpdateTracks(*static_cast<const mpv_node*>(property.data));
        } else {
            state_.tracks.clear();
        }
    }
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
