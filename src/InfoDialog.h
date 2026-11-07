#pragma once

#include <windows.h>

#include "MpvPlayer.h"
#include "Shortcuts.h"

// Shows the playback information window and blocks until it closes, keeping it up
// to date meanwhile. state must stay current: it's read again on each refresh.
// closeKey closes the window as well as Esc, so the key that opened it also closes it.
void ShowInfoDialog(HWND owner, const MpvPlayer& player, const PlaybackState& state, KeyChord closeKey);
