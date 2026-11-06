#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>

#include "MpvPlayer.h"

// What the user picked from the right-click menu.
struct MenuChoice {
    enum class Action {
        None,
        OpenVideo,
        OpenSubtitle,
        SelectSubtitle,  // trackId empty means subtitles off.
        SelectAudio,
        ResetSubtitleDelay,
        SetSubtitleEncoding,
    };
    Action action = Action::None;
    std::optional<int64_t> trackId;
    SubtitleEncoding encoding = SubtitleEncoding::Korean;
};

// Shows the menu at a screen position and blocks until it closes. Playback keeps running.
MenuChoice ShowContextMenu(HWND owner, POINT screenPoint, const PlaybackState& state);

// A short human-readable name for a track, for menus and on-screen messages.
std::wstring TrackLabel(const MediaTrack& track);
