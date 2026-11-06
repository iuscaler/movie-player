#pragma once

#include <cmath>
#include <cwchar>
#include <string>

// Formats a playback time as "MM:SS", or "H:MM:SS" from one hour up.
inline std::wstring FormatTime(double seconds) {
    const long long total = static_cast<long long>(std::floor(seconds < 0 ? 0 : seconds));
    wchar_t text[32];
    if (total >= 3600) {
        swprintf_s(text, L"%lld:%02lld:%02lld", total / 3600, total / 60 % 60, total % 60);
    } else {
        swprintf_s(text, L"%02lld:%02lld", total / 60, total % 60);
    }
    return text;
}
