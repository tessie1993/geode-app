#pragma once

#include <cstdint>
#include <random>

#include "viz/ThermalGovernor.hpp"

// Liquid mode (Customize › Advanced › Liquid): every style becomes a liquid that keeps its own look.
//
// The pipeline (viz/liquid/LiquidPass): the style draws its frame as always; a feedback buffer, the
// liquid, is carried forward every frame by the flow field's velocity and by the dive, and the style's
// frame is poured back into it. Nothing here knows any style: it sits between the scene target and the
// composite, so every family (and an export) goes liquid the same way.
//
// Why the dive is a feedback zoom and not a force in the fluid: a steady outflow from the vanishing
// point is divergent, and the solver's pressure projection exists to remove divergence; it would cancel
// the dive within a frame. So the solver carries the swirl, the pushes and the sound's stirring, and the
// dive is a per-frame magnification about the vanishing point applied in the liquid's own advection,
// MilkDrop's endless-zoom idiom: a feedback zoom never arrives and never needs EndlessZoom's two
// octave layers, because each frame only ever samples the one before it.
//
// This file is the pure part (no GL), so core/tests/liquid_test.cpp can pin it on the host.
namespace geode::viz::liquid {

// Customize › Liquid › Born inside: what forms at the vanishing point and swells outward as you dive.
enum class Born { Live = 0, Rerolled = 1, NextStyle = 2, Forms = 3 };
// Customize › Liquid › Colour age: what happens to liquid as it ages and streams past.
enum class Age { Pure = 0, FadeDeep = 1, InkBleed = 2 };
// Customize › Liquid › Surface.
enum class Surface { Ink = 0, Gloss = 1, Glass = 2 };
// Settings › Performance › Liquid on a hot phone.
enum class HotPolicy { Simplify = 0, Full = 1, Pause = 2 };
// What the liquid actually runs this frame; Kotlin reads it back to say so (never silently off).
enum class Mode { Off = 0, Full = 1, Lite = 2, Warp = 3, Paused = 4 };

Born bornFrom(int v);
Age ageFrom(int v);
Surface surfaceFrom(int v);
HotPolicy hotPolicyFrom(int v);

// The thermal tier and the user's hot-phone policy decide the mode. Full and Eased always run the whole
// liquid; Reduced simplifies (Lite) unless the user asked for Full; Minimal falls back to a refraction
// warp (Simplify), keeps going (Full) or pauses (Pause).
Mode modeFor(ThermalTier tier, HotPolicy policy);

struct Plan {
    int velocityGrid = 0;      // the flow field's grid height, cells; 0 runs no solver
    float liquidScale = 1.0f;  // the liquid buffer's share of the render size
    bool feedback = false;     // carry the liquid frame to frame; false is the warp fallback
    bool macCormack = false;   // second-order velocity advection
};
Plan planFor(Mode mode);

// Share of the style's own frame poured into the liquid this frame, frame-rate independent. At
// Liquidity 0 the style is half re-drawn every 60 Hz frame (a short smear); at 1 only ~1.2%, so the
// liquid holds for seconds and the style mostly enters through the vanishing point.
float injectWeight(float liquidity, float dt);

// The vanishing point's own disc: born content fills it, the live style is kept out of it.
float bornRadius(float liquidity);

// One frame of the dive: the magnification about the vanishing point (>= 1), the twist this frame
// (radians) and where the vanishing point is, in centred units (x times the aspect, y -1..1).
struct DiveFrame {
    float scale = 1.0f;
    float twist = 0.0f;
    float vpX = 0.0f;
    float vpY = 0.0f;
};

// The dive's course: truly random each play (the user's call), so it never repeats a pattern. Every
// few seconds it picks a new place for the vanishing point to wander to, a new surge and, now and
// then, a twist; it eases toward each so nothing jumps. The bass reaches it through EndlessZoom's
// musicRate (the caller multiplies it into octavesPerSecond), two fingers steer it.
class Dive {
public:
    static constexpr float kWander = 0.22f;      // farthest the vanishing point wanders from centre
    static constexpr float kMaxTwist = 0.5f;     // radians per second at the most
    static constexpr float kSteerReach = 0.6f;   // farthest two fingers can pull it
    static constexpr float kMinCourse = 2.5f;    // seconds between new courses, shortest
    static constexpr float kMaxCourse = 7.0f;

