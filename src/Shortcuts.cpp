#include "Shortcuts.h"

namespace {

constexpr int kKeyMask = 0xFF;

constexpr std::array<ShortcutInfo, kShortcutCount> kShortcuts{{
    {L"TogglePause", L"재생/일시정지", VK_SPACE, false},
    {L"SeekBackward", L"뒤로 이동", VK_LEFT, true},
    {L"SeekForward", L"앞으로 이동", VK_RIGHT, true},
    {L"VolumeUp", L"볼륨 높이기", VK_UP, true},
    {L"VolumeDown", L"볼륨 낮추기", VK_DOWN, true},
    {L"ToggleFullscreen", L"전체화면 전환", VK_RETURN, false},
    {L"ExitFullscreen", L"전체화면 해제", VK_ESCAPE, false},
    {L"CycleSubtitle", L"자막 트랙 전환", 'S', false},
    {L"CycleAudio", L"오디오 트랙 전환", 'A', false},
    {L"SubtitleDelayEarlier", L"자막 싱크 빠르게", VK_OEM_COMMA, true},
    {L"SubtitleDelayLater", L"자막 싱크 느리게", VK_OEM_PERIOD, true},
    {L"SubtitleDelayReset", L"자막 싱크 초기화", VK_OEM_2, false},
    {L"SpeedUp", L"배속 빠르게", 'C', true},
    {L"SpeedDown", L"배속 느리게", 'X', true},
    {L"SpeedReset", L"배속 원래대로", 'Z', false},
    {L"LoopStart", L"구간 반복 시작 지정", 'J', false},
    {L"LoopEnd", L"구간 반복 끝 지정", 'K', false},
    {L"LoopClear", L"구간 반복 해제", 'L', false},
    {L"FrameForward", L"다음 프레임", 'F', true},
    {L"FrameBackward", L"이전 프레임", 'D', true},
    {L"BrightnessDown", L"밝기 낮추기", 'Q', true},
    {L"BrightnessUp", L"밝기 높이기", 'W', true},
    {L"ContrastDown", L"대비 낮추기", 'E', true},
    {L"ContrastUp", L"대비 높이기", 'R', true},
    {L"SaturationDown", L"채도 낮추기", 'T', true},
    {L"SaturationUp", L"채도 높이기", 'Y', true},
    {L"HueDown", L"색조 낮추기", 'U', true},
    {L"HueUp", L"색조 높이기", 'I', true},
    {L"GammaDown", L"감마 낮추기", 'O', true},
    {L"GammaUp", L"감마 높이기", 'P', true},
    {L"VideoReset", L"영상 조정 초기화", VK_BACK, false},
    {L"AudioDelayEarlier", L"음성 싱크 빠르게", VK_OEM_4, true},
    {L"AudioDelayLater", L"음성 싱크 느리게", VK_OEM_6, true},
    {L"AudioDelayReset", L"음성 싱크 초기화", VK_OEM_5, false},
    {L"ChapterList", L"챕터 목록", 'H', false},
    {L"ShowInfo", L"재생 정보", VK_F1, false},
    {L"OpenSettings", L"설정 열기", VK_F5, false},
    {L"WindowSize50", L"화면 크기 50%", '1', false},
    {L"WindowSize100", L"화면 크기 100%", '2', false},
    {L"WindowSize150", L"화면 크기 150%", '3', false},
    {L"WindowSize200", L"화면 크기 200%", '4', false},
}};

std::wstring KeyName(int key) {
    switch (key) {
    case VK_SPACE: return L"Space";
    case VK_RETURN: return L"Enter";
    case VK_ESCAPE: return L"Esc";
    case VK_BACK: return L"Backspace";
    case VK_TAB: return L"Tab";
    case VK_LEFT: return L"←";
    case VK_RIGHT: return L"→";
    case VK_UP: return L"↑";
    case VK_DOWN: return L"↓";
    case VK_INSERT: return L"Insert";
    case VK_DELETE: return L"Delete";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_PRIOR: return L"PageUp";
    case VK_NEXT: return L"PageDown";
    case VK_PAUSE: return L"Pause";
    case VK_MULTIPLY: return L"Num *";
    case VK_ADD: return L"Num +";
    case VK_SUBTRACT: return L"Num -";
    case VK_DECIMAL: return L"Num .";
    case VK_DIVIDE: return L"Num /";
    case VK_HANGUL: return L"한/영";
    case VK_HANJA: return L"한자";
    default: break;
    }
    if (key >= VK_F1 && key <= VK_F24) {
        return L"F" + std::to_wstring(key - VK_F1 + 1);
    }
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) {
        return L"Num " + std::to_wstring(key - VK_NUMPAD0);
    }
    const UINT character = MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VK_TO_CHAR) & 0x7FFF;
    if (character > L' ') {
        return std::wstring(1, static_cast<wchar_t>(character));
    }
    return L"키 " + std::to_wstring(key);
}

}  // namespace

