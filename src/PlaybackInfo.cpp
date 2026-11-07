#include "PlaybackInfo.h"

#include <dxgi1_6.h>
#include <psapi.h>
#include <wrl/client.h>

#include <cmath>
#include <cwchar>
#include <optional>
#include <tuple>

#include "ContextMenu.h"
#include "TimeFormat.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr double kSdrReferenceNits = 203.0;  // mpv reports HDR peaks relative to this.

// Collects rows under one heading, leaving out anything with no value.
class Rows {
public:
    explicit Rows(std::vector<InfoRow>& rows) : rows_(rows) {}
    void Group(const wchar_t* group) { group_ = group; }
    void Add(const std::wstring& label, const std::wstring& value) {
        if (!value.empty()) {
            rows_.push_back({group_, label, value});
        }
    }

private:
    std::vector<InfoRow>& rows_;
    std::wstring group_;
};

std::wstring Number(double value, int decimals) {
    wchar_t text[64];
    swprintf_s(text, L"%.*f", decimals, value);
    return text;
}

std::wstring Whole(double value) {
    return std::to_wstring(std::llround(value));
}

std::wstring Bitrate(double bitsPerSecond) {
    if (bitsPerSecond >= 1'000'000.0) {
        return Number(bitsPerSecond / 1'000'000.0, 2) + L" Mbps";
    }
    return Whole(bitsPerSecond / 1000.0) + L" kbps";
}

std::wstring Bitrate(const std::optional<double>& bitsPerSecond) {
    return bitsPerSecond && *bitsPerSecond > 0.0 ? Bitrate(*bitsPerSecond) : L"";
}

std::wstring Bytes(double bytes) {
    constexpr double kKilo = 1024.0;
    if (bytes >= kKilo * kKilo * kKilo) {
        return Number(bytes / (kKilo * kKilo * kKilo), 2) + L" GB";
    }
    if (bytes >= kKilo * kKilo) {
        return Number(bytes / (kKilo * kKilo), 1) + L" MB";
    }
    return Whole(bytes / kKilo) + L" KB";
}

std::wstring Size(const std::optional<double>& width, const std::optional<double>& height) {
    return width && height && *width > 0.0 ? Whole(*width) + L" x " + Whole(*height) : L"";
}

std::wstring Fps(const std::optional<double>& fps) {
    return fps && *fps > 0.0 ? Number(*fps, 3) + L" fps" : L"";
}

std::wstring Seconds(double seconds, int decimals) {
    wchar_t text[64];
    swprintf_s(text, L"%+.*f초", decimals, seconds);
    return text;
}

std::wstring HdrDescription(const std::wstring& transfer, const std::wstring& matrix) {
    if (transfer.empty()) {
        return {};
    }
    if (matrix == L"dolbyvision") {
        return L"예 (Dolby Vision)";
    }
    return transfer == L"pq" ? L"예 (PQ)" : transfer == L"hlg" ? L"예 (HLG)" : L"아니요";
}

std::wstring ChannelConversion(const std::wstring& source, const std::wstring& output, double sourceCount, double outputCount) {
    if (outputCount == sourceCount) {
        return source == output ? L"없음" : L"배치 변환 (" + source + L" → " + output + L")";
    }
    return (outputCount < sourceCount ? L"다운믹스 (" : L"업믹스 (") + source + L" → " + output + L")";
}

const MediaTrack* SelectedTrack(const PlaybackState& state, TrackType type) {
    for (const MediaTrack& track : state.tracks) {
        if (track.type == type && track.selected) {
            return &track;
        }
    }
    return nullptr;
}

std::wstring Join(const std::vector<std::wstring>& parts) {
    std::wstring joined;
    for (const std::wstring& part : parts) {
        if (!part.empty()) {
            joined += (joined.empty() ? L"" : L", ") + part;
        }
    }
    return joined;
}

