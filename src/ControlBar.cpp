#include "ControlBar.h"

#include <d2d1_1helper.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "TimeFormat.h"

using Microsoft::WRL::ComPtr;

namespace {

// Sizes are in device-independent pixels (1/96 inch).
constexpr float kBarHeight = 72.0f;
constexpr float kHitHeight = 56.0f;
constexpr float kControlsCenterFromBottom = 28.0f;
constexpr float kEdgePadding = 12.0f;
constexpr float kPlayPauseSize = 36.0f;
constexpr float kTrackGap = 12.0f;
constexpr float kItemGap = 8.0f;
constexpr float kTimeWidth = 140.0f;
constexpr float kVolumeTrackWidth = 80.0f;
constexpr float kVolumeTextWidth = 44.0f;
constexpr float kFullscreenSize = 36.0f;
constexpr float kShowFullscreenMinWidth = 240.0f;
constexpr float kShowTimeMinWidth = 400.0f;
constexpr float kShowVolumeMinWidth = 600.0f;
constexpr float kTrackHitHalfHeight = 12.0f;
constexpr float kTrackThickness = 4.0f;
constexpr float kTrackThicknessHovered = 6.0f;
constexpr float kThumbRadius = 7.0f;
constexpr float kFontSize = 14.0f;
constexpr float kMarkWidth = 2.0f;
constexpr float kMarkIconHeight = 3.0f;
constexpr float kMarkIconHalfWidth = 2.5f;
constexpr double kMaxVolume = 130.0;  // mpv's default volume-max.

constexpr double kFadeSeconds = 0.12;
constexpr UINT kFadeMilliseconds = 120;

constexpr D2D1_COLOR_F kText{1.0f, 1.0f, 1.0f, 0.95f};
constexpr D2D1_COLOR_F kHoverFill{1.0f, 1.0f, 1.0f, 0.16f};
constexpr D2D1_COLOR_F kTrack{1.0f, 1.0f, 1.0f, 0.32f};
constexpr D2D1_COLOR_F kTrackFill{1.0f, 1.0f, 1.0f, 0.95f};
constexpr D2D1_COLOR_F kLoopRegion{1.0f, 0.42f, 0.0f, 0.8f};
constexpr D2D1_COLOR_F kLoopMark{1.0f, 0.42f, 0.0f, 1.0f};
constexpr D2D1_COLOR_F kLoopPlayed{1.0f, 0.78f, 0.56f, 1.0f};
constexpr D2D1_COLOR_F kChapterMark{1.0f, 0.85f, 0.0f, 1.0f};
constexpr D2D1_COLOR_F kScrimBottom{0.0f, 0.0f, 0.0f, 0.72f};
constexpr D2D1_COLOR_F kScrimTop{0.0f, 0.0f, 0.0f, 0.0f};

void Check(HRESULT result, const char* action) {
    if (FAILED(result)) {
        throw std::runtime_error(std::string("ControlBar: ") + action + " failed");
    }
}

ComPtr<IDWriteTextFormat> CreateTextFormat(IDWriteFactory* factory, DWRITE_TEXT_ALIGNMENT alignment) {
    ComPtr<IDWriteTextFormat> format;
    Check(factory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                    DWRITE_FONT_STRETCH_NORMAL, kFontSize, L"ko-KR", &format),
          "CreateTextFormat");
    format->SetTextAlignment(alignment);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return format;
}

}  // namespace

