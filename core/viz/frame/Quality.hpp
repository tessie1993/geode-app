#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Settings › Performance's three tiers, its render resolutions and the first-run pick for this phone (#437).
// Live surfaces only (the view, the dream); an export never reads them. Pure, so the host can test it
// (core/tests/quality_test.cpp). Kotlin reads the tiers through geode_perf_tier and passes in what Android says
// about the phone: RAM, the low-RAM flag, the media performance class, and the display's panel (its largest
// physical mode, Display.Mode.getPhysicalWidth/Height) and current mode. Every geode_perf_* is
// core/api/geode_perf_api.cpp.
namespace geode::viz::frame {

enum class QualityMode : int { Saver = 0, Balanced = 1, High = 2 };
inline constexpr int kQualityModes = 3;

// A render resolution is the picture's short side in real pixels when it fills the screen: 1080 draws
// 1080 x 2400 on a 20:9 phone and 1080 x 1920 on a 16:9 one, the same "1080p" on every phone. Native is the
// panel's own. The tab offers the steps below the panel, then Native; the window buffer is sized to it and the
// display's scaler fills the screen (bufferSize).
inline constexpr int kNativeResolution = 0;
inline constexpr std::array<int, 6> kResolutionSteps = {540, 720, 900, 1080, 1440, 2160};
inline constexpr int kMaxResolutionChoices = static_cast<int>(kResolutionSteps.size()) + 1;

struct QualityTier {
    int resolution;     // short side in pixels, or kNativeResolution
    float fpsCap;       // the live frame rate (viz/frame/LiveFps)
    float marchScale;   // ray-march step budget for marched styles
    bool postFx;        // trails and the other optional passes
};

// Saver is cheaper than High on every axis; Balanced sits between. Every value is one the Performance tab
// offers as a choice, so picking a tier never shows a value the tab can't. A tier's resolution over the panel
// draws the panel's own: Balanced on a 720p phone is Native.
// Saver 540 and Balanced 900 keep the old 50% and 75% of a 1080p phone (540, 810: 900 is the nearer step up).
inline constexpr std::array<QualityTier, kQualityModes> kQualityTiers = {{
    {540, 30.0f, 0.5f, false},
    {900, 60.0f, 0.75f, true},
    {kNativeResolution, 60.0f, 1.0f, true},
}};

inline const QualityTier& qualityTier(QualityMode mode) { return kQualityTiers[static_cast<int>(mode)]; }

// The march scales the tab offers, ascending; the march row names them Low, Medium, High.
inline constexpr std::array<float, 3> kScaleChoices = {0.5f, 0.75f, 1.0f};

inline int shortSide(int width, int height) { return width > 0 && height > 0 ? std::min(width, height) : 0; }

// The short side `resolution` draws on a screen whose short side is `screen`: Native, or any resolution at or
// over the screen, is the screen's own; one under the smallest step (a corrupt pref) is the smallest step.
// An unknown screen (0) draws the resolution as stored, Native as 0.
inline int drawnShortSide(int resolution, int screen) {
    if (resolution > kNativeResolution) resolution = std::max(resolution, kResolutionSteps.front());
    if (screen <= 0) return std::max(resolution, kNativeResolution);
    return resolution <= kNativeResolution || resolution >= screen ? screen : resolution;
}

// The resolutions the tab offers on a panel `panelWidth` x `panelHeight`, ascending into out: every step under
// the panel's short side, then Native. An unknown panel offers every step. Returns how many.
inline int resolutionChoices(int panelWidth, int panelHeight, int* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    const int panel = shortSide(panelWidth, panelHeight);
    int n = 0;
    for (int step : kResolutionSteps) {
        if (panel > 0 && step >= panel) break;
        if (n < capacity) out[n++] = step;
    }
    if (n < capacity) out[n++] = kNativeResolution;
    return n;
}

// The window buffer a live surface renders into: the view scaled by the resolution over the short side of the
// display's current mode, at least one pixel each way and never more than the view. A view smaller than the
// screen (split screen, a freeform window) keeps the same pixels per inch. The shell sizes the surface to it
// (SurfaceHolder.setFixedSize) and the display's scaler fills the view, so the scene, the composite and the UI
// blur all shade these pixels and the scene targets equal the buffer. An unknown mode (0) reads the view as the
// whole screen. Called with the mode as the view, it is what the resolution draws full screen (the tab's sizes).
struct BufferSize {
    int width = 0;
    int height = 0;
};

inline BufferSize bufferSize(int resolution, int modeWidth, int modeHeight, int viewWidth, int viewHeight) {
    if (viewWidth <= 0 || viewHeight <= 0) return {};
    const int mode = shortSide(modeWidth, modeHeight);
    const int screen = mode > 0 ? mode : shortSide(viewWidth, viewHeight);
    const int drawn = drawnShortSide(resolution, screen);
    if (drawn >= screen) return {viewWidth, viewHeight};
    const double s = static_cast<double>(drawn) / static_cast<double>(screen);
    const auto side = [s](int px) { return std::clamp(static_cast<int>(std::lround(px * s)), 1, px); };
    return {side(viewWidth), side(viewHeight)};
}

// The tier these values are on a panel with short side `panelShort`, or -1 once the user changed one (the tab
// reads "Custom"). Resolutions compare as drawn: Balanced's 900 on a 720p panel is its Native.
inline int modeOf(int resolution, int fpsCap, float marchScale, bool postFx, int panelShort) {
    for (int m = 0; m < kQualityModes; ++m) {
        const QualityTier& t = kQualityTiers[m];
        if (drawnShortSide(t.resolution, panelShort) == drawnShortSide(resolution, panelShort) &&
            t.fpsCap == static_cast<float>(fpsCap) && t.marchScale == marchScale && t.postFx == postFx)
            return m;
    }
    return -1;
}

// The resolution an old install's stored render scale (the retired perf_render_scale, a fraction of the screen)
// maps to on this panel: the offered choice nearest scale x the panel's short side, the higher on a tie, so an
// upgrade never lowers what the user picked. 1080p: 50% is 540, 75% (810) is 900, 100% Native. 1440p: 75% is
// 1080, 50% 720. An unknown panel reads as 1080p; a corrupt scale is Native.
inline constexpr int kAssumedPanelShort = 1080;

inline int resolutionForScale(float renderScale, int panelWidth, int panelHeight) {
    if (!std::isfinite(renderScale)) return kNativeResolution;
    const int panel = shortSide(panelWidth, panelHeight) > 0 ? shortSide(panelWidth, panelHeight) : kAssumedPanelShort;
    const double target = std::clamp(renderScale, 0.25f, 1.0f) * static_cast<double>(panel);
    int best = kNativeResolution;
    double bestDistance = panel - target;
    for (auto it = kResolutionSteps.rbegin(); it != kResolutionSteps.rend(); ++it) {
        if (*it >= panel) continue;
        const double d = std::abs(*it - target);
        if (d < bestDistance) {
            best = *it;
            bestDistance = d;
        }
    }
    return best;
}

// The first-run pick, a default the user may change; nothing ever changes it for frame time or load (owner,
// decision 44). A low-RAM phone, or one under 3 GB, starts on Saver. Media performance class 12 (Android S,
// 31) or better starts on High, the panel's native pixels, when the panel is a 1080p-class one (up to
// kHighNativeMaxPixels); a bigger panel (1440p: 1.8 times the pixels) needs class 14 (34) for High, else it
// starts on Balanced, 900p. Everything else starts on Balanced, however much RAM it has: RAM says nothing
// about the GPU, and a 6 GB phone picked for High drew a marched style at 20 fps at full resolution. An unknown
// RAM (0) is not read as small, an unknown panel (0) not as big.
inline constexpr int64_t kMiB = 1024LL * 1024LL;
inline constexpr int64_t kSaverBelowRam = 3000 * kMiB;
inline constexpr int kHighPerformanceClass = 31;
inline constexpr int kHighAnyPanelPerformanceClass = 34;
// 1080 x 2700: every 1080-wide phone panel, the tallest (21:9) included.
inline constexpr int64_t kHighNativeMaxPixels = 1080LL * 2700LL;

inline QualityMode modeForDevice(int64_t totalRamBytes, bool lowRamDevice, int mediaPerformanceClass, int panelWidth,
                                 int panelHeight) {
    if (lowRamDevice || (totalRamBytes >= 1 && totalRamBytes < kSaverBelowRam)) return QualityMode::Saver;
    const int64_t pixels = panelWidth > 0 && panelHeight > 0 ? static_cast<int64_t>(panelWidth) * panelHeight : 0;
    if (mediaPerformanceClass >= kHighAnyPanelPerformanceClass) return QualityMode::High;
    if (mediaPerformanceClass >= kHighPerformanceClass && pixels <= kHighNativeMaxPixels) return QualityMode::High;
    return QualityMode::Balanced;
}

}  // namespace geode::viz::frame