std::wstring TrackDescription(const MediaTrack& track) {
    std::vector<std::wstring> parts{
        track.codecDescription.empty() ? track.codec : track.codecDescription,
        track.codecProfile,
    };
    if (track.type == TrackType::Other || track.width > 0) {
        parts.push_back(track.width > 0 ? std::to_wstring(track.width) + L" x " + std::to_wstring(track.height) : L"");
        parts.push_back(track.fps > 0.0 ? Number(track.fps, 3) + L" fps" : L"");
    }
    parts.push_back(track.channelLayout);
    parts.push_back(track.sampleRate > 0 ? Number(track.sampleRate / 1000.0, 1) + L" kHz" : L"");
    parts.push_back(track.bitrate > 0 ? Bitrate(static_cast<double>(track.bitrate)) : L"");
    parts.push_back(track.language);
    parts.push_back(track.title);
    parts.push_back(track.isDefault ? L"기본" : L"");
    parts.push_back(track.forced ? L"강제" : L"");
    parts.push_back(track.externalFileName.empty() ? L"" : L"외부 파일 " + track.externalFileName);
    parts.push_back(track.selected ? L"사용 중" : L"");
    return Join(parts);
}

std::wstring RegistryText(const wchar_t* key, const wchar_t* name) {
    wchar_t buffer[256];
    DWORD bytes = sizeof(buffer);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_SZ, nullptr, buffer, &bytes) != ERROR_SUCCESS) {
        return {};
    }
    return buffer;
}

std::wstring WindowsVersion() {
    using RtlGetVersionFunction = LONG(WINAPI*)(OSVERSIONINFOW*);
    // GetVersionEx reports an older version to programs that don't declare the newest one.
    const auto getVersion = reinterpret_cast<RtlGetVersionFunction>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")));
    OSVERSIONINFOW version{sizeof(version)};
    if (!getVersion || getVersion(&version) != 0) {
        return {};
    }
    constexpr wchar_t kKey[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    std::wstring text = version.dwBuildNumber >= 22000 ? L"Windows 11" : L"Windows " + std::to_wstring(version.dwMajorVersion);
    const std::wstring release = RegistryText(kKey, L"DisplayVersion");
    if (!release.empty()) {
        text += L" " + release;
    }
    return text + L" (빌드 " + std::to_wstring(version.dwBuildNumber) + L")";
}

// libass packs its version as hex digits: 0x01704000 is 0.17.4.
std::wstring LibassVersion(const std::optional<double>& packed) {
    if (!packed) {
        return {};
    }
    const unsigned value = static_cast<unsigned>(*packed);
    wchar_t text[32];
    swprintf_s(text, L"%x.%x.%x", value >> 28, (value >> 20) & 0xFF, (value >> 12) & 0xFF);
    return text;
}

std::wstring DriverVersion(IDXGIAdapter1& adapter) {
    LARGE_INTEGER version{};
    if (FAILED(adapter.CheckInterfaceSupport(__uuidof(IDXGIDevice), &version))) {
        return {};
    }
    const auto part = [&](int shift) { return std::to_wstring((version.QuadPart >> shift) & 0xFFFF); };
    return part(48) + L"." + part(32) + L"." + part(16) + L"." + part(0);
}

void AddMonitorRows(Rows& rows, IDXGIOutput& output, HWND window) {
    ComPtr<IDXGIOutput6> details;
    DXGI_OUTPUT_DESC1 description{};
    if (FAILED(output.QueryInterface(IID_PPV_ARGS(&details))) || FAILED(details->GetDesc1(&description))) {
        return;
    }
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(description.DeviceName, ENUM_CURRENT_SETTINGS, &mode)) {
        rows.Add(L"해상도", std::to_wstring(mode.dmPelsWidth) + L" x " + std::to_wstring(mode.dmPelsHeight));
        rows.Add(L"주사율", std::to_wstring(mode.dmDisplayFrequency) + L" Hz");
    }
    rows.Add(L"화면 배율", std::to_wstring(MulDiv(static_cast<int>(GetDpiForWindow(window)), 100, 96)) + L"%");
    const bool hdr = description.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    rows.Add(L"HDR", hdr ? L"켜짐" : L"꺼짐");
    rows.Add(L"색 깊이", std::to_wstring(description.BitsPerColor) + L"비트");
    if (description.MaxLuminance > 0.0f) {
        rows.Add(L"최대 밝기", Whole(description.MaxLuminance) + L" nit");
    }
}

