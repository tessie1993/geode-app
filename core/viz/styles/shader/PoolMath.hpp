#pragma once
#include <array>
#include <optional>

// Pool: a swimming pool's tiled floor seen through its moving water, from above or at a slant. The
// surface is a closed form of the clock and the fingers, never a simulated field, so nothing can
// feed back into it or ring on:
//
//   h(p) = travelling sine waves  +  ripple rings from three wandering sources  +  finger wakes
//
// This file owns every number of it: the waves' directions, wavenumbers and phases (deep-water
// dispersion on the frame clock, so silence stops them and Speed paces them), the ring sources, the
// wakes, the view and the look the controls ask for. pool_frag draws the same sums per pixel and
// scales each group by what lib_sound hears (swell, chop, rings), each gain in 0..1; every bound
// below holds at gain 1. `p` is in surface units on the water plane, y away from the eye.
namespace geode::viz::pool {

constexpr int kWaves = 8;
constexpr int kSwellWaves = 4;
constexpr int kRings = 3;
constexpr int kMaxWakes = 6;
constexpr int kMaxFingers = 5;

// Heights at full sound; each group's weights sum to 1, so these are ceilings.
constexpr float kSwellHeight = 0.035f;
constexpr float kChopHeight = 0.009f;
constexpr float kRingHeight = 0.012f;
constexpr float kWakeHeight = 0.030f;

constexpr float kRingK = 10.0f;        // ring wavenumber
constexpr float kRingReach = 0.7f;     // the Gaussian window's sigma
constexpr float kRingSoft = 0.05f;     // smooths r at the source, so the centre has no cusp
constexpr float kRingOmega = 2.6f;     // rad/s at Speed 1

constexpr float kWakeSigma0 = 0.12f;
constexpr float kWakeSpread = 0.22f;   // sigma's growth, units per second
constexpr float kWakeTauSeconds = 1.1f;
constexpr float kWakeLifeSeconds = 5.0f;
constexpr float kWakeSpacing = 0.18f;  // a dragged finger leaves one this far apart
constexpr float kWakeMinInterval = 0.10f;

// Caustics add at most kCausticCeiling of luminance and take at most kCausticShade of that away, so
// their whole swing fits one safety step (safety::kMaxFlashDepth) whatever the surface does.
constexpr float kCausticCeiling = 0.20f;
constexpr float kCausticShade = 0.25f;
constexpr float kCausticSwing = kCausticCeiling * (1.0f + kCausticShade);
constexpr float kFocusFloor = 0.2f;

constexpr float kMaxRefraction = 0.12f;  // surface units: the furthest the floor is seen from where it is
constexpr float kMaxSpeed = 4.0f;        // the Speed control's ceiling (ParamTable.inc)

// The eye: kEyeHeight above the water, looking down, tilted toward the far end by `tilt` radians.
constexpr float kEyeHeight = 1.6f;
constexpr float kFieldOfView = 0.75f;  // screen units to ray slope
constexpr float kHorizon = 0.02f;      // a ray climbing slower than this never meets the water

struct Wave {
    float kx;
    float ky;
    float phase;
    float amp;
};
struct Ring {
    float x;
    float y;
    float phase;
    float amp;
};
struct Wake {
    float x;
    float y;
    float sigma;
    float amp;
};

struct Look {
    float refraction = 0.0f;  // surface units of floor shift per unit of slope
    float focus = 0.0f;       // the floor's optical distance, which scales the curvature
    float caustics = 0.0f;    // luminance at a full focus, at most kCausticCeiling
    float shade = kCausticShade;
};

struct View {
    float sinTilt = 0.0f;
    float cosTilt = 1.0f;
    float height = kEyeHeight;
    float fov = kFieldOfView;
};

struct Surface {
    std::array<Wave, kWaves> waves{};
    std::array<Ring, kRings> rings{};
    std::array<Wake, kMaxWakes> wakes{};
    Look look;
    View view;
};

// lib_sound's share of each group, 0..1, as pool_frag reads it.
struct Gains {
    float swell = 1.0f;
    float chop = 1.0f;
    float rings = 1.0f;
};

struct Hessian {
    float xx;
    float xy;
    float yy;
};

float height(const Surface& s, const Gains& g, float x, float y);
std::array<float, 2> gradient(const Surface& s, const Gains& g, float x, float y);
Hessian hessian(const Surface& s, const Gains& g, float x, float y);
// Where the floor under p is seen from: p plus this, capped at kMaxRefraction.
std::array<float, 2> refraction(const Surface& s, const Gains& g, float x, float y);
// The luminance the caustics add on the floor under p: within [-shade * caustics, caustics].
float causticLuma(const Surface& s, const Gains& g, float x, float y);
// Where a screen point (aspect applied, y up, -1..1 tall) meets the water, or nothing above the horizon.
std::optional<std::array<float, 2>> surfacePoint(const View& v, float sx, float sy);
// The fastest any term of the surface repeats at a fixed point, in Hz, at the given Speed.
float fastestHz(float speed);

// Customize's reach: the four shader-look controls, Speed, and the six knobs (0.5 = untouched). Reduced
// motion arrives once, already in Speed and Turbulence (safety::apply scales both).
struct Controls {
    float morph = 0.0f;       // the view: 0 a slant along the pool, 1 straight down
    float warp = 0.0f;        // depth: how far the floor bends and the caustics focus
    float turbulence = 0.0f;  // the chop
    float intensity = 1.0f;   // the caustics' brightness, inside their ceiling
    float speed = 1.0f;
    std::array<float, 6> knobs{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
};

View viewFor(const Controls& c);
float wakeAmp(float strength, float age);
float wakeSigma(float age);

// The surface's state: wave and ring phases, the wandering sources and the live wakes.
class Pool {
public:
    void reset();
    // `dt` is the frame clock's step (0 in silence). `fingers` are touch points in y-up NDC (x, y) with
    // `aspect` = width / height; a finger that lands drops a wake, a dragged one leaves a trail of them.
    void step(float dt, const Controls& c, const float* fingers, int fingerCount, float aspect);
    Surface surface(const Controls& c) const;
    int wakeCount() const;

private:
    struct Live {
        float x = 0.0f;
        float y = 0.0f;
        float age = 0.0f;
        float strength = 0.0f;
    };
    struct Finger {
        float x = 0.0f;
        float y = 0.0f;
        float since = 0.0f;
        bool down = false;
    };

    void spawn(float x, float y, float strength);

    std::array<float, kWaves> phase_{};
    std::array<float, kRings> ringPhase_{};
    float wander_ = 0.0f;
    std::array<Live, kMaxWakes> wakes_{};
    std::array<Finger, kMaxFingers> fingers_{};
};

}  // namespace geode::viz::pool
