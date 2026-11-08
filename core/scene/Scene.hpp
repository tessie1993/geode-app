#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

#include "api/geode_api.h"
#include "scene/env/Environment.hpp"
#include "viz/CameraRig.hpp"

// geode::scene::Scene (#513, spec #511, building #302): Environment + Objects + Movement + Camera,
// stepped once a frame. Today it holds the environment; the camera is the renderer's one
// scene::camera::SceneCamera (#512), whose pose the Scene is handed each frame and passes on.
//
// WHERE IT RUNS. Each drawn style owns one (viz::ShaderScene builds it when env::has(style)), and the
// same viz::Renderer draws live, export (OffscreenSceneRenderer), wallpaper, dream and Cast, so every
// surface steps the same Scene code on its own frame clock. One per style rather than one per renderer
// because a crossfade draws two styles at once and each must keep its own world.
//
// THE SEED is the play's (SceneHost::playSeed: the auto-director's, or the one an export pinned): a
// new seed is a new play and the environment starts over from it, so a render replays the screen.
//
// Order in step(): (objects, #515) → environment ← camera pose → (director, #519). A style with no
// environment has an empty Scene that steps nothing, and its ShaderScene keeps today's path.
//
// Pure: no GL, no allocation after construction, one thread (the renderer's).

namespace geode::scene {

struct FrameInput {
    const GeodeSoundFrame* sound = nullptr;
    float dt = 0.0f;
    uint64_t playSeed = 0;
    // The camera's pose this frame, stepped before the Scene. Null: no camera (a flat style).
    const viz::CameraPose* camera = nullptr;
    // scene::camera::Mode::SixDof for this style (the catalogue's flag): the camera is free.
    bool freeCamera = false;
    std::array<float, 6> knobs{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    env::Controls controls{};
};

class Scene {
public:
    // The Scene for a style: its family's environment when one is ported (env::make), else none.
    explicit Scene(std::string_view styleId);

    void step(const FrameInput& in);

    bool hasEnvironment() const { return env_ != nullptr; }
    env::Environment* environment() { return env_.get(); }
    const env::Environment* environment() const { return env_.get(); }
    uint64_t seed() const { return seed_; }

private:
    std::unique_ptr<env::Environment> env_;
    uint64_t seed_ = 0;
    bool seeded_ = false;
    // Objects (#515): the pools of shapes, particles, models and lights, stepped before the world and
    // drawn after it as an overlay pass.
    // Director (#519): at a section, a drop or a track boundary it picks what changes (a camera move,
    // new objects, an environment shift) from the music and the seed; it will set EnvInput's moments.
};

}  // namespace geode::scene