ControlBar::ControlBar(HWND window) : window_(window) {
    Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.GetAddressOf()), "D2D1CreateFactory");
    Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                              reinterpret_cast<IUnknown**>(writeFactory_.GetAddressOf())),
          "DWriteCreateFactory");
    timeFormat_ = CreateTextFormat(writeFactory_.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING);
    volumeFormat_ = CreateTextFormat(writeFactory_.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING);

    ComPtr<ID2D1GeometrySink> sink;
    Check(d2dFactory_->CreatePathGeometry(&playIcon_), "CreatePathGeometry");
    Check(playIcon_->Open(&sink), "ID2D1PathGeometry::Open");
    sink->BeginFigure(D2D1::Point2F(-6.0f, -9.0f), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(9.0f, 0.0f));
    sink->AddLine(D2D1::Point2F(-6.0f, 9.0f));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    Check(sink->Close(), "ID2D1GeometrySink::Close");

    // A downward triangle with its point at the origin.
    ComPtr<ID2D1GeometrySink> markSink;
    Check(d2dFactory_->CreatePathGeometry(&markIcon_), "CreatePathGeometry");
    Check(markIcon_->Open(&markSink), "ID2D1PathGeometry::Open");
    markSink->BeginFigure(D2D1::Point2F(0.0f, 0.0f), D2D1_FIGURE_BEGIN_FILLED);
    markSink->AddLine(D2D1::Point2F(-kMarkIconHalfWidth, -kMarkIconHeight));
    markSink->AddLine(D2D1::Point2F(kMarkIconHalfWidth, -kMarkIconHeight));
    markSink->EndFigure(D2D1_FIGURE_END_CLOSED);
    Check(markSink->Close(), "ID2D1GeometrySink::Close");

    if (!CreateDeviceResources()) {
        throw std::runtime_error("ControlBar: could not create the graphics device");
    }
}

bool ControlBar::CreateDeviceResources() {
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                       D3D11_SDK_VERSION, &d3dDevice_, nullptr, nullptr);
    if (FAILED(result)) {
        result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
                                   D3D11_SDK_VERSION, &d3dDevice_, nullptr, nullptr);
    }
    if (FAILED(result)) {
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<ID2D1Device> d2dDevice;
    if (FAILED(d3dDevice_.As(&dxgiDevice)) ||
        FAILED(dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&dxgiFactory_))) ||
        FAILED(d2dFactory_->CreateDevice(dxgiDevice.Get(), &d2dDevice)) ||
        FAILED(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context_)) ||
        FAILED(context_->CreateSolidColorBrush(kText, &brush_)) ||
        FAILED(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&compositionDevice_))) ||
        FAILED(compositionDevice_->CreateTargetForHwnd(window_, TRUE, &compositionTarget_)) ||
        FAILED(compositionDevice_->CreateVisual(&visual_)) ||
        FAILED(compositionDevice_->CreateEffectGroup(&effect_)) ||
        FAILED(visual_->SetEffect(effect_.Get()))) {
        ReleaseDeviceResources();
        return false;
    }
    // Text sits on a transparent surface, where ClearType's per-channel blending can't work.
    context_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    return true;
}

void ControlBar::ReleaseDeviceResources() {
    if (context_) {
        context_->SetTarget(nullptr);
    }
    swapChain_.Reset();
    effect_.Reset();
    visual_.Reset();
    compositionTarget_.Reset();
    compositionDevice_.Reset();
    brush_.Reset();
    context_.Reset();
    dxgiFactory_.Reset();
    d3dDevice_.Reset();
    lastFrame_.reset();
}

void ControlBar::Resize(int clientWidth, int clientHeight, UINT dpi) {
    clientWidth_ = clientWidth;
    clientHeight_ = clientHeight;
    dpi_ = dpi;
    lastFrame_.reset();
    Render();
    UpdateComposition();
}

void ControlBar::SetVisible(bool visible) {
    if (visible == visible_) {
        return;
    }
    visible_ = visible;
    fadingOut_ = !visible;
    Render();
    StartFade(visible ? 1.0f : 0.0f);
    UpdateComposition();
    if (visible) {
        KillTimer(window_, kFadeTimerId);
    } else {
        SetTimer(window_, kFadeTimerId, kFadeMilliseconds + 30, nullptr);
    }
}

