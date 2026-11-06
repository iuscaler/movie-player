#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// A SMI file can hold several languages, told apart by a class on each paragraph.
// mpv would only show whichever comes last at each moment, so a file like that is
// split into one temporary file per language and each becomes its own track.

struct SmiLanguage {
    std::string tag;      // A language code like "ko" if known, otherwise from the class name.
    std::string content;  // A complete SMI file holding only this language.
};

// Returns one entry per language, Korean first. Returns nothing if the file uses
// fewer than two classes and so can be played as it is. Works on raw bytes, so
// the text keeps its original encoding and mpv's encoding setting still applies.
std::vector<SmiLanguage> SplitSmiByLanguage(std::string_view content);

bool IsSmiFile(const std::filesystem::path& path);

// The files to load in place of this SMI file, preferred language first. That is
// the file itself unless it needs splitting, in which case the parts are written
// to outputDirectory. Falls back to the file itself if it can't be read or split.
std::vector<std::filesystem::path> PrepareSmiFile(const std::filesystem::path& smi,
                                                  const std::filesystem::path& outputDirectory);

// Does the same for every SMI file next to a video whose name contains the video's name.
std::vector<std::filesystem::path> PrepareSmiFilesFor(const std::wstring& video,
                                                      const std::filesystem::path& outputDirectory);
