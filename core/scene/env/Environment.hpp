#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

#include "api/geode_api.h"
#include "scene/object/ObjectOverlay.hpp"
#include "scene/object/Spawner.hpp"
#include "signal/params/ParamStore.hpp"
#include "viz/CameraRig.hpp"

// geode::scene::env: the environment interface (#513, spec #511, building #302). An Environment is
// one family's raymarched world: one shader algorithm (a lib_<family> world() function and the C++
// that drives its numbers), with each style of the family a variant of it (its own params and wiring).
// The Scene (core/scene/Scene.hpp) owns one, steps it once a frame and hands it the camera's pose.
//
// THE PATTERN is Morph World's (#314, #348): a fixed uniform block the shader's world() reads (the
// World Genome there, uWorld[18]), a pure C++ driver that eases that block from the sound, the
// controls and a per-play seed, and a CPU port of world() so host tests can probe the space. Morph
// World is the first environment (MorphWorldEnv.hpp, which wraps WorldGenome and MorphDriver rather
// than forking them); each family port implements this interface the same way.
//
// WHAT AN ENVIRONMENT PROMISES:
//   - reset(seed) then the same EnvInput sequence and dts give the same uniforms(): live and export
//     drive the same code on their own frame clocks, and a pinned seed replays the film.
//   - step() is allocation-free and runs on the renderer's thread.
//   - sixDof() true means the world is valid for any camera position and orientation: geometry, fog
//     and sky in every direction, and at least eyeRadius() of open air around the eye whatever the
//     world morphs to. sdf() is the proof host tests read (sdf(eye) >= eyeRadius(), and open along every
//     ray out to it). An environment that is not sixDof() keeps its style on the catalogue's rails.
//   - params() are core/signal rows (base vs this frame's modulation, one clamp); a route writes
//     modulation before step(), and step() clears it, so a route never outlives its frame.

namespace geode::scene::env {

using Vec3 = std::array<float, 3>;

// The shape controls every family reads (SceneParams' Customize controls; presets save them there).
// The Scene copies them in each frame; an environment takes them as its params' base values.
struct Controls {
    float morph = 0.0f;
    float warp = 0.0f;
    float turbulence = 0.0f;
    float detail = 1.0f;     // SceneParams::marchDetail, catalogue::kMarchDetail
    float intensity = 1.0f;
};

struct EnvInput {
    // This frame's sound (the renderer's, with the style's band trims). Null steps on silence.
    const GeodeSoundFrame* sound = nullptr;
    // The six style knobs (uKnob: k0 PULSE .. k5 SPACE), 0.5 when Note & key reactions are off.
    std::array<float, 6> knobs{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    Controls controls{};
    // This frame's musical moments, as levels (an environment takes the edges itself). The Scene sets
    // them from the sound frame today; the scene director (#519) may set them instead.
    bool section = false;
    bool drop = false;
    // The camera this frame, already stepped (scene::camera::SceneCamera's pose). Null: no camera.
    const viz::CameraPose* camera = nullptr;
    // The camera is free (scene::camera::Mode::SixDof, the catalogue's flag). False is the rails
    // flight, where an environment draws exactly what it drew before #513.
    bool freeCamera = false;
};

// The world's uniform block, as the shader declares it (uniform vec4 <name>[vec4Count]).
struct UniformBlock {
    const char* name = nullptr;
    const float* data = nullptr;
    int vec4Count = 0;
};

class Environment {
public:
    virtual ~Environment() = default;

    // The family world ("morph_world") and the style variant this instance draws (its catalogue id).
    virtual std::string_view family() const = 0;
    virtual std::string_view variant() const = 0;

    // A new play: everything the world draws from its seed starts over from this one.
    virtual void reset(uint64_t seed) = 0;
    virtual uint64_t seed() const = 0;

    virtual void step(const EnvInput& in, float dt) = 0;

    // What the shader reads this frame, valid until the next step().
    virtual UniformBlock uniforms() const = 0;

    virtual signal::ParamStore& params() = 0;
    virtual const signal::ParamStore& params() const = 0;

    // The world's signed distance at p as the shader sees it this frame (positive in the air), on the
    // CPU, for host tests. Not for the frame path.
    virtual float sdf(const Vec3& p) const = 0;
    // The air always kept around the eye.
    virtual float eyeRadius() const = 0;
    // True when the world is valid for any camera pose (see above).
    virtual bool sixDof() const = 0;

    // A GL fact the drawing scene reports: its baked assets (textures) are bound, or no longer are.
    virtual void setAssetsBound(bool bound) { (void) bound; }

    // Objects (#515). The family's starting objects and spawners (static storage, read once when the
    // Scene is built; a style only tunes their numbers), and the world's material and view this frame,
    // which the overlay pass lights and turns the shapes by so they belong to the world. The defaults
    // are no objects and a neutral look.
    virtual object::Declaration objects() const { return {}; }
    virtual object::OverlayLook objectLook() const { return {}; }
};

// The environment for a style, or null when its family has no environment yet (it keeps today's path).
std::unique_ptr<Environment> make(std::string_view styleId);
// Whether make() returns one for this style.
bool has(std::string_view styleId);

}  // namespace geode::scene::env