void ControlBar::HandleFadeTimer() {
    KillTimer(window_, kFadeTimerId);
    fadingOut_ = false;
    UpdateComposition();
}

float ControlBar::CurrentOpacity() const {
    const double elapsed = static_cast<double>(GetTickCount64() - fadeStart_) / 1000.0;
    const double progress = std::clamp(elapsed / kFadeSeconds, 0.0, 1.0);
    return fadeFrom_ + (fadeTo_ - fadeFrom_) * static_cast<float>(progress);
}

void ControlBar::StartFade(float target) {
    if (!compositionDevice_ || !effect_) {
        return;
    }
    // Starting from the current opacity keeps a reversal in mid-fade from jumping.
    fadeFrom_ = CurrentOpacity();
    fadeTo_ = target;
    fadeStart_ = GetTickCount64();

    ComPtr<IDCompositionAnimation> animation;
    if (FAILED(compositionDevice_->CreateAnimation(&animation))) {
        effect_->SetOpacity(target);
        return;
    }
    const float slope = static_cast<float>((target - fadeFrom_) / kFadeSeconds);
    animation->AddCubic(0.0, fadeFrom_, slope, 0.0f, 0.0f);
    animation->End(kFadeSeconds, target);
    effect_->SetOpacity(animation.Get());
}

void ControlBar::SetFullscreenState(bool fullscreen) {
    fullscreen_ = fullscreen;
    Render();
}

void ControlBar::SetPlayback(const PlaybackState& state) {
    playback_ = state;
    Render();
}

void ControlBar::SetSeekPreview(std::optional<double> position) {
    seekPreview_ = position;
    Render();
}

void ControlBar::SetHoveredPart(Part part) {
    hovered_ = part;
    Render();
}

float ControlBar::ToDips(int pixels) const {
    return static_cast<float>(pixels) * 96.0f / static_cast<float>(dpi_);
}

ControlBar::Layout ControlBar::ComputeLayout() const {
    Layout layout;
    layout.width = ToDips(clientWidth_);
    const float height = ToDips(surfaceHeight_);
    layout.centerY = height - kControlsCenterFromBottom;

    const float playLeft = kEdgePadding;
    const float halfControl = kPlayPauseSize / 2.0f;
    layout.playPause = D2D1::RectF(playLeft, layout.centerY - halfControl, playLeft + kPlayPauseSize,
                                   layout.centerY + halfControl);
    layout.trackLeft = layout.playPause.right + kTrackGap;

    // Narrow windows drop the extras first, so the seek track stays usable.
    float right = layout.width - kEdgePadding;
    layout.showFullscreen = layout.width >= kShowFullscreenMinWidth;
    if (layout.showFullscreen) {
        layout.fullscreen = D2D1::RectF(right - kFullscreenSize, layout.playPause.top, right, layout.playPause.bottom);
        right -= kFullscreenSize + kItemGap;
    }
    layout.showVolume = layout.width >= kShowVolumeMinWidth;
    if (layout.showVolume) {
        layout.volumeText = D2D1::RectF(right - kVolumeTextWidth, layout.playPause.top, right, layout.playPause.bottom);
        right -= kVolumeTextWidth + kItemGap;
        layout.volumeTrackRight = right;
        layout.volumeTrackLeft = right - kVolumeTrackWidth;
        right = layout.volumeTrackLeft - kThumbRadius - kItemGap;
    }
    layout.showTime = layout.width >= kShowTimeMinWidth;
    if (layout.showTime) {
        layout.time = D2D1::RectF(right - kTimeWidth, layout.playPause.top, right, layout.playPause.bottom);
        right -= kTimeWidth + kTrackGap;
    }
    right -= kThumbRadius;
    layout.trackRight = std::max(right, layout.trackLeft);
    return layout;
}

