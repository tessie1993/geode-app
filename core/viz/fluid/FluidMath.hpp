#pragma once
#include <utility>
#include <vector>

#include "api/geode_api.h"

namespace geode::viz::fluid {

// The fluid engine's pure maths: everything a frame decides on the CPU before it issues a pass, and
// the scalar kernels the shaders run, written once here so core/tests/fluid_math_test.cpp can hold
// them to their contracts on the host. A function here either has a caller in the engine or is the
// exact mirror of a shader line, named in its comment; the CPU grid references that check whole
// passes (the multigrid against Jacobi, MacCormack against semi-Lagrangian) live in the test itself,
// because shipping them in the library would be shipping dead code.
namespace math {
constexpr float kMinAudioDrive = 0.2f;
constexpr float kMaxAudioDrive = 2.5f;
constexpr float kDriveCeiling = 1.5f;

float driven(float value, float audioDrive);
int stateSide(int count);
// Distance from (px, py) to the segment a-b, and how far along it (0..1) the nearest point lies.
std::pair<float, float> segDist(float ax, float ay, float bx, float by, float px, float py);
// One step of v easing toward the flow at a per-60-fps-frame drag, so the pull is frame-rate independent.
float dragStep(float v, float flow, float drag, float dt = 1.0f / 60.0f);

// ---- Splats --------------------------------------------------------------------------------------

/** Splats one batched pass takes; fluid_splat_batch_frag.glsl's uniform arrays are this long. */
constexpr int kSplatBatch = 16;

/**
 * Past this many radii a splat's falloff is below 1e-5 and the batched pass skips it. The serial
 * pass this replaced evaluated every splat on every texel; the cutoff is what keeps sixteen splats
 * in one pass from costing sixteen exp() per texel across a 1152-line dye grid.
 */
constexpr float kSplatCutoffRadii = 11.5f;

/**
 * The capsule falloff of one splat at a texel `dist` from its swept segment, `along` (0..1) of the
 * way from its previous to its current position: exp(-d/r), tapered 40% toward the head so a fast
 * stroke reads as a comet rather than a sausage. Mirrors fluid_splat_batch_frag.glsl `falloff`.
 */
float splatFalloff(float dist, float radius, float along);

// ---- Advection -----------------------------------------------------------------------------------

/**
 * The MacCormack corrector for one channel (Selle, Fedkiw et al. 2008, "An Unconditionally Stable
 * MacCormack Method"): the forward semi-Lagrangian value plus half the error a backward trace of it
 * finds, clamped to the four source taps the forward trace interpolated between. The clamp is what
 * makes it unconditionally stable: the correction can sharpen an edge but never invent a value the
 * source did not hold, so it cannot ring or overshoot into negative dye. Mirrors fluid_maccormack_frag.
 */
float macCormack(float forward, float source, float backward, float lo, float hi);

// ---- Pressure ------------------------------------------------------------------------------------

/** The damped-Jacobi weight every multigrid smoothing pass uses: 4/5 is the optimum for the 5-point
 *  Laplacian's high-frequency modes in 2D, which is all a smoother is asked to remove. */
constexpr float kSmootherOmega = 0.8f;
/** Coarsening stops before a level's short side would drop under this many cells. */
constexpr int kMinCoarseSide = 8;
/** At most this many levels: the fixed cost of a pass (a render-target switch) dominates below that. */
constexpr int kMaxLevels = 6;
constexpr int kMaxCycles = 2;

struct SolvePlan {
    int levels = 1;        // 1 = no coarse grid; the plan is then plain damped Jacobi
    int cycles = 0;        // V-cycles
    int smooth = 0;        // pre- and post-smoothing passes on every non-coarsest level
    int coarseIterations = 0;
    int extraPasses = 0;   // fine-grid smoothing passes after the last cycle, with what is left
    float cost = 0.0f;     // in full-grid passes, the unit the user's "Solver iterations" counts
    int draws = 0;         // passes issued, of any size
};

/** The grid sizes of a V-cycle over a w x h pressure grid, finest first. Cell-centred: (n + 1) / 2. */
std::vector<std::pair<int, int>> multigridLevels(int w, int h);

/**
 * How to spend `budget` full-grid passes on the pressure solve. "Solver iterations" used to be the
 * Jacobi count; it is now the budget, so the slider keeps both of its meanings - more is more
 * accurate and more is slower, in proportion - while a V-cycle spends the same budget where Jacobi
 * cannot reach: one Jacobi pass moves information one cell, so the large-scale divergence a 288-line
 * grid holds was never touched by 28 of them, and that residue is the soft, compressible sloshing
 * the old solver showed. The cost model counts each pass by the fraction of the fine grid it covers.
 */
SolvePlan planPressureSolve(int budget, int w, int h);

// ---- Sound couplings ------------------------------------------------------------------------------

/**
 * Every sound coupling this engine added in one place, each a named scalar read off a BLEND of the
 * sound frame rather than one trigger, so no single event owns a response: heat is the low end as a
 * whole (bass, sub, kick), the calm-water stir is the top end's texture (treble, hats, noisiness), and
 * so on. Kept as one struct so the coming per-style knobs can replace any of them by name without
 * the passes that read them changing. Each is 0..~1.5, with a floor so silence still moves.
 */
struct Drives {
    float heat = 0.0f;           // temperature injected per splat: bass, sub, kick, a little loudness trend
    float buoyancy = 1.0f;       // multiplier on lift: the loudness trend and how dense the mix is
    float particleStir = 0.0f;   // curl-noise strength for tracers in calm fluid: treble, hats, flatness
    float stirRate = 0.0f;       // how fast that noise churns: mids, spectral flux, run density
    float streak = 1.0f;         // motion-blur length multiplier: onsets, snare, RMS
};

Drives drives(const GeodeSoundFrame& f);

// ---- Time step -----------------------------------------------------------------------------------

/** The longest single step the fluid was tuned for: emitters, buoyancy and vorticity stay stable under it. */
constexpr float kMaxSimStep = 1.0f / 30.0f;
/** At most this many steps a frame, so a stall cannot start a catch-up spiral. */
constexpr int kMaxSubsteps = 4;

/**
 * The simulated time a frame of `dt` gets: all of it, up to kMaxSubsteps * kMaxSimStep (NaN and negative
 * are 0). Before #383 the fluid clamped every frame to kMaxSimStep, so a 24 fps export ran it at 80 %.
 */
float frameSimDt(float dt);

/** A frame's simulated time split into `count` equal steps of `step` seconds, each at most kMaxSimStep. */
struct Substeps {
    int count = 1;
    float step = 0.0f;
};
Substeps substeps(float dt);

// ---- Temperature ---------------------------------------------------------------------------------

/** Heat a texel can hold: injection fades into what is left, like the dye's headroom. */
constexpr float kMaxTemperature = 2.0f;

/**
 * Heat one splat's centre receives this frame from `Drives::heat`: a floor so a quiet passage still
 * has convection, the rest from the low end, so the bass is literally what lifts the smoke. Scaled
 * by dt so it is a rate, not a per-frame amount that would double at 120 Hz.
 */
float heatInjection(float heatDrive, float heatGain, float dt);

/** What the headroom leaves of an injection into a texel already at `temperature`. */
float heatHeadroom(float temperature);

/** The temperature grid's per-step divisor, the same form as the dye's dissipation. */
float coolingDivisor(float cooling, float dt);

/** Upward acceleration, sim units / s^2: the Boussinesq term with the ambient at zero. */
float buoyancyAccel(float buoyancy, float temperature);

// ---- Particles -----------------------------------------------------------------------------------

/**
 * Per-particle light for a population of `count` against the `refCount` it was tuned on. Additive
 * sprites sum, so a population eight times larger at the same brightness is eight times brighter
 * and washes to white; scaling by 1/count instead would keep the total but make each tracer a dim
 * speck. The 0.75 power sits between: more particles read as more detail and a little more light.
 */
float particleExposure(int count, int refCount);

/**
 * One inertial tracer step through `flow` (sim units / s): the midpoint (RK2) rule the update shader
 * runs, mirrored for the host test. `flowAt(x, y)` returns the field there. Euler sampled the field
 * only where a particle started, so in a vortex every step left it on a slightly wider circle and
 * the population spiralled outward into the walls; the midpoint sample cancels that first-order drift.
 */
template <typename Flow>
void tracerStepRk2(float& px, float& py, float& vx, float& vy, float drag, float dt, Flow flowAt) {
    const auto [f0x, f0y] = flowAt(px, py);
    const float kx = dragStep(vx, f0x, drag, dt);
    const float ky = dragStep(vy, f0y, drag, dt);
    const auto [fmx, fmy] = flowAt(px + 0.5f * dt * kx, py + 0.5f * dt * ky);
    vx = dragStep(vx, fmx, drag, dt);
    vy = dragStep(vy, fmy, drag, dt);
    px += vx * dt;
    py += vy * dt;
}

}  // namespace math

// The frame with its levels and bands scaled by the audio-drive control.
GeodeSoundFrame scaledFeatures(const GeodeSoundFrame& features, float audioDrive);

// Water: which dye texels are a catch well, and the well's radius and depth.
namespace water {
constexpr float kMinCatchRadius = 0.03f;
constexpr float kMaxCatchRadius = 0.3f;
constexpr float kRefCatchRadius = 0.12f;
constexpr float kDisplayBrightness = 1.0f;

bool isCatchWell(float r, float g, float b);
float catchWellRadius(float catchRadius);
float catchWellAmplitude(float speed, float catchRadius, float rippleStrength);
}  // namespace water

// Curl flow: beat drive, field amplitude, trail retention and particle brightness.
namespace curl {
constexpr float kMinRetention = 0.6f;
constexpr float kOffRetention = 0.45f;
constexpr float kBaseBrightness = 0.85f;
constexpr float kBeatBrightness = 0.35f;
constexpr float kBaseAmp = 0.55f;
constexpr float kBeatAmp = 0.9f;
/** The population Curl Flow's brightness was tuned on, before its count followed the quality tier. */
constexpr int kRefParticles = 49152;

float beatDrive(float beatEnvelope, float beatResponse);
float fieldAmp(float audioDrive, float beatDrive);
float retention(float trailLength, bool trails = true);
float fadeAlpha(bool trails, float trailLength, float dt);
float warpDecay(float retention, float dt);
float particleBrightness(float beatEnvelope);
}  // namespace curl

}  // namespace geode::viz::fluid
