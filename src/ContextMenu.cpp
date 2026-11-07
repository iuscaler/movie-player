#include "ContextMenu.h"

#include <array>
#include <cwchar>

#include "TimeFormat.h"

namespace {

// Track ids are added to these bases to form menu command ids.
constexpr UINT kOpenVideo = 1;
constexpr UINT kOpenSubtitle = 2;
constexpr UINT kSubtitleOff = 3;
constexpr UINT kResetSubtitleDelay = 4;
constexpr UINT kEncodingKorean = 5;
constexpr UINT kEncodingAuto = 6;
constexpr UINT kOpenSettings = 7;
constexpr UINT kLoopStart = 9;
constexpr UINT kLoopEnd = 15;
constexpr UINT kClearLoop = 10;
constexpr UINT kStepForward = 11;
constexpr UINT kStepBackward = 12;
constexpr UINT kResetVideo = 13;
constexpr UINT kResetAudioDelay = 14;
constexpr UINT kSpeedBase = 100;
constexpr UINT kAspectBase = 120;
constexpr UINT kRotationBase = 130;
constexpr UINT kAdjustBase = 140;  // Two ids per setting: lower, then raise.
constexpr UINT kSubtitleTrackBase = 1000;

constexpr std::array<double, 8> kSpeeds{0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 3.0};
constexpr std::array<AspectMode, 5> kAspects{AspectMode::Original, AspectMode::Ratio16x9, AspectMode::Ratio4x3,
                                             AspectMode::Ratio235x100, AspectMode::Stretch};
constexpr std::array<int, 4> kRotations{0, 90, 180, 270};
constexpr std::array<VideoAdjustment, kVideoAdjustmentCount> kAdjustments{
    VideoAdjustment::Brightness, VideoAdjustment::Contrast, VideoAdjustment::Saturation, VideoAdjustment::Hue, VideoAdjustment::Gamma};
constexpr std::array<ShortcutAction, kVideoAdjustmentCount> kLowerActions{
    ShortcutAction::BrightnessDown, ShortcutAction::ContrastDown, ShortcutAction::SaturationDown, ShortcutAction::HueDown, ShortcutAction::GammaDown};
constexpr std::array<ShortcutAction, kVideoAdjustmentCount> kRaiseActions{
    ShortcutAction::BrightnessUp, ShortcutAction::ContrastUp, ShortcutAction::SaturationUp, ShortcutAction::HueUp, ShortcutAction::GammaUp};
constexpr UINT kAudioTrackBase = 20000;

// '&' marks a shortcut letter in menu text, so a literal one has to be doubled.
std::wstring EscapeForMenu(const std::wstring& text) {
    std::wstring escaped;
    for (const wchar_t c : text) {
        if (c == L'&') {
            escaped += L'&';
        }
        escaped += c;
    }
    return escaped;
}

void AddItem(HMENU menu, UINT id, const std::wstring& text, bool checked = false, bool enabled = true) {
    UINT flags = MF_STRING;
    flags |= checked ? MF_CHECKED : MF_UNCHECKED;
    flags |= enabled ? MF_ENABLED : MF_GRAYED;
    AppendMenuW(menu, flags, id, text.c_str());
}

// Shows the action's current key at the right of the menu item, if it has one.
std::wstring WithKey(const std::wstring& label, const ShortcutTable& shortcuts, ShortcutAction action) {
    const KeyChord chord = shortcuts[static_cast<size_t>(action)];
    return chord == kNoKey ? label : label + L"\t" + KeyChordName(chord);
}

HMENU BuildSubtitleMenu(const PlaybackState& state, bool fileOpen) {
    HMENU menu = CreatePopupMenu();
    bool anySelected = false;
    for (const MediaTrack& track : state.tracks) {
        anySelected = anySelected || (track.type == TrackType::Subtitle && track.selected);
    }
    AddItem(menu, kSubtitleOff, L"자막 끄기", !anySelected, fileOpen);
    for (const MediaTrack& track : state.tracks) {
        if (track.type == TrackType::Subtitle) {
            AddItem(menu, kSubtitleTrackBase + static_cast<UINT>(track.id), EscapeForMenu(TrackLabel(track)), track.selected);
        }
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AddItem(menu, kOpenSubtitle, L"자막 파일 열기...", false, fileOpen);

    wchar_t delay[64];
    swprintf_s(delay, L"싱크 초기화 (현재 %+.1f초)", state.subtitleDelay);
    AddItem(menu, kResetSubtitleDelay, delay, false, state.subtitleDelay != 0.0);

    HMENU encoding = CreatePopupMenu();
    AddItem(encoding, kEncodingKorean, L"한국어 (CP949)", state.subtitleEncoding == SubtitleEncoding::Korean);
    AddItem(encoding, kEncodingAuto, L"자동 감지", state.subtitleEncoding == SubtitleEncoding::Auto);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(encoding), L"자막 인코딩");
    return menu;
}

HMENU BuildAudioMenu(const PlaybackState& state) {
    HMENU menu = CreatePopupMenu();
    for (const MediaTrack& track : state.tracks) {
        if (track.type == TrackType::Audio) {
            AddItem(menu, kAudioTrackBase + static_cast<UINT>(track.id), EscapeForMenu(TrackLabel(track)), track.selected);
        }
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    wchar_t delay[64];
    swprintf_s(delay, L"음성 싱크 초기화 (현재 %+.1f초)", state.audioDelay);
    AddItem(menu, kResetAudioDelay, delay, false, state.audioDelay != 0.0);
    return menu;
}

HMENU BuildPlaybackMenu(const PlaybackState& state, bool fileOpen, const ShortcutTable& shortcuts) {
    HMENU menu = CreatePopupMenu();
    HMENU speed = CreatePopupMenu();
    for (size_t i = 0; i < kSpeeds.size(); ++i) {
        wchar_t label[32];
        swprintf_s(label, L"%g배", kSpeeds[i]);
        AddItem(speed, kSpeedBase + static_cast<UINT>(i), label, state.speed == kSpeeds[i]);
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(speed), L"배속");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AddItem(menu, kLoopStart, WithKey(L"구간 반복 시작 지정", shortcuts, ShortcutAction::LoopStart), false, fileOpen);
    AddItem(menu, kLoopEnd, WithKey(L"구간 반복 끝 지정", shortcuts, ShortcutAction::LoopEnd), false, fileOpen);
    AddItem(menu, kClearLoop, WithKey(L"구간 반복 해제", shortcuts, ShortcutAction::LoopClear), false,
            state.loopStart.has_value() || state.loopEnd.has_value());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AddItem(menu, kStepForward, WithKey(L"다음 프레임", shortcuts, ShortcutAction::FrameForward), false, fileOpen);
    AddItem(menu, kStepBackward, WithKey(L"이전 프레임", shortcuts, ShortcutAction::FrameBackward), false, fileOpen);
    return menu;
}

HMENU BuildVideoMenu(const PlaybackState& state, const ShortcutTable& shortcuts) {
    HMENU menu = CreatePopupMenu();
    HMENU aspect = CreatePopupMenu();
    for (size_t i = 0; i < kAspects.size(); ++i) {
        AddItem(aspect, kAspectBase + static_cast<UINT>(i), AspectLabel(kAspects[i]), state.aspect == kAspects[i]);
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(aspect), L"화면 비율");

    HMENU rotation = CreatePopupMenu();
    for (size_t i = 0; i < kRotations.size(); ++i) {
        const std::wstring label = kRotations[i] == 0 ? L"회전 안 함" : std::to_wstring(kRotations[i]) + L"도";
        AddItem(rotation, kRotationBase + static_cast<UINT>(i), label, state.rotation == kRotations[i]);
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(rotation), L"회전");

    HMENU adjust = CreatePopupMenu();
    bool anyAdjusted = false;
    for (size_t i = 0; i < kAdjustments.size(); ++i) {
        const int value = state.adjustments[i];
        anyAdjusted = anyAdjusted || value != 0;
        wchar_t lower[64];
        wchar_t raise[64];
        swprintf_s(lower, L"%s 낮추기 (현재 %+d)", VideoAdjustmentLabel(kAdjustments[i]), value);
        swprintf_s(raise, L"%s 높이기 (현재 %+d)", VideoAdjustmentLabel(kAdjustments[i]), value);
        AddItem(adjust, kAdjustBase + static_cast<UINT>(i) * 2, WithKey(lower, shortcuts, kLowerActions[i]));
        AddItem(adjust, kAdjustBase + static_cast<UINT>(i) * 2 + 1, WithKey(raise, shortcuts, kRaiseActions[i]));
    }
    AppendMenuW(adjust, MF_SEPARATOR, 0, nullptr);
    AddItem(adjust, kResetVideo, WithKey(L"모두 초기화", shortcuts, ShortcutAction::VideoReset), false, anyAdjusted);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(adjust), L"영상 조정");
    return menu;
}

MenuChoice ChoiceFromCommand(UINT command) {
    MenuChoice choice;
    if (command >= kAudioTrackBase) {
        choice.action = MenuChoice::Action::SelectAudio;
        choice.trackId = command - kAudioTrackBase;
    } else if (command >= kSubtitleTrackBase) {
        choice.action = MenuChoice::Action::SelectSubtitle;
        choice.trackId = command - kSubtitleTrackBase;
    } else if (command >= kAdjustBase && command < kAdjustBase + kAdjustments.size() * 2) {
        const UINT index = command - kAdjustBase;
        choice.action = MenuChoice::Action::AdjustVideo;
        choice.adjustment = kAdjustments[index / 2];
        choice.adjustmentDirection = index % 2 == 0 ? -1 : 1;
    } else if (command >= kRotationBase && command < kRotationBase + kRotations.size()) {
        choice.action = MenuChoice::Action::SetRotation;
        choice.rotation = kRotations[command - kRotationBase];
    } else if (command >= kAspectBase && command < kAspectBase + kAspects.size()) {
        choice.action = MenuChoice::Action::SetAspect;
        choice.aspect = kAspects[command - kAspectBase];
    } else if (command >= kSpeedBase && command < kSpeedBase + kSpeeds.size()) {
        choice.action = MenuChoice::Action::SetSpeed;
        choice.speed = kSpeeds[command - kSpeedBase];
    } else if (command == kLoopStart) {
        choice.action = MenuChoice::Action::LoopStart;
    } else if (command == kLoopEnd) {
        choice.action = MenuChoice::Action::LoopEnd;
    } else if (command == kClearLoop) {
        choice.action = MenuChoice::Action::ClearLoop;
    } else if (command == kStepForward) {
        choice.action = MenuChoice::Action::StepForward;
    } else if (command == kStepBackward) {
        choice.action = MenuChoice::Action::StepBackward;
    } else if (command == kResetVideo) {
        choice.action = MenuChoice::Action::ResetVideo;
    } else if (command == kResetAudioDelay) {
        choice.action = MenuChoice::Action::ResetAudioDelay;
    } else if (command == kOpenVideo) {
        choice.action = MenuChoice::Action::OpenVideo;
    } else if (command == kOpenSubtitle) {
        choice.action = MenuChoice::Action::OpenSubtitle;
    } else if (command == kSubtitleOff) {
        choice.action = MenuChoice::Action::SelectSubtitle;
    } else if (command == kResetSubtitleDelay) {
        choice.action = MenuChoice::Action::ResetSubtitleDelay;
    } else if (command == kEncodingKorean || command == kEncodingAuto) {
        choice.action = MenuChoice::Action::SetSubtitleEncoding;
        choice.encoding = command == kEncodingKorean ? SubtitleEncoding::Korean : SubtitleEncoding::Auto;
    } else if (command == kOpenSettings) {
        choice.action = MenuChoice::Action::OpenSettings;
    }
    return choice;
}

}  // namespace

