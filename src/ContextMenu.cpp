#include "ContextMenu.h"

#include <cwchar>

namespace {

// Track ids are added to these bases to form menu command ids.
constexpr UINT kOpenVideo = 1;
constexpr UINT kOpenSubtitle = 2;
constexpr UINT kSubtitleOff = 3;
constexpr UINT kResetSubtitleDelay = 4;
constexpr UINT kEncodingKorean = 5;
constexpr UINT kEncodingAuto = 6;
constexpr UINT kSubtitleTrackBase = 1000;
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
    }
    return choice;
}

}  // namespace

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

MenuChoice ShowContextMenu(HWND owner, POINT screenPoint, const PlaybackState& state) {
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

    // A popup menu only closes on an outside click if its owner is in the foreground.
    SetForegroundWindow(owner);
    const UINT command = static_cast<UINT>(
        TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, 0, owner, nullptr));
    DestroyMenu(menu);  // Also destroys the submenus.
    return ChoiceFromCommand(command);
}
