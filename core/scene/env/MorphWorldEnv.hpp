#pragma once

#include <array>
#include <string>

#include "scene/env/Environment.hpp"
#include "viz/world/MorphDriver.hpp"
#include "viz/world/WorldGenome.hpp"

// Morph World as the first environment (#513): MorphDriver and WorldGenome (#314, #348) wrapped,
// never forked. The driver still chooses the worlds, glides between them and sums the wobble; this
// class feeds it from EnvInput and its params, moves it along with the camera, and makes what it
// uploads valid for a free 6-DoF camera.
//
// WHY 6-DoF NEEDS MORE THAN THE DRIVER. lib_world was built for forward flight along +z: its corridor
// is a tube about the travel axis, and a world with little x/y repeat is one column of structures
// round that axis. A free camera turns round, looks up and down, and may slide off the axis. So,
// with the camera free (EnvInput::freeCamera), two fields of the uploaded block are rewritten, the
// same two in the genome sdf() probes, so the CPU and the shader still agree:
//   - Corridor: the tube is kept only as an option while the camera flies forward (along +z, at
//     speed). Its weight eases with the heading; at 0 the tube's radius is kTubeClosed, so far below
//     zero that the carve never reaches it. The eye bubble and the wake (a capsule along wherever the
//     eye is heading) are always carved, and they hold in every direction: smax never lies below max,
//     so the eye keeps kEyeRadius of air whatever the world, the look or the glide.
//   - RepXY: at least env.repeatFloor, so the x/y cells repeat and the world is a 3D lattice round
//     the eye: a sideways or upward look meets structure in the fog, never an empty column's void.
//     A world that repeats more already is untouched, so the look stays the world's.
// Everything else (the z cells, already relative to the eye with a seamless phase; the wake; the
// roll and the lens, which turn only the ray) was direction-free to begin with.
//
// On rails (freeCamera false) the uploaded block is the driver's exactly, so the style draws as it
// did before this class.
//
// Params (core/signal): the five shape controls, which the Scene sets as base each frame from
// SceneParams (so Customize, presets and macros reach them as before) and routes may modulate, plus
// this world's own two: env.corridor (the tube option) and env.repeatFloor.
//
// Objects (#515): one spawner, fired by the music's drop: three to five of the six SDF shapes thrown
// out from a point ahead of the camera at the drop, in the world's material, growing in from nothing
// and leaving by Burst after about two seconds. The camera flies on past them.

namespace geode::scene::env {

enum MorphWorldParam : signal::ParamId {
    kMwMorph = 0,
    kMwWarp,
    kMwTurbulence,
    kMwDetail,
    kMwIntensity,
    kMwCorridor,     // Bool: keep the travel-axis tube while the camera flies forward (free camera only)
    kMwRepeatFloor,  // 0..1: the least x/y repeat a free camera sees
    kMorphWorldParamCount,
};

signal::ParamSchema morphWorldSchema();

class MorphWorldEnv final : public Environment {
public:
    // The tube's radius when it is off: far enough below zero that the carve ignores it.
    static constexpr float kTubeClosed = -4.0f;
    // How fast the tube's weight follows the heading, seconds.
    static constexpr float kTubeEase = 0.6f;

    explicit MorphWorldEnv(std::string variant);

    std::string_view family() const override { return "morph_world"; }
    std::string_view variant() const override { return variant_; }

    void reset(uint64_t seed) override;
    uint64_t seed() const override { return driver_.seedValue(); }
    void step(const EnvInput& in, float dt) override;
    UniformBlock uniforms() const override;
    signal::ParamStore& params() override { return store_; }
    const signal::ParamStore& params() const override { return store_; }
    float sdf(const Vec3& p) const override;
    float eyeRadius() const override { return viz::world::kEyeRadius; }
    bool sixDof() const override { return true; }
    void setAssetsBound(bool bound) override;
    // The tracer bullet of #515: a drop throws a few shapes into view, which burst on their way out.
    object::Declaration objects() const override;
    // The palette position, emission, gain, fog, roll and lens the world drew with this frame.
    object::OverlayLook objectLook() const override;

    // For tests and tools: the driver underneath, and what the shader's world() sees this frame.
    const viz::world::MorphDriver& driver() const { return driver_; }
    const viz::world::Genome& view() const { return view_; }
    const viz::world::Way& way() const { return way_; }
    // The tube's weight, 0 (bubble and wake only) .. 1 (the driver's corridor).
    float tubeWeight() const { return tube_; }

private:
    void publish(bool freeCamera);

    std::string variant_;
    signal::ParamStore store_;
    viz::world::MorphDriver driver_;
    std::array<float, viz::world::kUniformCount> uniforms_{};
    viz::world::Genome view_{};
    viz::world::Way way_{};
    // The camera's motion, read from its pose (the driver keeps its own for the wake).
    bool hasEye_ = false;
    Vec3 eye_{0.0f, 0.0f, 0.0f};
    Vec3 vel_{0.0f, 0.0f, 0.0f};
    float tube_ = 0.0f;
    bool assetsBound_ = false;
};

}  // namespace geode::scene::env
