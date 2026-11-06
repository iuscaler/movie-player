#pragma once

#include <windows.h>

#include <optional>

#include "MpvPlayer.h"

// What the app remembers between runs. Stored in %APPDATA%\MoviePlayer\settings.ini.
struct Settings {
    double volume = 100.0;
    SubtitleEncoding subtitleEncoding = SubtitleEncoding::Korean;
    std::optional<RECT> windowPosition;  // The window's normal, non-maximized rectangle.
    bool windowMaximized = false;
};

// Missing or invalid values fall back to the defaults above.
Settings LoadSettings();
void SaveSettings(const Settings& settings);
