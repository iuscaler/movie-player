#pragma once

#include <windows.h>

#include <array>
#include <optional>
#include <string>

// Everything a keyboard shortcut can do. AllShortcuts() lists them in this order.
enum class ShortcutAction {
    TogglePause,
    SeekBackward,
    SeekForward,
    VolumeUp,
    VolumeDown,
    ToggleFullscreen,
    ExitFullscreen,
    CycleSubtitle,
    CycleAudio,
    SubtitleDelayEarlier,
    SubtitleDelayLater,
    SubtitleDelayReset,
    SpeedUp,
    SpeedDown,
    SpeedReset,
    LoopStart,
    LoopEnd,
    LoopClear,
    FrameForward,
    FrameBackward,
    BrightnessDown,
    BrightnessUp,
    ContrastDown,
    ContrastUp,
    SaturationDown,
    SaturationUp,
    HueDown,
    HueUp,
    GammaDown,
    GammaUp,
    VideoReset,
    AudioDelayEarlier,
    AudioDelayLater,
    AudioDelayReset,
    ChapterList,
    ShowInfo,
    OpenSettings,
    WindowSize50,
    WindowSize100,
    WindowSize150,
    WindowSize200,
    Count,
};
inline constexpr size_t kShortcutCount = static_cast<size_t>(ShortcutAction::Count);

// A key with its modifiers: the virtual-key code in the low byte, and these flags above it.
using KeyChord = int;
inline constexpr KeyChord kNoKey = 0;
inline constexpr KeyChord kCtrlFlag = 0x100;
inline constexpr KeyChord kShiftFlag = 0x200;
inline constexpr KeyChord kAltFlag = 0x400;

using ShortcutTable = std::array<KeyChord, kShortcutCount>;

struct ShortcutInfo {
    const wchar_t* settingsName;
    const wchar_t* label;
    KeyChord defaultKey;
    bool repeats;  // Whether holding the key down keeps doing it.
};

const std::array<ShortcutInfo, kShortcutCount>& AllShortcuts();
const ShortcutInfo& GetShortcutInfo(ShortcutAction action);
ShortcutTable DefaultShortcuts();

// The given key with whichever modifiers are held down right now.
KeyChord ChordFromKey(WPARAM key);
std::optional<ShortcutAction> FindShortcut(const ShortcutTable& shortcuts, KeyChord chord);

// For display, such as "Ctrl+←". Empty for kNoKey.
std::wstring KeyChordName(KeyChord chord);

bool IsModifierKey(WPARAM key);
// False for keys that Windows or the input method keeps for itself, such as Alt+F4.
bool CanBeShortcut(KeyChord chord);