void AddGraphicsRows(Rows& rows, HWND window) {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return;
    }
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    ComPtr<IDXGIOutput> monitorOutput;
    std::vector<ComPtr<IDXGIAdapter1>> adapters;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)) || (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }
        adapters.push_back(adapter);
        ComPtr<IDXGIOutput> output;
        for (UINT j = 0; adapter->EnumOutputs(j, &output) != DXGI_ERROR_NOT_FOUND; ++j) {
            DXGI_OUTPUT_DESC outputDescription{};
            if (SUCCEEDED(output->GetDesc(&outputDescription)) && outputDescription.Monitor == monitor) {
                monitorOutput = output;
            }
        }
    }

    rows.Group(L"그래픽");
    for (size_t i = 0; i < adapters.size(); ++i) {
        DXGI_ADAPTER_DESC1 description{};
        adapters[i]->GetDesc1(&description);
        const std::wstring suffix = adapters.size() > 1 ? L" " + std::to_wstring(i + 1) : L"";
        rows.Add(L"그래픽 카드" + suffix, description.Description);
        if (description.DedicatedVideoMemory > 0) {
            rows.Add(L"전용 메모리" + suffix, Bytes(static_cast<double>(description.DedicatedVideoMemory)));
        }
        rows.Add(L"드라이버" + suffix, DriverVersion(*adapters[i].Get()));
    }

    rows.Group(L"현재 모니터");
    if (monitorOutput) {
        AddMonitorRows(rows, *monitorOutput.Get(), window);
    }
}

}  // namespace