    explicit Dive(uint64_t seed = 1) { reseed(seed); }
    void reseed(uint64_t seed);

    // `steerWeight` 0..1 is how firmly two fingers hold it (TouchField strength); reduced motion halves
    // the speed and takes the twist and the wander away.
    DiveFrame step(float dt, float octavesPerSecond, float steerX, float steerY, float steerWeight, bool reducedMotion);

    float vpX() const { return vpX_; }
    float vpY() const { return vpY_; }

private:
    void pickCourse();
    float uniform(float lo, float hi);

    std::mt19937 rng_;
    float courseLeft_ = 0.0f;
    float targetX_ = 0.0f;
    float targetY_ = 0.0f;
    float targetTwist_ = 0.0f;
    float targetSurge_ = 1.0f;
    float vpX_ = 0.0f;
    float vpY_ = 0.0f;
    float twist_ = 0.0f;
    float surge_ = 1.0f;
};

// One finger: a drag pushes (the stroke's velocity), holding still spins a whirlpool that grows the
// longer it is held. Two fingers: their centre steers the dive. Fed TouchField's live points.
struct GestureOut {
    bool push = false;
    float x = 0.0f;  // centred NDC, y up
    float y = 0.0f;
    float vx = 0.0f;  // NDC per second
    float vy = 0.0f;
    float swirl = 0.0f;  // whirlpool strength 0..1 at (x, y), signed by the finger's last turn
    float steerWeight = 0.0f;
    float steerX = 0.0f;
    float steerY = 0.0f;
};

class Gestures {
public:
    static constexpr float kStillSpeed = 0.35f;  // NDC/s below which a finger counts as held still
    static constexpr float kSwirlRise = 1.2f;    // seconds of holding to a full whirlpool
    static constexpr float kSwirlFall = 0.4f;    // seconds to calm once moved or lifted
    static constexpr float kMaxSpeed = 8.0f;

    // count: fingers down; xy: their positions (centred NDC, y up), count * 2 floats.
    GestureOut step(int count, const float* xy, float dt);
    void reset();

private:
    bool had_ = false;
    float lastX_ = 0.0f;
    float lastY_ = 0.0f;
    float swirl_ = 0.0f;
    float spinSign_ = 1.0f;
    float steer_ = 0.0f;
    float steerX_ = 0.0f;
    float steerY_ = 0.0f;
};

// When the born look changes (Re-rolled, Next style): on each 8-bar phrase or new section, or, while
// the beat tracker is unsure, after a random 8-16 s. The change dips the born disc out and back in over
// kFade seconds so the swap happens while nothing is showing it.
class BornClock {
public:
    static constexpr float kFade = 0.45f;
    static constexpr float kMinFallback = 8.0f;
    static constexpr float kMaxFallback = 16.0f;

    explicit BornClock(uint64_t seed = 7) : rng_(static_cast<uint32_t>(seed ^ (seed >> 32))) { rearm(); }
    void reseed(uint64_t seed);

    // True on the frame the new look should be swapped in (at the bottom of the dip). `trigger` is the
    // sound frame's phrase or sectionChange; `locked` its beat confidence above the lock.
    bool step(float dt, bool trigger, bool locked);
    // The born disc's gain this frame, 0..1.
    float gain() const { return gain_; }
    uint64_t rollSeed() const { return rollSeed_; }

private:
    void rearm();

    std::mt19937 rng_;
    float fallback_ = 0.0f;
    float dip_ = -1.0f;  // seconds into a dip; < 0 none
    float gain_ = 1.0f;
    uint64_t rollSeed_ = 1;
};

}  // namespace geode::viz::liquid
