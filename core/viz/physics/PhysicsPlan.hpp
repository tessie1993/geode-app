#pragma once
#include <array>

#include "viz/Params.hpp"
#include "viz/frame/Thermal.hpp"

namespace geode::viz::physics {

// The physics layers' pure maths (GLOSSARY "Physics layers"; the GL side is viz/physics/PhysicsLayers). Any style
// can carry two layers over its finished picture, composited back to front:
//
//   back   a low-res fluid (viz/fluid FluidSim). The picture is its ink: each step the dye moves toward the
//          picture's colour where the picture is lit, and the picture's contours stir its velocity, so the
//          style's own shapes swirl their colours out into the dark around them.
//   middle the style, as it drew.
//   front  particles (viz/fluid Particles) that ride the fluid's velocity, or a curl drift when the fluid is
//          off, each coloured from the picture under it.
//
// The fluid's place is a param of its own: 0 behind the style (it shows through the style's darks), 1 over it
// (screened on). Nothing here hears the sound: the music reaches the layers only through their params, which
// waves, envelopes, reaction routes and macro knobs move (the owner's rule, no hardcoded reactions). Everything
// steps on the frame's music clock, so silence holds the layers and an export replays them.

// Below this a layer's mix draws nothing and allocates nothing.
inline constexpr float kOn = 1.0f / 256.0f;
// Frames the grids and targets outlive a layer going off (about two seconds at 60 Hz): a wave or a route that
// dips to 0 each cycle must not free and allocate them every time.
inline constexpr int kIdleFrames = 120;

// What one frame asks of the layers.
struct Plan {
    bool fluid = false;
    bool particles = false;
    float fluidMix = 0.0f;
    float particleMix = 0.0f;
    float order = 0.0f;  // the fluid's place: 0 behind the style .. 1 over it
    float force = 0.0f;  // how hard the picture stirs the fluid and the particles drift, 0..2
    int simRes = 0;      // the fluid's velocity grid, short side
    int dyeRes = 0;      // its dye grid, short side
    int pressureIterations = 0;
    int particleSide = 0;  // the particle state texture's side: side * side particles
    bool any() const { return fluid || particles; }
};

// The plan for the drawn (resolved, safety-clamped) params on a phone at `tier` (ADR 0008: fps first, then render
// scale, then march quality and post FX last). Full and Eased run the fluid on FluidQuality's Low grids; Reduced
// steps down to its Min grids and fewer particles; Minimal, where the post FX go, skips the fluid and keeps a
// small population of particles. An export renders at Full whatever the phone (ThermalGovernor::tier), so its plan
// never depends on how warm the phone was. Both mixes at 0 (every look's default) plan nothing.
Plan planFor(const SceneParams& p, ThermalTier tier);

// How far the dye moves toward the picture's colour in a step of dt: frame-rate independent, so two half steps
// land where one whole step does, and 0 for a stopped clock.
float dyeWeight(float dt);
// The fluid's dissipations per second: the ink fades from where the picture is dark in about two seconds, and the
// stir lets go in about one.
inline constexpr float kDyeDissipation = 0.5f;
inline constexpr float kVelocityDissipation = 1.2f;
// The picture's push on the fluid: sim units per second squared for a whole step of luminance across two cells,
// along the contour (the gradient turned a quarter, so the pressure solve keeps all of it).
float swirlGain(float force);
// The fluid's vorticity confinement at a force.
float curlStrength(float force);
// The particles' curl drift where the fluid is calm or off, sim units per second.
float driftSpeed(float force);

// Where particles are born: eight points over the frame (sim units, x across -aspect..aspect, y -1..1), each with
// weight 1 and a jitter wide enough that the population fills the frame. Four floats a point, the layout
// fluid::Particles::setChoreography reads.
inline constexpr int kSpawnPoints = 8;
std::array<float, kSpawnPoints * 4> spawns(float aspect);

// The particles' stir phase (the curl drift's rotation), on the music clock only.
struct Clock {
    static constexpr float kStirRate = 0.35f;  // radians per second
    float stirTime = 0.0f;
    void step(float dt);
};

// Whether the layers' grids and targets stay this frame: kept while on and for kIdleFrames after.
struct Keep {
    int idle = kIdleFrames + 1;  // nothing to keep before the first frame on
    bool step(bool on);
};

// The composite's maths, which physics_layer_frag.glsl and physics_particle_frag.glsl draw: kept here so the host
// tests hold the layer order, the off case and the bounds.
struct Rgb {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};
float luma(const Rgb& c);
// The fluid under or over the style: behind, the dye fills the style's darks and leaves its lit parts as they
// are; over, it is screened on. `order` blends the two, `fluidMix` fades the whole layer; at 0 the style is
// returned exactly.
Rgb layer(const Rgb& style, const Rgb& dye, float fluidMix, float order);
// One particle laid on the picture in front: screened, so the frame never passes white however many overlap.
Rgb front(const Rgb& base, const Rgb& mote);

}  // namespace geode::viz::physics
