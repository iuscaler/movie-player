#include "SmiSubtitles.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace {

constexpr uintmax_t kMaxSmiFileSize = 64 * 1024 * 1024;
constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

// In the encodings SMI files use (UTF-8, CP949, Shift-JIS), the trailing bytes of a
// multi-byte character never equal '<' or '>', so tags can be found byte by byte.
char ToLowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

size_t FindNoCase(std::string_view text, std::string_view lowerNeedle, size_t from = 0) {
    if (lowerNeedle.size() > text.size()) {
        return std::string_view::npos;
    }
    for (size_t i = from; i + lowerNeedle.size() <= text.size(); ++i) {
        size_t matched = 0;
        while (matched < lowerNeedle.size() && ToLowerAscii(text[i + matched]) == lowerNeedle[matched]) {
            ++matched;
        }
        if (matched == lowerNeedle.size()) {
            return i;
        }
    }
    return std::string_view::npos;
}

// Finds "<name" as a whole tag name, so that looking for <p doesn't match <pre.
size_t FindTag(std::string_view text, std::string_view lowerOpening, size_t from = 0) {
    while (true) {
        const size_t found = FindNoCase(text, lowerOpening, from);
        if (found == std::string_view::npos) {
            return found;
        }
        const size_t after = found + lowerOpening.size();
        if (after >= text.size() || text[after] == '>' || std::isspace(static_cast<unsigned char>(text[after]))) {
            return found;
        }
        from = after;
    }
}

bool IsNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

// Reads the value of class=... from a <P ...> tag, in upper case. Empty if there is none.
std::string ClassOf(std::string_view tag) {
    size_t at = FindNoCase(tag, "class");
    if (at == std::string_view::npos) {
        return {};
    }
    at += 5;
    while (at < tag.size() && std::isspace(static_cast<unsigned char>(tag[at]))) {
        ++at;
    }
    if (at >= tag.size() || tag[at] != '=') {
        return {};
    }
    ++at;
    while (at < tag.size() && (std::isspace(static_cast<unsigned char>(tag[at])) || tag[at] == '"' || tag[at] == '\'')) {
        ++at;
    }
    std::string name;
    while (at < tag.size() && IsNameChar(tag[at])) {
        name += static_cast<char>(std::toupper(static_cast<unsigned char>(tag[at])));
        ++at;
    }
    return name;
}

// True if anything would be visible: not only tags, spaces and &nbsp;.
bool HasVisibleText(std::string_view markup) {
    size_t i = 0;
    while (i < markup.size()) {
        const char c = markup[i];
        if (c == '<') {
            const size_t end = markup.find('>', i);
            if (end == std::string_view::npos) {
                return false;
            }
            i = end + 1;
        } else if (FindNoCase(markup.substr(i, 6), "&nbsp;") == 0) {
            i += 6;
        } else if (static_cast<unsigned char>(c) <= 0x20) {
            ++i;
        } else if (markup.substr(i, 2) == "\xC2\xA0") {  // A UTF-8 no-break space.
            i += 2;
        } else {
            return true;
        }
    }
    return false;
}

struct Paragraph {
    std::string className;  // Empty for a paragraph without a class, which every language shares.
    std::string_view markup;
};

struct SyncBlock {
    std::string_view tag;
    std::vector<Paragraph> paragraphs;
};

std::vector<Paragraph> ParseParagraphs(std::string_view body) {
    std::vector<Paragraph> paragraphs;
    size_t at = FindTag(body, "<p");
    if (at == std::string_view::npos) {
        if (HasVisibleText(body)) {
            paragraphs.push_back({std::string(), body});
        }
        return paragraphs;
    }
    if (at > 0 && HasVisibleText(body.substr(0, at))) {
        paragraphs.push_back({std::string(), body.substr(0, at)});
    }
    while (at != std::string_view::npos) {
        const size_t next = FindTag(body, "<p", at + 2);
        const std::string_view markup = body.substr(at, next == std::string_view::npos ? std::string_view::npos : next - at);
        const size_t tagEnd = markup.find('>');
        const std::string_view tag = markup.substr(0, tagEnd == std::string_view::npos ? markup.size() : tagEnd);
        paragraphs.push_back({ClassOf(tag), markup});
        at = next;
    }
    return paragraphs;
}