const std::array<ShortcutInfo, kShortcutCount>& AllShortcuts() {
    return kShortcuts;
}

const ShortcutInfo& GetShortcutInfo(ShortcutAction action) {
    return kShortcuts[static_cast<size_t>(action)];
}

ShortcutTable DefaultShortcuts() {
    ShortcutTable table{};
    for (size_t i = 0; i < kShortcutCount; ++i) {
        table[i] = kShortcuts[i].defaultKey;
    }
    return table;
}

KeyChord ChordFromKey(WPARAM key) {
    KeyChord chord = static_cast<KeyChord>(key) & kKeyMask;
    if (GetKeyState(VK_CONTROL) < 0) {
        chord |= kCtrlFlag;
    }
    if (GetKeyState(VK_SHIFT) < 0) {
        chord |= kShiftFlag;
    }
    if (GetKeyState(VK_MENU) < 0) {
        chord |= kAltFlag;
    }
    return chord;
}

std::optional<ShortcutAction> FindShortcut(const ShortcutTable& shortcuts, KeyChord chord) {
    if (chord == kNoKey) {
        return std::nullopt;
    }
    for (size_t i = 0; i < shortcuts.size(); ++i) {
        if (shortcuts[i] == chord) {
            return static_cast<ShortcutAction>(i);
        }
    }
    return std::nullopt;
}

std::wstring KeyChordName(KeyChord chord) {
    if (chord == kNoKey) {
        return {};
    }
    std::wstring name;
    if (chord & kCtrlFlag) {
        name += L"Ctrl+";
    }
    if (chord & kShiftFlag) {
        name += L"Shift+";
    }
    if (chord & kAltFlag) {
        name += L"Alt+";
    }
    return name + KeyName(chord & kKeyMask);
}

bool IsModifierKey(WPARAM key) {
    switch (key) {
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
        return true;
    default:
        return false;
    }
}

bool CanBeShortcut(KeyChord chord) {
    const int key = chord & kKeyMask;
    if (key == 0 || (chord & ~(kKeyMask | kCtrlFlag | kShiftFlag | kAltFlag)) != 0 || IsModifierKey(static_cast<WPARAM>(key))) {
        return false;
    }
    switch (key) {
    case VK_LWIN:
    case VK_RWIN:
    case VK_APPS:      // Opens the right-click menu.
    case VK_SNAPSHOT:  // Windows only reports this one on release.
    case VK_HANGUL:    // Input-method keys. These are the right Alt and Ctrl on Korean keyboards.
    case VK_HANJA:
    case VK_PROCESSKEY:
    case VK_PACKET:
        return false;
    default:
        break;
    }
    if (chord & kAltFlag) {
        if (key == VK_F4 || key == VK_SPACE || key == VK_TAB || key == VK_ESCAPE) {
            return false;
        }
    }
    return !((chord & kCtrlFlag) && key == VK_ESCAPE);  // Opens the Start menu.
}
