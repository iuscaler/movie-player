#include "Settings.h"

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace {

constexpr wchar_t kPlayerSection[] = L"Player";
constexpr wchar_t kWindowSection[] = L"Window";
constexpr wchar_t kVideoSection[] = L"Video";
constexpr wchar_t kSubtitleSection[] = L"Subtitle";
constexpr wchar_t kShortcutSection[] = L"Shortcuts";
constexpr std::array<const wchar_t*, kVideoAdjustmentCount> kAdjustmentKeys{L"Brightness", L"Contrast", L"Saturation", L"Hue", L"Gamma"};
constexpr int kMaxAdjustment = 100;
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

// The profile functions write ANSI unless the file already starts with a UTF-16 mark,
// and a font's name may not exist in the ANSI code page.
void EnsureUnicodeFile(const std::wstring& path) {
    constexpr char kMark[] = "\xFF\xFE";
    std::string bytes;
    {
        std::ifstream in(std::filesystem::path(path), std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (bytes.compare(0, 2, kMark) == 0) {
        return;
    }
    std::wstring text(bytes.size(), L'\0');
    if (!bytes.empty()) {
        text.resize(static_cast<size_t>(MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), static_cast<int>(text.size()))));
    }
    std::ofstream out(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    out.write(kMark, 2);
    out.write(reinterpret_cast<const char*>(text.data()), static_cast<std::streamsize>(text.size() * sizeof(wchar_t)));
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

    for (size_t i = 0; i < kAdjustmentKeys.size(); ++i) {
        const int value = ReadInt(path, kVideoSection, kAdjustmentKeys[i]);
        if (value >= -kMaxAdjustment && value <= kMaxAdjustment) {
            settings.videoAdjustments[i] = value;
        }
    }

    Preferences& preferences = settings.preferences;
    const int seekSeconds = ReadInt(path, kPlayerSection, L"SeekSeconds");
    if (seekSeconds >= kMinSeekSeconds && seekSeconds <= kMaxSeekSeconds) {
        preferences.seekSeconds = seekSeconds;
    }
    preferences.subtitleStyle.font = ReadText(path, kSubtitleSection, L"Font");
    const int fontSize = ReadInt(path, kSubtitleSection, L"FontSize");
    if (fontSize >= kMinSubtitleFontSize && fontSize <= kMaxSubtitleFontSize) {
        preferences.subtitleStyle.fontSize = fontSize;
    }
    std::array<bool, kShortcutCount> stored{};
    const int heightPercent = ReadInt(path, kSubtitleSection, L"HeightPercent");
    if (heightPercent >= 0 && heightPercent <= kMaxSubtitleHeightPercent) {
        preferences.subtitleStyle.heightPercent = heightPercent;
    }
    const std::wstring outlineText = ReadText(path, kSubtitleSection, L"OutlineSize");
    wchar_t* outlineEnd = nullptr;
    const double outlineSize = std::wcstod(outlineText.c_str(), &outlineEnd);
    if (!outlineText.empty() && *outlineEnd == L'\0' &&
        std::find(kSubtitleOutlineSizes.begin(), kSubtitleOutlineSizes.end(), outlineSize) != kSubtitleOutlineSizes.end()) {
        preferences.subtitleStyle.outlineSize = outlineSize;
    }
    for (size_t i = 0; i < kShortcutCount; ++i) {
        const int chord = ReadInt(path, kShortcutSection, AllShortcuts()[i].settingsName);
        if (chord == kNoKey || (chord != kMissing && CanBeShortcut(chord))) {
            preferences.shortcuts[i] = chord;
            stored[i] = true;
        }
    }
    // A default added in a newer version gives way to a key the user already assigned.
    for (size_t i = 0; i < kShortcutCount; ++i) {
        for (size_t other = 0; !stored[i] && other < kShortcutCount; ++other) {
            if (stored[other] && preferences.shortcuts[other] == preferences.shortcuts[i]) {
                preferences.shortcuts[i] = kNoKey;
            }
        }
    }
    return settings;
}

void SaveSettings(const Settings& settings) {
    const std::wstring path = SettingsPath();
    if (path.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    EnsureUnicodeFile(path);

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
    for (size_t i = 0; i < kAdjustmentKeys.size(); ++i) {
        WriteInt(path, kVideoSection, kAdjustmentKeys[i], settings.videoAdjustments[i]);
    }

    const Preferences& preferences = settings.preferences;
    WriteInt(path, kPlayerSection, L"SeekSeconds", preferences.seekSeconds);
    WriteText(path, kSubtitleSection, L"Font", preferences.subtitleStyle.font);
    WriteInt(path, kSubtitleSection, L"FontSize", preferences.subtitleStyle.fontSize);
    wchar_t outlineSize[32];
    swprintf_s(outlineSize, L"%g", preferences.subtitleStyle.outlineSize);
    WriteText(path, kSubtitleSection, L"OutlineSize", outlineSize);
    WriteInt(path, kSubtitleSection, L"HeightPercent", preferences.subtitleStyle.heightPercent);
    // Only changed keys are stored, so a future change to a default reaches everyone else.
    for (size_t i = 0; i < kShortcutCount; ++i) {
        const ShortcutInfo& info = AllShortcuts()[i];
        if (preferences.shortcuts[i] == info.defaultKey) {
            WritePrivateProfileStringW(kShortcutSection, info.settingsName, nullptr, path.c_str());
        } else {
            WriteInt(path, kShortcutSection, info.settingsName, preferences.shortcuts[i]);
        }
    }
}
