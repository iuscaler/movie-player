#pragma once

#include <windows.h>

#include <array>
#include <optional>

#include "MpvPlayer.h"

// What the app remembers between runs. Stored in %APPDATA%\MoviePlayer\settings.ini.
struct Settings {
    double volume = 100.0;
    SubtitleEncoding subtitleEncoding = SubtitleEncoding::Korean;
    // The window's normal, non-maximized rectangle. Opening a video keeps its
    // position but replaces its size with one that fits the video.
    std::optional<RECT> windowPosition;
    bool windowMaximized = false;
    std::array<int, kVideoAdjustmentCount> videoAdjustments{};
};

// Missing or invalid values fall back to the defaults above.
Settings LoadSettings();
void SaveSettings(const Settings& settings);
