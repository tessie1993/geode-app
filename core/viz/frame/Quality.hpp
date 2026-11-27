#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Settings › Performance's three tiers and the first-run pick for this phone (#437).
// Live surfaces only (the view, the dream); an export never reads them. Pure, so the
// host can test it (core/tests/quality_test.cpp). Kotlin reads the tiers through geode_perf_tier
// and passes in what Android says about the phone (RAM, the low-RAM flag, the media performance
// class) for geode_perf_mode_for_device; every geode_perf_* is core/api/geode_perf_api.cpp.
namespace geode::viz::frame {

enum class QualityMode : int { Saver = 0, Balanced = 1, High = 2 };
inline constexpr int kQualityModes = 3;

struct QualityTier {
    float renderScale;  // fraction of the view's pixels the window buffer has (bufferSize)
    float fpsCap;       // the live frame rate (viz/frame/LiveFps)
    float marchScale;   // ray-march step budget for marched styles
    bool postFx;        // trails and the other optional passes
};

// Saver is cheaper than High on every axis; Balanced sits between. Every value is one the
// Performance tab offers as a choice, so picking a tier never shows a value the tab can't.
inline constexpr std::array<QualityTier, kQualityModes> kQualityTiers = {{
    {0.5f, 30.0f, 0.5f, false},
    {0.75f, 60.0f, 0.75f, true},
    {1.0f, 60.0f, 1.0f, true},
}};

inline const QualityTier& qualityTier(QualityMode mode) { return kQualityTiers[static_cast<int>(mode)]; }

// The render scales and march scales the tab offers, ascending; the march row names them Low, Medium, High.
inline constexpr std::array<float, 3> kScaleChoices = {0.5f, 0.75f, 1.0f};

// The window buffer a live surface renders into: the view's pixels times the render scale, at least one pixel
// each way. The shell sizes the surface to it (SurfaceHolder.setFixedSize) and the display's scaler fills the
// view, so the scene, the composite and the UI blur all shade these pixels and the scene targets equal the
// buffer. A non-finite scale (a corrupt pref) is full size; any scale holds to 0.25..1.
struct BufferSize {
    int width = 0;
    int height = 0;
};

inline BufferSize bufferSize(float renderScale, int viewWidth, int viewHeight) {
    if (viewWidth <= 0 || viewHeight <= 0) return {};
    const float s = std::isfinite(renderScale) ? std::clamp(renderScale, 0.25f, 1.0f) : 1.0f;
    const auto side = [s](int px) { return std::clamp(static_cast<int>(std::lround(static_cast<float>(px) * s)), 1, px); };
    return {side(viewWidth), side(viewHeight)};
}

// The tier these values are, or -1 once the user changed one (the tab reads "Custom").
inline int modeOf(float renderScale, int fpsCap, float marchScale, bool postFx) {
    for (int m = 0; m < kQualityModes; ++m) {
        const QualityTier& t = kQualityTiers[m];
        if (t.renderScale == renderScale && t.fpsCap == static_cast<float>(fpsCap) && t.marchScale == marchScale &&
            t.postFx == postFx)
            return m;
    }
    return -1;
}

// The first-run pick. A low-RAM phone, or one under 3 GB, starts on Saver. Media performance class
// 12 (Android S, 31) or better starts on High. Everything else starts on Balanced, however much RAM
// it has: RAM says nothing about the GPU, and a 6 GB phone picked for High drew a marched style at
// 20 fps at full resolution. An unknown RAM (0) is not read as small.
inline constexpr int64_t kMiB = 1024LL * 1024LL;
inline constexpr int64_t kSaverBelowRam = 3000 * kMiB;
inline constexpr int kHighPerformanceClass = 31;

inline QualityMode modeForDevice(int64_t totalRamBytes, bool lowRamDevice, int mediaPerformanceClass) {
    if (lowRamDevice || (totalRamBytes >= 1 && totalRamBytes < kSaverBelowRam)) return QualityMode::Saver;
    if (mediaPerformanceClass >= kHighPerformanceClass) return QualityMode::High;
    return QualityMode::Balanced;
}

}  // namespace geode::viz::frame