// Works out a language tag from the class's style, like .KRCC { lang:ko-KR; }, or
// failing that from well-known class names.
std::string LanguageTagFor(const std::string& className, std::string_view header) {
    static const std::map<std::string, std::string> aliases = {
        {"kr", "ko"}, {"jp", "ja"}, {"cn", "zh"}, {"krcc", "ko"}, {"kocc", "ko"}, {"encc", "en"},
        {"egcc", "en"}, {"enuscc", "en"}, {"jpcc", "ja"}, {"jacc", "ja"}, {"cncc", "zh"}, {"zhcc", "zh"}, {"twcc", "zh"},
    };
    const auto normalize = [](std::string tag) {
        const auto alias = aliases.find(tag);
        return alias == aliases.end() ? tag : alias->second;
    };

    std::string lowerClass;
    std::transform(className.begin(), className.end(), std::back_inserter(lowerClass), ToLowerAscii);
    const size_t style = FindNoCase(header, "." + lowerClass);
    if (style != std::string_view::npos) {
        const size_t open = header.find('{', style);
        const size_t close = open == std::string_view::npos ? open : header.find('}', open);
        if (close != std::string_view::npos) {
            const std::string_view rules = header.substr(open, close - open);
            size_t lang = FindNoCase(rules, "lang");
            if (lang != std::string_view::npos && (lang = rules.find(':', lang)) != std::string_view::npos) {
                ++lang;
                while (lang < rules.size() && std::isspace(static_cast<unsigned char>(rules[lang]))) {
                    ++lang;
                }
                std::string code;
                while (lang < rules.size() && std::isalpha(static_cast<unsigned char>(rules[lang]))) {
                    code += ToLowerAscii(rules[lang]);
                    ++lang;
                }
                if (code.size() == 2 || code.size() == 3) {
                    return normalize(code);
                }
            }
        }
    }
    return normalize(lowerClass);
}

std::string ReadFile(const fs::path& path) {
    std::error_code error;
    const uintmax_t size = fs::file_size(path, error);
    if (error || size > kMaxSmiFileSize) {
        return {};
    }
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

// The byte-level parser needs an ASCII-compatible encoding, which UTF-16 isn't.
std::string ConvertUtf16ToUtf8IfNeeded(std::string content) {
    const bool littleEndian = content.size() >= 2 && content[0] == '\xFF' && content[1] == '\xFE';
    const bool bigEndian = content.size() >= 2 && content[0] == '\xFE' && content[1] == '\xFF';
    if (!littleEndian && !bigEndian) {
        return content;
    }
    std::wstring wide((content.size() - 2) / 2, L'\0');
    for (size_t i = 0; i < wide.size(); ++i) {
        const auto first = static_cast<unsigned char>(content[2 + i * 2]);
        const auto second = static_cast<unsigned char>(content[3 + i * 2]);
        wide[i] = static_cast<wchar_t>(littleEndian ? (second << 8 | first) : (first << 8 | second));
    }
    const int length = static_cast<int>(wide.size());
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), length, nullptr, 0, nullptr, nullptr);
    std::string utf8(kUtf8Bom);
    utf8.resize(kUtf8Bom.size() + static_cast<size_t>(size));
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), length, utf8.data() + kUtf8Bom.size(), size, nullptr, nullptr);
    return utf8;
}

}  // namespace

