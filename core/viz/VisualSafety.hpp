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
// WCAG 2.3.1 / ITU-R BT.1702, counted on the finished frame. A *transition* is a change of
// relative luminance (linear, Rec.709 weights after the sRGB decode) of at least kFlashSwing between
// a local maximum and minimum, where the darker of the two is below kFlashDarkLimit; a *flash* is a
// pair of opposing transitions. "No more than three flashes in any one-second period" is therefore
// no more than six transitions in any one-second window. Saturated red is counted the same way on
// its own scale (below).
//
// Counting happens per region, not per pixel: a 3 x 3 grid of overlapping windows, each half the
// frame wide and half (rounded up) high, sliding by half (on the 16 x 9 tile grid: 8 x 5 tiles,
// 28% of the frame; see FlashLimiter.hpp for why).
//
// Per region and per kind the limiter runs a zigzag detector on the luminance it SHOWS: it records
// an event each time the shown value turns back by a full swing from its last extreme. Frames pass
// untouched (bit for bit) until an event would make kFlashEventsPerWindow + 1 events in the
// trailing window; only then is that region held at the luminance it showed last frame (zero
// change, so no event), until the oldest event leaves the window; then the held tiles ease back to
// the drawn picture at kFlashReleaseRate per second.
//
// Why five events and not six: a sequence of k alternating swings inside any window forces at
// least k - 1 detector events inside it (the first swing may have been confirmed just before the
// window opened; the induction is in FlashLimiter.cpp). Five events allow at most six transitions:
// three flashes. Five is also exactly what a 2 Hz full-screen kick needs (events 0.25 s apart: the
// one at the window's far edge plus four), so 120 bpm kicks pass at full strength; 2.5 Hz and up
// (and every strobe) is capped.
constexpr float kFlashSwing = 0.1f;
constexpr float kFlashDarkLimit = 0.8f;
constexpr float kFlashWindow = 1.0f;
constexpr int kFlashEventsPerWindow = 5;
static_assert((kFlashEventsPerWindow + 1) / 2 <= 3,
              "events + 1 transitions per window must stay at or under six: three flashes");
// Saturated red (WCAG's red-flash definition as tools like PEAT apply it): a pixel is saturated red
// when R / (R + G + B) >= kRedSaturation, and its red value is then (R - G - B), else 0, on
// sRGB-decoded linear channels in 0..1. WCAG's scale is that value times 320, and a transition is a
// change of more than 20 on it: kRedSwing = 20 / 320 of the area-averaged red value. Red flashes
// are counted separately from luminance ones, three a second each.
constexpr float kRedSaturation = 0.8f;
constexpr float kRedSwing = 20.0f / 320.0f;
// The red guard: any pixel with R / (R + G + B) above kRedGuardRatio is moved toward the grey of its
// own luminance, s = A / (A + (3 T - 1) Y) of the way (A = R - T (R + G + B), Y its luminance), which
// lands it on exactly T. Its red value becomes 0 and its luminance does not change, so guarding red
// never makes a luminance flash of its own; saturated red turns a muted rose. T sits 0.02 under the
// gate so that no later gain, lift or 8-bit rounding can tip a guarded pixel back over it; for the
// same reason every tile the limiter touches at all is guarded, and an untouched tile is a byte copy.
// The limiter can remove red but never add it, so a red event that RAISES red is let through only
// with two slots free (its own and the fall that must follow it); a falling red event always has its.
constexpr float kRedGuardRatio = 0.78f;
// The detector fires this much early (counts a hair more than the definition does), so 16-bit
// rounding of the state can never hide a real transition.
constexpr float kFlashDetectSlack = 1.0e-4f;
// Events up to this much older than the window still count: the same rounding margin on time.
constexpr float kFlashWindowSlack = 2.0e-3f;
// How fast a held tile eases back to the drawn picture once its region may change again.
constexpr float kFlashReleaseRate = 4.0f;
// The longest frame step the limiter credits (the renderer clamps dt to 0.1 s too): a stall never
// ages the event history faster than real time.
constexpr float kFlashMaxDt = 0.1f;
// The tile grid, long side x short side of the frame (16 x 9 in landscape, 9 x 16 in portrait).
constexpr int kFlashGridLong = 16;
constexpr int kFlashGridShort = 9;

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