std::optional<size_t> ShowChapterMenu(HWND owner, POINT screenCenter, const PlaybackState& state) {
    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < state.chapters.size(); ++i) {
        const Chapter& chapter = state.chapters[i];
        const bool current = state.chapter && *state.chapter == static_cast<int64_t>(i);
        // Item ids start at 1, because 0 means nothing was picked.
        AddItem(menu, static_cast<UINT>(i) + 1, EscapeForMenu(chapter.title) + L"\t" + FormatTime(chapter.time), current);
    }
    SetForegroundWindow(owner);
    const UINT command = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_CENTERALIGN | TPM_VCENTERALIGN, screenCenter.x,
                                                           screenCenter.y, 0, owner, nullptr));
    DestroyMenu(menu);
    return command == 0 ? std::nullopt : std::optional<size_t>(command - 1);
}

const wchar_t* AspectLabel(AspectMode aspect) {
    switch (aspect) {
    case AspectMode::Ratio16x9:
        return L"16:9";
    case AspectMode::Ratio4x3:
        return L"4:3";
    case AspectMode::Ratio235x100:
        return L"2.35:1";
    case AspectMode::Stretch:
        return L"창에 맞춰 늘리기";
    case AspectMode::Original:
        break;
    }
    return L"원본 비율";
}

