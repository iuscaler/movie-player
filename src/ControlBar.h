#pragma once

#include <windows.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <optional>
#include <string>

#include "MpvPlayer.h"

// The playback controls drawn over the video. This class only draws and answers
// "what is at this point"; MainWindow decides when to show it and what clicks do.
//
// It is a DirectComposition layer on the main window that composites above the
// window's children, so it covers mpv's video window without being a window of
// its own. All coordinates passed in are client-area pixels.
class ControlBar {
public:
    enum class Part { None, Background, PlayPause, SeekTrack, VolumeTrack, Fullscreen };

    // The main window passes this timer on to HandleFadeTimer().
    static constexpr UINT_PTR kFadeTimerId = 2;

    // Throws std::runtime_error if the graphics objects can't be created.
    explicit ControlBar(HWND window);

    ControlBar(const ControlBar&) = delete;
    ControlBar& operator=(const ControlBar&) = delete;

    void Resize(int clientWidth, int clientHeight, UINT dpi);
    void SetVisible(bool visible);
    void SetPlayback(const PlaybackState& state);
    // While set, the bar shows this position instead of the playback position.
    void SetSeekPreview(std::optional<double> position);
    void SetHoveredPart(Part part);
    void SetFullscreenState(bool fullscreen);
    void HandleFadeTimer();

    // Returns Part::None while the bar is hidden.
    Part HitTest(POINT point) const;
    // Where clientX falls along the seek track, from 0 to 1.
    double SeekFractionAt(int clientX) const;
    // The volume that clientX falls on along the volume slider.
    double VolumeAt(int clientX) const;

private:
    struct Layout {
        float width = 0;
        float centerY = 0;
        D2D1_RECT_F playPause{};
        float trackLeft = 0;
        float trackRight = 0;
        D2D1_RECT_F time{};
        float volumeTrackLeft = 0;
        float volumeTrackRight = 0;
        D2D1_RECT_F volumeText{};
        D2D1_RECT_F fullscreen{};
        bool showTime = false;
        bool showVolume = false;
        bool showFullscreen = false;
    };

    // Everything a frame depends on, so identical frames can be skipped.
    struct Frame {
        std::wstring time;
        std::wstring volume;
        int fillPixels = 0;
        int volumePixels = 0;
        int loopStartPixels = -1;  // -1 when not set.
        int loopEndPixels = -1;
        bool paused = false;
        bool fullscreen = false;
        Part hovered = Part::None;
        auto operator<=>(const Frame&) const = default;
    };

    bool CreateDeviceResources();
    void ReleaseDeviceResources();
    bool UpdateSwapChain();
    void UpdateComposition();
    void Render();
    bool DrawFrame(const Layout& layout, const Frame& frame);
    void DrawPlayPauseIcon(const Layout& layout, const Frame& frame);
    void DrawFullscreenIcon(const Layout& layout, const Frame& frame);
    void DrawSeekTrack(const Layout& layout, const Frame& frame);
    void DrawVolume(const Layout& layout, const Frame& frame);
    void StartFade(float target);
    float CurrentOpacity() const;

    Layout ComputeLayout() const;
    Frame ComputeFrame(const Layout& layout) const;
    float ToDips(int pixels) const;

    HWND window_;
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> timeFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> volumeFormat_;
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> playIcon_;

    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> compositionDevice_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> compositionTarget_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> visual_;
    Microsoft::WRL::ComPtr<IDCompositionEffectGroup> effect_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;

    int clientWidth_ = 0;
    int clientHeight_ = 0;
    int surfaceHeight_ = 0;
    UINT dpi_ = 96;
    bool visible_ = false;
    bool fadingOut_ = false;
    bool fullscreen_ = false;
    float fadeFrom_ = 0.0f;
    float fadeTo_ = 0.0f;
    ULONGLONG fadeStart_ = 0;

    PlaybackState playback_;
    std::optional<double> seekPreview_;
    Part hovered_ = Part::None;
    std::optional<Frame> lastFrame_;
};
