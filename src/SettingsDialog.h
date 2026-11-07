#pragma once

#include <windows.h>

#include <functional>

#include "Settings.h"

// Shows the settings window and blocks until it closes. Playback keeps running behind it.
// apply is called with the new preferences each time the user confirms a change.
void ShowSettingsDialog(HWND owner, const Preferences& current, const std::function<void(const Preferences&)>& apply);