const wchar_t* VideoAdjustmentLabel(VideoAdjustment adjustment) {
    switch (adjustment) {
    case VideoAdjustment::Contrast:
        return L"대비";
    case VideoAdjustment::Saturation:
        return L"채도";
    case VideoAdjustment::Hue:
        return L"색조";
    case VideoAdjustment::Gamma:
        return L"감마";
    case VideoAdjustment::Brightness:
        break;
    }
    return L"밝기";
}

std::wstring TrackLabel(const MediaTrack& track) {
    std::wstring label;
    if (!track.externalFileName.empty()) {
        label = track.externalFileName + L" (외부)";
    } else {
        label = !track.title.empty() ? track.title : !track.language.empty() ? track.language : L"트랙 " + std::to_wstring(track.id);
        if (!track.title.empty() && !track.language.empty()) {
            label += L" [" + track.language + L"]";
        }
        if (!track.codec.empty()) {
            label += L" (" + track.codec + L")";
        }
    }
    if (track.channels > 0) {
        label += L" " + std::to_wstring(track.channels) + L"ch";
    }
    return label;
}

MenuChoice ShowContextMenu(HWND owner, POINT screenPoint, const PlaybackState& state, const ShortcutTable& shortcuts) {
    const bool fileOpen = !state.fileName.empty();
    HMENU menu = CreatePopupMenu();
    AddItem(menu, kOpenVideo, L"파일 열기...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildSubtitleMenu(state, fileOpen)), L"자막");
    const bool hasAudio = fileOpen && [&] {
        for (const MediaTrack& track : state.tracks) {
            if (track.type == TrackType::Audio) {
                return true;
            }
        }
        return false;
    }();
    AppendMenuW(menu, MF_POPUP | (hasAudio ? MF_ENABLED : MF_GRAYED), reinterpret_cast<UINT_PTR>(BuildAudioMenu(state)), L"오디오");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildPlaybackMenu(state, fileOpen, shortcuts)), L"재생");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildVideoMenu(state, shortcuts)), L"영상");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AddItem(menu, kOpenSettings, WithKey(L"설정...", shortcuts, ShortcutAction::OpenSettings));

    // A popup menu only closes on an outside click if its owner is in the foreground.
    SetForegroundWindow(owner);
    const UINT command = static_cast<UINT>(
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, 0, owner, nullptr));
    DestroyMenu(menu);  // Also destroys the submenus.
    return ChoiceFromCommand(command);
}
