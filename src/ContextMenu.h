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
        RegisterFileAssociation,
        UnregisterFileAssociation,
        SetSpeed,
        ToggleLoop,
        ClearLoop,
        StepForward,
        StepBackward,
        SetAspect,
        SetRotation,
        AdjustVideo,
        ResetVideo,
        ResetAudioDelay,
    };
    Action action = Action::None;
    std::optional<int64_t> trackId;
    SubtitleEncoding encoding = SubtitleEncoding::Korean;
    double speed = 1.0;
    AspectMode aspect = AspectMode::Original;
    int rotation = 0;
    VideoAdjustment adjustment = VideoAdjustment::Brightness;
    int adjustmentDirection = 0;  // +1 or -1.
};

// Shows the menu at a screen position and blocks until it closes. Playback keeps running.
MenuChoice ShowContextMenu(HWND owner, POINT screenPoint, const PlaybackState& state, bool fileAssociationRegistered);

// A short human-readable name for a track, for menus and on-screen messages.
std::wstring TrackLabel(const MediaTrack& track);

// The same for other settings.
const wchar_t* AspectLabel(AspectMode aspect);
const wchar_t* VideoAdjustmentLabel(VideoAdjustment adjustment);