std::vector<SmiLanguage> SplitSmiByLanguage(std::string_view content) {
    const size_t firstSync = FindTag(content, "<sync");
    if (firstSync == std::string_view::npos) {
        return {};
    }
    const std::string_view header = content.substr(0, firstSync);
    size_t bodyEnd = FindNoCase(content, "</body", firstSync);
    if (bodyEnd == std::string_view::npos) {
        bodyEnd = content.size();
    }
    const std::string_view footer = content.substr(bodyEnd);

    // Parse every timed block, and note the classes in order of first appearance.
    std::vector<SyncBlock> blocks;
    std::vector<std::string> classes;
    std::set<std::string> classesWithText;
    size_t at = firstSync;
    while (at != std::string_view::npos && at < bodyEnd) {
        size_t next = FindTag(content, "<sync", at + 5);
        if (next == std::string_view::npos || next > bodyEnd) {
            next = bodyEnd;
        }
        const size_t tagEnd = content.find('>', at);
        if (tagEnd == std::string_view::npos || tagEnd >= next) {
            at = next == bodyEnd ? std::string_view::npos : next;
            continue;
        }
        SyncBlock block;
        block.tag = content.substr(at, tagEnd + 1 - at);
        block.paragraphs = ParseParagraphs(content.substr(tagEnd + 1, next - tagEnd - 1));
        for (const Paragraph& paragraph : block.paragraphs) {
            if (paragraph.className.empty()) {
                continue;
            }
            if (std::find(classes.begin(), classes.end(), paragraph.className) == classes.end()) {
                classes.push_back(paragraph.className);
            }
            if (HasVisibleText(paragraph.markup)) {
                classesWithText.insert(paragraph.className);
            }
        }
        blocks.push_back(std::move(block));
        at = next == bodyEnd ? std::string_view::npos : next;
    }

    // Two classes need separating even if one is blank: its blank lines would
    // overwrite the other's text just the same.
    if (classes.size() < 2) {
        return {};
    }

    std::vector<SmiLanguage> languages;
    std::set<std::string> usedTags;
    for (const std::string& className : classes) {
        if (classesWithText.count(className) == 0) {
            continue;
        }
        SmiLanguage language;
        language.tag = LanguageTagFor(className, header);
        if (!usedTags.insert(language.tag).second) {
            std::string lowerClass;
            std::transform(className.begin(), className.end(), std::back_inserter(lowerClass), ToLowerAscii);
            language.tag += "-" + lowerClass;
            usedTags.insert(language.tag);
        }
        language.content.assign(header);
        for (const SyncBlock& block : blocks) {
            bool opened = false;
            for (const Paragraph& paragraph : block.paragraphs) {
                if (paragraph.className == className || paragraph.className.empty()) {
                    if (!opened) {
                        language.content.append(block.tag);
                        opened = true;
                    }
                    language.content.append(paragraph.markup);
                }
            }
        }
        language.content.append(footer);
        languages.push_back(std::move(language));
    }
    std::stable_partition(languages.begin(), languages.end(), [](const SmiLanguage& language) { return language.tag == "ko"; });
    return languages;
}

bool IsSmiFile(const fs::path& path) {
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return extension == L".smi" || extension == L".sami";
}

std::vector<fs::path> PrepareSmiFile(const fs::path& smi, const fs::path& outputDirectory) {
    const std::string content = ConvertUtf16ToUtf8IfNeeded(ReadFile(smi));
    const std::vector<SmiLanguage> languages = SplitSmiByLanguage(content);
    if (languages.empty()) {
        return {smi};
    }

    std::error_code error;
    fs::create_directories(outputDirectory, error);
    std::vector<fs::path> files;
    for (const SmiLanguage& language : languages) {
        fs::path part = outputDirectory / smi.stem();
        part += L"." + std::wstring(language.tag.begin(), language.tag.end()) + L".smi";
        std::ofstream file(part, std::ios::binary);
        file.write(language.content.data(), static_cast<std::streamsize>(language.content.size()));
        file.close();
        if (!file) {
            return {smi};
        }
        files.push_back(std::move(part));
    }
    return files;
}

std::vector<fs::path> PrepareSmiFilesFor(const std::wstring& video, const fs::path& outputDirectory) {
    if (video.find(L"://") != std::wstring::npos) {
        return {};
    }
    const auto toLower = [](std::wstring text) {
        std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return text;
    };

    const fs::path videoPath(video);
    const std::wstring stem = toLower(videoPath.stem().wstring());
    std::vector<fs::path> matches;
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(videoPath.parent_path(), error)) {
        if (IsSmiFile(entry.path()) && toLower(entry.path().stem().wstring()).find(stem) != std::wstring::npos) {
            matches.push_back(entry.path());
        }
    }
    // The file with exactly the video's name comes first, then the rest in a stable order.
    std::sort(matches.begin(), matches.end(), [&](const fs::path& a, const fs::path& b) {
        const bool aExact = toLower(a.stem().wstring()) == stem;
        const bool bExact = toLower(b.stem().wstring()) == stem;
        return aExact != bExact ? aExact : a < b;
    });

    std::vector<fs::path> files;
    for (const fs::path& smi : matches) {
        const std::vector<fs::path> prepared = PrepareSmiFile(smi, outputDirectory);
        files.insert(files.end(), prepared.begin(), prepared.end());
    }
    return files;
}
