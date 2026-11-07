#pragma once

#include <windows.h>

#include <array>
#include <optional>

#include "MpvPlayer.h"
#include "Shortcuts.h"

inline constexpr int kMinSeekSeconds = 1;
inline constexpr int kMaxSeekSeconds = 600;
inline constexpr int kMinSubtitleFontSize = 10;
inline constexpr int kMaxSubtitleFontSize = 150;
inline constexpr std::array<double, 10> kSubtitleOutlineSizes{0.0, 0.5, 1.0, kDefaultSubtitleOutlineSize, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0};

// What the settings window changes.
struct Preferences {
    int seekSeconds = 5;
    SubtitleStyle subtitleStyle;
    ShortcutTable shortcuts = DefaultShortcuts();

    bool operator==(const Preferences&) const = default;
};

// What the app remembers between runs. Stored in %APPDATA%\MoviePlayer\settings.ini.
struct Settings {
    double volume = 100.0;
    SubtitleEncoding subtitleEncoding = SubtitleEncoding::Korean;
    // The window's normal, non-maximized rectangle. Opening a video keeps its
    // position but replaces its size with one that fits the video.
    std::optional<RECT> windowPosition;
    bool windowMaximized = false;
    std::array<int, kVideoAdjustmentCount> videoAdjustments{};
    Preferences preferences;
};

// Missing or invalid values fall back to the defaults above.
Settings LoadSettings();
void SaveSettings(const Settings& settings);
