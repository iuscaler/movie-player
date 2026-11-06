#include "MpvPlayer.h"

#include <mpv/client.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

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

}  // namespace

MpvPlayer::MpvPlayer(HWND videoWindow, Callbacks callbacks) : callbacks_(std::move(callbacks)) {
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
        Check(mpv_initialize(mpv_), "mpv_initialize");

        Check(mpv_observe_property(mpv_, 0, "filename", MPV_FORMAT_STRING), "observe filename");
        Check(mpv_observe_property(mpv_, 0, "time-pos", MPV_FORMAT_DOUBLE), "observe time-pos");
        Check(mpv_observe_property(mpv_, 0, "duration", MPV_FORMAT_DOUBLE), "observe duration");
        Check(mpv_observe_property(mpv_, 0, "volume", MPV_FORMAT_DOUBLE), "observe volume");
        Check(mpv_observe_property(mpv_, 0, "pause", MPV_FORMAT_FLAG), "observe pause");
        Check(mpv_observe_property(mpv_, 0, "eof-reached", MPV_FORMAT_FLAG), "observe eof-reached");
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

void MpvPlayer::Open(const std::wstring& path) {
    const std::string utf8Path = ToUtf8(path);
    Command({"loadfile", utf8Path.c_str()});
    // Reaching the end of the previous file leaves mpv paused, and so does the user.
    Command({"set", "pause", "no"});
}

void MpvPlayer::TogglePause() {
    if (state_.atEnd) {
        Command({"seek", "0", "absolute"});
        Command({"set", "pause", "no"});
    } else {
        Command({"osd-msg", "cycle", "pause"});
    }
}

void MpvPlayer::SeekBy(int seconds) {
    const std::string amount = std::to_string(seconds);
    Command({"osd-msg-bar", "seek", amount.c_str(), "relative+exact"});
}

void MpvPlayer::ChangeVolume(int delta) {
    const std::string amount = std::to_string(delta);
    Command({"osd-msg-bar", "add", "volume", amount.c_str()});
}

void MpvPlayer::Command(std::initializer_list<const char*> arguments) {
    std::vector<const char*> command(arguments);
    command.push_back(nullptr);
    mpv_command_async(mpv_, 0, command.data());
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