std::vector<InfoRow> BuildPlaybackRows(const MpvPlayer& player, const PlaybackState& state) {
    std::vector<InfoRow> list;
    Rows rows(list);

    rows.Group(L"재생");
    rows.Add(L"상태", state.fileName.empty() ? L"" : state.atEnd ? L"끝남" : state.paused ? L"일시정지" : L"재생 중");
    if (state.position) {
        std::wstring position = FormatTime(*state.position);
        if (state.duration && *state.duration > 0.0) {
            position += L" / " + FormatTime(*state.duration) + L" (" + Whole(*state.position / *state.duration * 100.0) + L"%)";
        }
        rows.Add(L"위치", position);
    }
    wchar_t speed[32];
    swprintf_s(speed, L"%g배", state.speed);
    rows.Add(L"배속", speed);
    rows.Add(L"볼륨", Whole(state.volume) + L"%");
    if (state.chapter && *state.chapter >= 0 && static_cast<size_t>(*state.chapter) < state.chapters.size()) {
        const Chapter& chapter = state.chapters[static_cast<size_t>(*state.chapter)];
        rows.Add(L"챕터", std::to_wstring(*state.chapter + 1) + L" / " + std::to_wstring(state.chapters.size()) + L": " + chapter.title);
    }
    if (state.loopStart || state.loopEnd) {
        rows.Add(L"구간 반복", (state.loopStart ? FormatTime(*state.loopStart) : L"처음") + L" ~ " + (state.loopEnd ? FormatTime(*state.loopEnd) : L"끝"));
    }
    if (state.aspect != AspectMode::Original) {
        rows.Add(L"화면 비율", AspectLabel(state.aspect));
    }
    if (state.rotation != 0) {
        rows.Add(L"회전", std::to_wstring(state.rotation) + L"도");
    }

    rows.Group(L"비디오");
    if (const MediaTrack* track = SelectedTrack(state, TrackType::Other)) {
        rows.Add(L"코덱", Join({track->codecDescription.empty() ? track->codec : track->codecDescription, track->codecProfile}));
    }
    const std::wstring hardware = player.PropertyText("hwdec-current");
    if (!player.PropertyText("video-format").empty()) {
        rows.Add(L"디코더", hardware.empty() || hardware == L"no" ? L"소프트웨어" : L"하드웨어 (" + hardware + L")");
    }
    rows.Add(L"원본 크기", Size(player.PropertyNumber("video-params/w"), player.PropertyNumber("video-params/h")));
    rows.Add(L"화면에 맞춘 크기", Size(player.PropertyNumber("osd-dimensions/w"), player.PropertyNumber("osd-dimensions/h")));
    // While decoding in hardware, the plain property only names the kind of GPU surface.
    const std::wstring hardwareFormat = player.PropertyText("video-params/hw-pixelformat");
    rows.Add(L"픽셀 형식", hardwareFormat.empty() ? player.PropertyText("video-params/pixelformat") : hardwareFormat);
    const std::wstring matrix = player.PropertyText("video-params/colormatrix");
    rows.Add(L"HDR", HdrDescription(player.PropertyText("video-params/gamma"), matrix));
    rows.Add(L"행렬", matrix);
    rows.Add(L"색 영역", player.PropertyText("video-params/primaries"));
    const std::wstring transfer = player.PropertyText("video-params/gamma");
    rows.Add(L"전달 함수", transfer);
    if (transfer == L"pq" || transfer == L"hlg") {
        const std::optional<double> peak = player.PropertyNumber("video-params/sig-peak");
        rows.Add(L"HDR 최대 밝기", peak && *peak > 0.0 ? Whole(*peak * kSdrReferenceNits) + L" nit" : L"");
    }
    rows.Add(L"파일의 프레임 속도", Fps(player.PropertyNumber("container-fps")));
    rows.Add(L"출력 프레임 속도", Fps(player.PropertyNumber("estimated-vf-fps")));
    rows.Add(L"모니터 주사율", Fps(player.PropertyNumber("display-fps")));
    rows.Add(L"비트레이트", Bitrate(player.PropertyNumber("video-bitrate")));
    const auto count = [&](const char* name) {
        const std::optional<double> value = player.PropertyNumber(name);
        return value ? Whole(*value) : std::wstring();
    };
    rows.Add(L"버린 프레임 (출력)", count("frame-drop-count"));
    rows.Add(L"버린 프레임 (디코더)", count("decoder-frame-drop-count"));
    rows.Add(L"늦은 프레임", count("vo-delayed-frame-count"));

    rows.Group(L"오디오");
    if (const MediaTrack* track = SelectedTrack(state, TrackType::Audio)) {
        rows.Add(L"코덱", track->codecDescription.empty() ? track->codec : track->codecDescription);
        rows.Add(L"언어", track->language);
    }
    const std::optional<double> sampleRate = player.PropertyNumber("audio-params/samplerate");
    rows.Add(L"샘플레이트", sampleRate ? Number(*sampleRate / 1000.0, 1) + L" kHz" : L"");
    const std::optional<double> outputRate = player.PropertyNumber("audio-out-params/samplerate");
    if (outputRate && sampleRate && *outputRate != *sampleRate) {
        rows.Add(L"출력 샘플레이트", Number(*outputRate / 1000.0, 1) + L" kHz");
    }
    const std::wstring sourceChannels = player.PropertyText("audio-params/hr-channels");
    const std::wstring outputChannels = player.PropertyText("audio-out-params/hr-channels");
    rows.Add(L"원본 채널", sourceChannels);
    rows.Add(L"출력 채널", outputChannels);
    const std::optional<double> sourceCount = player.PropertyNumber("audio-params/channel-count");
    const std::optional<double> outputCount = player.PropertyNumber("audio-out-params/channel-count");
    if (sourceCount && outputCount) {
        rows.Add(L"채널 변환", ChannelConversion(sourceChannels, outputChannels, *sourceCount, *outputCount));
    }
    rows.Add(L"샘플 형식", player.PropertyText("audio-params/format"));
    rows.Add(L"비트레이트", Bitrate(player.PropertyNumber("audio-bitrate")));
    rows.Add(L"출력", player.PropertyText("current-ao"));
    const std::optional<double> avSync = player.PropertyNumber("avsync");
    rows.Add(L"영상과의 차이", avSync ? Number(*avSync * 1000.0, 1) + L" ms" : L"");
    if (state.audioDelay != 0.0) {
        rows.Add(L"음성 싱크", Seconds(state.audioDelay, 1));
    }

    rows.Group(L"자막");
    if (const MediaTrack* track = SelectedTrack(state, TrackType::Subtitle)) {
        rows.Add(L"트랙", TrackLabel(*track));
    } else if (!state.fileName.empty()) {
        rows.Add(L"트랙", L"없음");
    }
    if (state.subtitleDelay != 0.0) {
        rows.Add(L"싱크", Seconds(state.subtitleDelay, 1));
    }

    rows.Group(L"버퍼");
    const std::optional<double> cached = player.PropertyNumber("demuxer-cache-duration");
    rows.Add(L"미리 읽은 시간", cached ? Number(*cached, 1) + L"초" : L"");
    const std::optional<double> readSpeed = player.PropertyNumber("cache-speed");
    rows.Add(L"읽기 속도", readSpeed ? Bytes(*readSpeed) + L"/s" : L"");
    return list;
}

