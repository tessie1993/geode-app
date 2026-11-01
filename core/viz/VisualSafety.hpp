#pragma once
#include <string>

#include "viz/Lfo.hpp"
#include "viz/Params.hpp"

namespace geode::viz {

enum class TransitionStyle { Cut, Fade, Melt, Slide, Zoom };
enum class BlendMode { Normal, Screen, Add, Multiply, Difference, Overlay, Lighten, Darken };

const char* transitionStyleId(TransitionStyle style);
BlendMode blendModeFromOrdinal(int i);

// Port of VisualSafety.kt: the unconditional flash and luminance clamp.
namespace safety {

constexpr float kWcagFlashesPerSecond = 3.0f;
constexpr float kMaxFlashDepth = 0.25f;
constexpr float kStrobeShaderDepth = 0.85f;
constexpr float kFlashShaderDepth = 0.6f;
constexpr float kReducedMotionScale = 0.4f;

// ---- The output flash limiter (viz/FlashLimiter, viz/FlashLimitPass) ----
//
// WCAG 2.3.1 general flash: a pair of opposing changes in relative luminance (linear, Rec.709
// weights after the sRGB decode) of 10% of the maximum or more, i.e. two opposing swings of
// >= kFlashSwing. "No more than three flashes in any one second" therefore means no more than six
// such swings (transitions) in any 1 s window. The limiter keeps that true per screen tile of the
// finished frame, whatever drew it: built-in styles, MilkDrop presets, user GLSL.
//
// Per tile it keeps the luminance it shows, L, and an anchor A that L drags inside a band of
// half-width kFlashBand (a backlash: |A - L| <= h always). Only the anchor's movement is paid for,
// out of a token bucket: kFlashBudgetCapacity of luminance, refilled at kFlashBudgetRefill per
// second, plus kFlashFreeRate per second that is never charged (scaled by each frame's dt, so a
// slow fade costs nothing at any frame rate). So over any window of T seconds the anchor moves at
// most C + (R + F) * T.
//
// Why the band: a swing of L between extremes m and M (M - m >= 0.1) drags the anchor from at most
// m + h to at least M - h, so every transition costs the anchor at least 0.1 - 2h. Jitter smaller
// than 2h (a busy tile's mean twitching from frame to frame, which is never a flash) moves L
// inside the band and costs nothing, so the limiter stays out of the way of ordinary motion
// instead of draining its budget on it and flattening the tile.
//
// The arithmetic: seven transitions in one second would cost the anchor at least 7 * (0.1 - 2h)
// = 7 * 0.08 = 0.56. The bucket allows at most C + R + F = 0.36 + 0.13 + 0.06 = 0.55 in a second,
// so at most six transitions fit: three flashes, also under the strictest (Harding) count. The
// owner's reading of the rule, "four flashes = eight swings of 0.1 = 0.8 total change", is met
// with room: eight transitions would need 0.64 of anchor travel. The 0.005 of slack absorbs the
// 16-bit quantisation the GPU state goes through (at most ~1e-3 per second).
//
// What the numbers let through untouched: a single beat flash up to about +0.2 of full-screen
// luminance and back (it costs ~0.37 against C = 0.36 plus what refills while it decays); any fade
// slower than R + F = 0.19 per second; any jitter under 0.02. What they catch: a 10 Hz black/white
// strobe is held to a gray that moves at 0.19 per second after the first 0.37 jump; a hard cut from
// black to white shows its first 0.37 at once and reaches white in about 3.3 s.
constexpr float kFlashSwing = 0.1f;
constexpr float kFlashBand = 0.01f;
constexpr float kFlashBudgetCapacity = 0.36f;
constexpr float kFlashBudgetRefill = 0.13f;
constexpr float kFlashFreeRate = 0.06f;
// The longest frame step the limiter credits (the renderer clamps dt to 0.1 s too): a stall never
// banks more than this much refill and free movement at once.
constexpr float kFlashMaxDt = 0.1f;
// The tile grid, long side x short side of the frame (16 x 9 in landscape, 9 x 16 in portrait).
constexpr int kFlashGridLong = 16;
constexpr int kFlashGridShort = 9;
constexpr float kFlashPerTransition = kFlashSwing - 2.0f * kFlashBand;
static_assert(kFlashBudgetCapacity + kFlashBudgetRefill + kFlashFreeRate + 0.005f < 7.0f * kFlashPerTransition,
              "the flash budget must not fit seven 10% transitions (four flashes) into one second");
static_assert(kFlashBand > 0.0f && kFlashPerTransition > 0.0f, "the band must be narrower than half a flash swing");

SceneParams apply(const SceneParams& p, bool reducedMotion = false);
inline float flashImpulse(float flash, float beatImpulse) { return flash * beatImpulse * kFlashShaderDepth; }
inline float strobeHz() { return kWcagFlashesPerSecond; }
float limitLfoRate(float rateHz, LfoTarget target);
float beatMinIntervalMs(float requestedMs);
TransitionStyle transitionStyle(TransitionStyle requested);
std::string transitionId(const std::string& requested);
float layerMix(float requested, BlendMode mode);

}  // namespace safety

// Port of FlashBudget.kt: three rising edges per rolling second.
class FlashBudget {
public:
    static constexpr float kRiskThreshold = 0.08f;

    explicit FlashBudget(float maxPerSecond = safety::kWcagFlashesPerSecond) : maxPerSecond_(maxPerSecond) {}
    void reset();
    float gainFor(float timeSeconds, float impulse);

private:
    static constexpr float kSuppressedScale = 0.6f;
    static constexpr float kMinGain = 0.05f;
    static constexpr float kWindowSeconds = 1.0f;
    static constexpr int kCapacity = 16;

    void dropOlderThan(float cutoff);
    void record(float timeSeconds);

    float maxPerSecond_;
    float edges_[kCapacity] = {};
    int head_ = 0;
    int count_ = 0;
    bool above_ = false;
    float lastTime_ = -3.0e38f;
};

}  // namespace geode::viz