ControlBar::Frame ControlBar::ComputeFrame(const Layout& layout) const {
    Frame frame;
    const double position = seekPreview_.value_or(playback_.position.value_or(0.0));
    frame.time = FormatTime(position);
    double fraction = 0.0;
    if (playback_.duration && *playback_.duration > 0.0) {
        frame.time += L" / " + FormatTime(*playback_.duration);
        fraction = std::clamp(position / *playback_.duration, 0.0, 1.0);
    }
    frame.volume = std::to_wstring(std::lround(playback_.volume)) + L"%";
    const double trackPixels = (layout.trackRight - layout.trackLeft) * dpi_ / 96.0;
    frame.fillPixels = static_cast<int>(std::lround(fraction * trackPixels));
    const double volumePixels = (layout.volumeTrackRight - layout.volumeTrackLeft) * dpi_ / 96.0;
    frame.volumePixels = static_cast<int>(std::lround(std::clamp(playback_.volume / kMaxVolume, 0.0, 1.0) * volumePixels));
    if (playback_.duration && *playback_.duration > 0.0) {
        const auto toPixels = [&](double seconds) {
            return static_cast<int>(std::lround(std::clamp(seconds / *playback_.duration, 0.0, 1.0) * trackPixels));
        };
        frame.loopStartPixels = playback_.loopStart ? toPixels(*playback_.loopStart) : -1;
        frame.loopEndPixels = playback_.loopEnd ? toPixels(*playback_.loopEnd) : -1;
        // The first chapter usually starts at 0, where there's nothing to separate.
        for (const Chapter& chapter : playback_.chapters) {
            if (chapter.time > 0.0 && chapter.time < *playback_.duration) {
                frame.chapterPixels.push_back(toPixels(chapter.time));
            }
        }
    }
    frame.fullscreen = fullscreen_;
    frame.paused = playback_.paused;
    frame.hovered = hovered_;
    return frame;
}

ControlBar::Part ControlBar::HitTest(POINT point) const {
    if (!visible_ || clientHeight_ == 0) {
        return Part::None;
    }
    const float x = ToDips(point.x);
    const float y = ToDips(point.y - (clientHeight_ - surfaceHeight_));
    const float height = ToDips(surfaceHeight_);
    if (y < height - kHitHeight || y > height || x < 0.0f || x > ToDips(clientWidth_)) {
        return Part::None;
    }

    const Layout layout = ComputeLayout();
    const D2D1_RECT_F& play = layout.playPause;
    if (x >= play.left && x <= play.right && y >= play.top && y <= play.bottom) {
        return Part::PlayPause;
    }
    const auto contains = [&](const D2D1_RECT_F& r) { return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom; };
    if (layout.showFullscreen && contains(layout.fullscreen)) {
        return Part::Fullscreen;
    }
    if (layout.showVolume && x >= layout.volumeTrackLeft - kThumbRadius && x <= layout.volumeTrackRight + kThumbRadius &&
        std::abs(y - layout.centerY) <= kTrackHitHalfHeight) {
        return Part::VolumeTrack;
    }
    if (layout.trackRight > layout.trackLeft &&
        x >= layout.trackLeft - kThumbRadius && x <= layout.trackRight + kThumbRadius &&
        std::abs(y - layout.centerY) <= kTrackHitHalfHeight) {
        return Part::SeekTrack;
    }
    return Part::Background;
}

double ControlBar::SeekFractionAt(int clientX) const {
    const Layout layout = ComputeLayout();
    const float span = layout.trackRight - layout.trackLeft;
    if (span <= 0.0f) {
        return 0.0;
    }
    return std::clamp(static_cast<double>((ToDips(clientX) - layout.trackLeft) / span), 0.0, 1.0);
}

double ControlBar::VolumeAt(int clientX) const {
    const Layout layout = ComputeLayout();
    const float span = layout.volumeTrackRight - layout.volumeTrackLeft;
    if (span <= 0.0f) {
        return 0.0;
    }
    return std::clamp(static_cast<double>((ToDips(clientX) - layout.volumeTrackLeft) / span), 0.0, 1.0) * kMaxVolume;
}

