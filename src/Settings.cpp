#include "Settings.h"

#include <cwchar>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

constexpr wchar_t kPlayerSection[] = L"Player";
constexpr wchar_t kWindowSection[] = L"Window";
constexpr double kMaxVolume = 130.0;  // mpv's default volume-max.
constexpr LONG kMinWindowSize = 100;
constexpr int kMissing = INT_MIN;

std::wstring SettingsPath() {
    wchar_t appData[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::wstring(appData) + L"\\MoviePlayer\\settings.ini";
}

std::wstring ReadText(const std::wstring& path, const wchar_t* section, const wchar_t* key) {
    wchar_t buffer[64];
    GetPrivateProfileStringW(section, key, L"", buffer, 64, path.c_str());
    return buffer;
}

// Reads a whole-number value, rejecting text that only starts like a number.
int ReadInt(const std::wstring& path, const wchar_t* section, const wchar_t* key) {
    const std::wstring text = ReadText(path, section, key);
    wchar_t* end = nullptr;
    const long value = std::wcstol(text.c_str(), &end, 10);
    return (text.empty() || *end != L'\0') ? kMissing : static_cast<int>(value);
}

void WriteText(const std::wstring& path, const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(section, key, value.c_str(), path.c_str());
}

void WriteInt(const std::wstring& path, const wchar_t* section, const wchar_t* key, long value) {
    WriteText(path, section, key, std::to_wstring(value));
}

}  // namespace

Settings LoadSettings() {
    Settings settings;
    const std::wstring path = SettingsPath();
    if (path.empty()) {
        return settings;
    }

    const std::wstring volumeText = ReadText(path, kPlayerSection, L"Volume");
    wchar_t* end = nullptr;
    const double volume = std::wcstod(volumeText.c_str(), &end);
    if (!volumeText.empty() && *end == L'\0' && volume >= 0.0 && volume <= kMaxVolume) {
        settings.volume = volume;
    }

    if (ReadText(path, kPlayerSection, L"SubtitleEncoding") == L"auto") {
        settings.subtitleEncoding = SubtitleEncoding::Auto;
    }

    const int left = ReadInt(path, kWindowSection, L"Left");
    const int top = ReadInt(path, kWindowSection, L"Top");
    const int right = ReadInt(path, kWindowSection, L"Right");
    const int bottom = ReadInt(path, kWindowSection, L"Bottom");
    if (left != kMissing && top != kMissing && right != kMissing && bottom != kMissing &&
        right - left >= kMinWindowSize && bottom - top >= kMinWindowSize) {
        settings.windowPosition = RECT{left, top, right, bottom};
    }
    settings.windowMaximized = ReadInt(path, kWindowSection, L"Maximized") == 1;
    return settings;
}

void SaveSettings(const Settings& settings) {
    const std::wstring path = SettingsPath();
    if (path.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);

    wchar_t volume[32];
    swprintf_s(volume, L"%.0f", settings.volume);
    WriteText(path, kPlayerSection, L"Volume", volume);
    WriteText(path, kPlayerSection, L"SubtitleEncoding", settings.subtitleEncoding == SubtitleEncoding::Auto ? L"auto" : L"korean");
    if (settings.windowPosition) {
        WriteInt(path, kWindowSection, L"Left", settings.windowPosition->left);
        WriteInt(path, kWindowSection, L"Top", settings.windowPosition->top);
        WriteInt(path, kWindowSection, L"Right", settings.windowPosition->right);
        WriteInt(path, kWindowSection, L"Bottom", settings.windowPosition->bottom);
    }
    WriteInt(path, kWindowSection, L"Maximized", settings.windowMaximized ? 1 : 0);
}
