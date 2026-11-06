#include "FileDialogs.h"

#include <commdlg.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <vector>

namespace {

std::wstring PickFile(HWND owner, const wchar_t* filter, const wchar_t* title) {
    std::vector<wchar_t> buffer(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrTitle = title;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    return GetOpenFileNameW(&dialog) ? std::wstring(buffer.data()) : std::wstring();
}

}  // namespace

std::wstring PickVideoFile(HWND owner) {
    return PickFile(owner,
                    L"동영상 파일\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.webm;*.ts;*.m4v\0"
                    L"모든 파일\0*.*\0",
                    L"재생할 동영상 선택");
}

std::wstring PickSubtitleFile(HWND owner) {
    return PickFile(owner,
                    L"자막 파일\0*.smi;*.sami;*.srt;*.ass;*.ssa;*.vtt;*.sub\0"
                    L"모든 파일\0*.*\0",
                    L"자막 파일 선택");
}

bool IsSubtitleFile(const std::wstring& path) {
    constexpr std::array<const wchar_t*, 7> extensions{L".smi", L".sami", L".srt", L".ass", L".ssa", L".vtt", L".sub"};
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) {
        return false;
    }
    std::wstring extension = path.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}