std::vector<InfoRow> BuildFileRows(const MpvPlayer& player, const PlaybackState& state) {
    std::vector<InfoRow> list;
    Rows rows(list);

    rows.Group(L"파일");
    rows.Add(L"이름", state.fileName);
    rows.Add(L"경로", player.PropertyText("path"));
    const std::optional<double> size = player.PropertyNumber("file-size");
    rows.Add(L"크기", size ? Bytes(*size) : L"");
    rows.Add(L"형식", player.PropertyText("file-format"));
    if (state.duration && *state.duration > 0.0) {
        rows.Add(L"길이", FormatTime(*state.duration));
        rows.Add(L"평균 비트레이트", size ? Bitrate(*size * 8.0 / *state.duration) : L"");
    }
    rows.Add(L"챕터", state.chapters.empty() ? L"" : std::to_wstring(state.chapters.size()) + L"개");

    rows.Group(L"메타데이터");
    for (const auto& [key, value] : state.metadata) {
        rows.Add(key, value);
    }

    for (const auto& [type, group, label] : {std::tuple{TrackType::Other, L"비디오 트랙", L"비디오 "},
                                             std::tuple{TrackType::Audio, L"오디오 트랙", L"오디오 "},
                                             std::tuple{TrackType::Subtitle, L"자막 트랙", L"자막 "}}) {
        rows.Group(group);
        for (const MediaTrack& track : state.tracks) {
            if (track.type == type) {
                rows.Add(label + std::to_wstring(track.id), TrackDescription(track));
            }
        }
    }
    return list;
}

std::vector<InfoRow> BuildSystemRows(const MpvPlayer& player, HWND window) {
    std::vector<InfoRow> list;
    Rows rows(list);

    rows.Group(L"플레이어");
    rows.Add(L"mpv", player.PropertyText("mpv-version"));
    rows.Add(L"FFmpeg", player.PropertyText("ffmpeg-version"));
    rows.Add(L"libass", LibassVersion(player.PropertyNumber("libass-version")));
    rows.Add(L"영상 출력", player.PropertyText("current-vo"));
    rows.Add(L"그래픽 API", player.PropertyText("current-gpu-context"));
    PROCESS_MEMORY_COUNTERS memory{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) {
        rows.Add(L"사용 중인 메모리", Bytes(static_cast<double>(memory.WorkingSetSize)));
    }

    AddGraphicsRows(rows, window);

    rows.Group(L"시스템");
    rows.Add(L"운영체제", WindowsVersion());
    rows.Add(L"프로세서", RegistryText(L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString"));
    rows.Add(L"논리 프로세서", std::to_wstring(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)) + L"개");
    MEMORYSTATUSEX systemMemory{sizeof(systemMemory)};
    if (GlobalMemoryStatusEx(&systemMemory)) {
        const double total = static_cast<double>(systemMemory.ullTotalPhys);
        const double available = static_cast<double>(systemMemory.ullAvailPhys);
        rows.Add(L"메모리", Bytes(total) + L" (사용 가능 " + Bytes(available) + L")");
    }
    return list;
}
