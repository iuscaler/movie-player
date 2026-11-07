#pragma once

#include <windows.h>

#include <array>
#include <string>

// The file types the app offers to open, here and in Windows' "Open with" list.
inline constexpr std::array<const wchar_t*, 17> kVideoExtensions{
    L".mp4", L".mkv", L".avi", L".mov", L".wmv", L".webm", L".ts",  L".m2ts", L".mts",
    L".m4v", L".flv", L".mpg", L".mpeg", L".3gp", L".ogv", L".vob", L".asf",
};

inline constexpr std::array<const wchar_t*, 7> kSubtitleExtensions{L".smi", L".sami", L".srt", L".ass", L".ssa", L".vtt", L".sub"};

// These return an empty string if the user cancels.
std::wstring PickVideoFile(HWND owner);
std::wstring PickSubtitleFile(HWND owner);

// Decides by file extension, which is what tells a dropped subtitle from a video.
bool IsSubtitleFile(const std::wstring& path);

// The video in the same folder that this subtitle belongs to, or an empty string.
// "movie.srt" and "movie.ko.srt" both belong to "movie.mkv". An exact match comes first.
std::wstring FindVideoForSubtitle(const std::wstring& subtitle);
