#pragma once
#include <functional>
#include <utility>
#include <vector>

#include "api/geode_api.h"

namespace geode::viz::fluid::ripple {

// Port of RippleMath.kt.
constexpr float kRefractionCap = 0.08f;
constexpr float kMaxHeight = 8.0f;
constexpr float kHeightDecayRatio = 0.35f;

struct StrokeDrop {
    float x;
    float y;
    float radius;
    float amplitude;
};

float heightDecayPerSubstep(float damping, float subDt);
float dropProfile(float dist, float radius, float amp);
float cflClampedDt(float c, float dt, float dx);
std::pair<float, float> refractionOffset(float hL, float hR, float hT, float hB, float strength);
float inkDissipation(float dissipation, float dt);
std::vector<StrokeDrop> strokeDrops(float x, float y, float dx, float dy, float dt, float radius, float strength);
std::pair<float, float> overlayDropPosition(int index, float aspect);

// Port of RippleOverlayDrops.kt. Wave three: rings ring phase-locked to the
// bar oscillator (one per bar, at its peak) instead of on a heard transient;
// sparkles run continuously at a rate set by the relative treble level. This
// class is fed straight from Overlays::stepRippleOverlay (out of this unit's
// scope), which has no MotionField instance to hand it, so it keeps its own
// small self-contained copy of the relative-level/rhythm-lock math (see
// viz/MotionField.hpp) rather than reading a beat/transient flag.
class OverlayDrops {
public:
    static constexpr int kBeatDrops = 2;

    using Queue = std::function<void(float x, float y, float radius, float amp)>;

    void reset();
    // dt defaults to a nominal 60fps frame for any caller that cannot supply
    // the real one; Overlays::stepRippleOverlay passes the renderer's actual
    // frame dt.
    void tick(const GeodeFeatureFrame& features, float aspect, const Queue& queue, float dt = 1.0f / 60.0f);

private:
    static constexpr float kAvgSeconds = 20.0f;
    static constexpr float kRhythmLockSeconds = 1.0f;
    static constexpr float kTwoPi = 6.2831853f;

    int frame_ = 0;
    int dropIndex_ = 0;
    float bassAvg_ = 1.0f;
    float trebAvg_ = 1.0f;
    float bassWarm_ = 0.0f;
    float trebWarm_ = 0.0f;
    float rhythmLock_ = 0.0f;
    float prevBarOsc_ = 0.5f;
    bool barOscRising_ = false;
    float sparklePhase_ = 0.0f;
};

}  // namespace geode::viz::fluid::ripple
