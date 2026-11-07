#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "MpvPlayer.h"

// One line of the playback information window, under a group heading.
struct InfoRow {
    std::wstring group;
    std::wstring label;
    std::wstring value;
};

// What's happening right now: position, decoder, frame rates, bitrates and so on.
std::vector<InfoRow> BuildPlaybackRows(const MpvPlayer& player, const PlaybackState& state);
// The open file: its container, metadata and tracks.
std::vector<InfoRow> BuildFileRows(const MpvPlayer& player, const PlaybackState& state);
// The player's libraries and this PC. window identifies the monitor to describe.
std::vector<InfoRow> BuildSystemRows(const MpvPlayer& player, HWND window);
