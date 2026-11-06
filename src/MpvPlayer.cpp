#include "MpvPlayer.h"

#include <mpv/client.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

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

}  // namespace

MpvPlayer::MpvPlayer(HWND videoWindow, Callbacks callbacks) : callbacks_(std::move(callbacks)) {
    mpv_ = mpv_create();
    if (!mpv_) {
        throw std::runtime_error("mpv_create failed");
    }

    try {
        int64_t windowId = reinterpret_cast<int64_t>(videoWindow);
        Check(mpv_set_property(mpv_, "wid", MPV_FORMAT_INT64, &windowId), "set wid");
        Check(mpv_set_property_string(mpv_, "hwdec", "auto-safe"), "set hwdec");
        Check(mpv_initialize(mpv_), "mpv_initialize");
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
    const char* command[] = {"loadfile", utf8Path.c_str(), nullptr};
    Check(mpv_command(mpv_, command), "loadfile");
}

void MpvPlayer::ProcessEvents() {
    while (true) {
        const mpv_event* event = mpv_wait_event(mpv_, 0);
        if (event->event_id == MPV_EVENT_NONE) {
            return;
        }
        if (event->event_id == MPV_EVENT_END_FILE) {
            const auto* endFile = static_cast<const mpv_event_end_file*>(event->data);
            if (endFile->reason == MPV_END_FILE_REASON_ERROR) {
                callbacks_.playbackError(ToWide(mpv_error_string(endFile->error)));
            }
        }
    }
}