bool ControlBar::UpdateSwapChain() {
    const int height = std::min(clientHeight_, MulDiv(static_cast<int>(kBarHeight), static_cast<int>(dpi_), 96));
    if (swapChain_) {
        DXGI_SWAP_CHAIN_DESC1 current{};
        swapChain_->GetDesc1(&current);
        if (static_cast<int>(current.Width) == clientWidth_ && static_cast<int>(current.Height) == height) {
            surfaceHeight_ = height;
            context_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
            return true;
        }
        context_->SetTarget(nullptr);
        if (FAILED(swapChain_->ResizeBuffers(0, clientWidth_, height, DXGI_FORMAT_UNKNOWN, 0))) {
            return false;
        }
    } else {
        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = clientWidth_;
        description.Height = height;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.Scaling = DXGI_SCALING_STRETCH;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        if (FAILED(dxgiFactory_->CreateSwapChainForComposition(d3dDevice_.Get(), &description, nullptr, &swapChain_)) ||
            FAILED(visual_->SetContent(swapChain_.Get()))) {
            return false;
        }
    }
    surfaceHeight_ = height;
    context_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));

    ComPtr<IDXGISurface> backBuffer;
    ComPtr<ID2D1Bitmap1> target;
    const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        static_cast<float>(dpi_), static_cast<float>(dpi_));
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) ||
        FAILED(context_->CreateBitmapFromDxgiSurface(backBuffer.Get(), &properties, &target))) {
        return false;
    }
    context_->SetTarget(target.Get());
    return true;
}

void ControlBar::UpdateComposition() {
    if (!compositionDevice_) {
        return;
    }
    const bool show = (visible_ || fadingOut_) && swapChain_ && clientWidth_ > 0 && clientHeight_ > 0;
    if (show) {
        visual_->SetOffsetY(static_cast<float>(clientHeight_ - surfaceHeight_));
    }
    // With no root the layer costs nothing, and mpv's video reaches the screen untouched.
    compositionTarget_->SetRoot(show ? visual_.Get() : nullptr);
    compositionDevice_->Commit();
}

void ControlBar::Render() {
    if (!visible_ || clientWidth_ <= 0 || clientHeight_ <= 0) {
        return;
    }
    // A lost device (driver update or reset) gets one rebuild; otherwise try again on the next update.
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!context_ && !CreateDeviceResources()) {
            return;
        }
        if (UpdateSwapChain()) {
            const Layout layout = ComputeLayout();
            const Frame frame = ComputeFrame(layout);
            if (lastFrame_ && *lastFrame_ == frame) {
                return;
            }
            if (DrawFrame(layout, frame)) {
                lastFrame_ = frame;
                return;
            }
        }
        ReleaseDeviceResources();
    }
}

void ControlBar::DrawPlayPauseIcon(const Layout& layout, const Frame& frame) {
    const float centerX = (layout.playPause.left + layout.playPause.right) / 2.0f;
    if (frame.hovered == Part::PlayPause) {
        brush_->SetColor(kHoverFill);
        context_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(centerX, layout.centerY), kPlayPauseSize / 2.0f, kPlayPauseSize / 2.0f), brush_.Get());
    }
    brush_->SetColor(kText);
    if (frame.paused) {
        context_->SetTransform(D2D1::Matrix3x2F::Translation(centerX, layout.centerY));
        context_->FillGeometry(playIcon_.Get(), brush_.Get());
        context_->SetTransform(D2D1::Matrix3x2F::Identity());
    } else {
        const float top = layout.centerY - 9.0f;
        const float bottom = layout.centerY + 9.0f;
        context_->FillRectangle(D2D1::RectF(centerX - 7.0f, top, centerX - 2.0f, bottom), brush_.Get());
        context_->FillRectangle(D2D1::RectF(centerX + 2.0f, top, centerX + 7.0f, bottom), brush_.Get());
    }
}

