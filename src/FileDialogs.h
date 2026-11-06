#pragma once

#include <windows.h>

#include <string>

// These return an empty string if the user cancels.
std::wstring PickVideoFile(HWND owner);
std::wstring PickSubtitleFile(HWND owner);

// Decides by file extension, which is what tells a dropped subtitle from a video.
bool IsSubtitleFile(const std::wstring& path);