void ControlBar::DrawFullscreenIcon(const Layout& layout, const Frame& frame) {
    const float centerX = (layout.fullscreen.left + layout.fullscreen.right) / 2.0f;
    const float centerY = layout.centerY;
    if (frame.hovered == Part::Fullscreen) {
        brush_->SetColor(kHoverFill);
        context_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(centerX, centerY), kFullscreenSize / 2.0f, kFullscreenSize / 2.0f), brush_.Get());
    }
    // Four corner brackets: pointing outward to enter fullscreen, inward to leave it.
    brush_->SetColor(kText);
    constexpr float kOuter = 8.0f;
    constexpr float kInner = 3.0f;
    constexpr float kStroke = 2.0f;
    for (const float sx : {-1.0f, 1.0f}) {
        for (const float sy : {-1.0f, 1.0f}) {
            const float cornerX = centerX + sx * (frame.fullscreen ? kInner : kOuter);
            const float cornerY = centerY + sy * (frame.fullscreen ? kInner : kOuter);
            const float armX = frame.fullscreen ? sx * (kOuter - kInner) : -sx * (kOuter - kInner);
            const float armY = frame.fullscreen ? sy * (kOuter - kInner) : -sy * (kOuter - kInner);
            context_->DrawLine(D2D1::Point2F(cornerX, cornerY), D2D1::Point2F(cornerX + armX, cornerY), brush_.Get(), kStroke);
            context_->DrawLine(D2D1::Point2F(cornerX, cornerY), D2D1::Point2F(cornerX, cornerY + armY), brush_.Get(), kStroke);
        }
    }
}

void ControlBar::DrawSeekTrack(const Layout& layout, const Frame& frame) {
    const float toDips = 96.0f / static_cast<float>(dpi_);
    const bool emphasized = frame.hovered == Part::SeekTrack;
    const float half = (emphasized ? kTrackThicknessHovered : kTrackThickness) / 2.0f;
    const float fillRight = layout.trackLeft + static_cast<float>(frame.fillPixels) * toDips;
    const auto bar = [&](float left, float right) {
        return D2D1::RoundedRect(D2D1::RectF(left, layout.centerY - half, right, layout.centerY + half), half, half);
    };
    const float startX = layout.trackLeft + static_cast<float>(frame.loopStartPixels) * toDips;
    const float endX = layout.trackLeft + static_cast<float>(frame.loopEndPixels) * toDips;

    brush_->SetColor(kTrack);
    context_->FillRoundedRectangle(bar(layout.trackLeft, layout.trackRight), brush_.Get());
    if (frame.loopStartPixels >= 0 && frame.loopEndPixels >= 0) {
        brush_->SetColor(kLoopRegion);
        context_->FillRectangle(D2D1::RectF(std::min(startX, endX), layout.centerY - half, std::max(startX, endX), layout.centerY + half), brush_.Get());
    }
    brush_->SetColor(kTrackFill);
    context_->FillRoundedRectangle(bar(layout.trackLeft, fillRight), brush_.Get());
    if (frame.loopStartPixels >= 0 && frame.loopEndPixels >= 0) {
        // Still lighter than the loop's unplayed part, so the position stands out.
        const float left = std::min(startX, endX);
        const float right = std::min(std::max(startX, endX), fillRight);
        if (right > left) {
            brush_->SetColor(kLoopPlayed);
            context_->FillRectangle(D2D1::RectF(left, layout.centerY - half, right, layout.centerY + half), brush_.Get());
        }
    }

    // Both kinds of mark are a line across the track with a triangle pointing at it:
    // from above for chapters, from below for the ends of a loop.
    const auto drawMark = [&](float x, bool fromBelow) {
        context_->FillRectangle(D2D1::RectF(x - kMarkWidth / 2.0f, layout.centerY - half, x + kMarkWidth / 2.0f, layout.centerY + half), brush_.Get());
        context_->SetTransform(fromBelow ? D2D1::Matrix3x2F::Scale(1.0f, -1.0f) * D2D1::Matrix3x2F::Translation(x, layout.centerY + half)
                                         : D2D1::Matrix3x2F::Translation(x, layout.centerY - half));
        context_->FillGeometry(markIcon_.Get(), brush_.Get());
        context_->SetTransform(D2D1::Matrix3x2F::Identity());
    };
    brush_->SetColor(kChapterMark);
    for (const int pixels : frame.chapterPixels) {
        drawMark(layout.trackLeft + static_cast<float>(pixels) * toDips, false);
    }
    brush_->SetColor(kLoopMark);
    if (frame.loopStartPixels >= 0) {
        drawMark(startX, true);
    }
    if (frame.loopEndPixels >= 0) {
        drawMark(endX, true);
    }

    if (emphasized) {
        brush_->SetColor(kTrackFill);
        context_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fillRight, layout.centerY), kThumbRadius, kThumbRadius), brush_.Get());
    }
}

void ControlBar::DrawVolume(const Layout& layout, const Frame& frame) {
    const bool emphasized = frame.hovered == Part::VolumeTrack;
    const float half = (emphasized ? kTrackThicknessHovered : kTrackThickness) / 2.0f;
    const float fillRight = layout.volumeTrackLeft + static_cast<float>(frame.volumePixels) * 96.0f / static_cast<float>(dpi_);
    const auto bar = [&](float left, float right) {
        return D2D1::RoundedRect(D2D1::RectF(left, layout.centerY - half, right, layout.centerY + half), half, half);
    };
    brush_->SetColor(kTrack);
    context_->FillRoundedRectangle(bar(layout.volumeTrackLeft, layout.volumeTrackRight), brush_.Get());
    brush_->SetColor(kTrackFill);
    context_->FillRoundedRectangle(bar(layout.volumeTrackLeft, fillRight), brush_.Get());
    if (emphasized) {
        context_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fillRight, layout.centerY), kThumbRadius, kThumbRadius), brush_.Get());
    }
    brush_->SetColor(kText);
    context_->DrawTextW(frame.volume.c_str(), static_cast<UINT32>(frame.volume.size()), volumeFormat_.Get(), layout.volumeText, brush_.Get());
}

bool ControlBar::DrawFrame(const Layout& layout, const Frame& frame) {
    const float height = ToDips(surfaceHeight_);

    context_->BeginDraw();
    context_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    // A gradient behind the controls keeps them readable over bright video.
    ComPtr<ID2D1GradientStopCollection> stops;
    ComPtr<ID2D1LinearGradientBrush> scrim;
    const D2D1_GRADIENT_STOP stopList[] = {{0.0f, kScrimTop}, {0.6f, kScrimBottom}, {1.0f, kScrimBottom}};
    if (SUCCEEDED(context_->CreateGradientStopCollection(stopList, 3, &stops)) &&
        SUCCEEDED(context_->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(0.0f, height)),
            stops.Get(), &scrim))) {
        context_->FillRectangle(D2D1::RectF(0.0f, 0.0f, layout.width, height), scrim.Get());
    }

    DrawPlayPauseIcon(layout, frame);
    if (layout.trackRight > layout.trackLeft) {
        DrawSeekTrack(layout, frame);
    }
    if (layout.showTime) {
        brush_->SetColor(kText);
        context_->DrawTextW(frame.time.c_str(), static_cast<UINT32>(frame.time.size()), timeFormat_.Get(), layout.time, brush_.Get());
    }
    if (layout.showVolume) {
        DrawVolume(layout, frame);
    }
    if (layout.showFullscreen) {
        DrawFullscreenIcon(layout, frame);
    }

    if (FAILED(context_->EndDraw())) {
        return false;
    }
    return SUCCEEDED(swapChain_->Present(1, 0));
}
