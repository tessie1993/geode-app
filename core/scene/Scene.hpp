#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

#include "api/geode_api.h"
#include "scene/env/Environment.hpp"
#include "scene/object/ObjectOverlay.hpp"
#include "scene/object/ObjectSystem.hpp"
#include "signal/events/Event.hpp"
#include "signal/events/SoundEvents.hpp"
#include "viz/CameraRig.hpp"

// geode::scene::Scene (#513, spec #511, building #302): Environment + Objects + Movement + Camera,
// stepped once a frame. It holds the environment and the objects; the camera is the renderer's one
// scene::camera::SceneCamera (#512), whose pose the Scene is handed each frame and passes on.
//
// WHERE IT RUNS. Each drawn style owns one (viz::ShaderScene builds it when env::has(style)), and the
// same viz::Renderer draws live, export (OffscreenSceneRenderer), wallpaper, dream and Cast, so every
// surface steps the same Scene code on its own frame clock. One per style rather than one per renderer
// because a crossfade draws two styles at once and each must keep its own world.
//
// THE SEED is the play's (SceneHost::playSeed: the auto-director's, or the one an export pinned): a
// new seed is a new play and the environment and the objects start over from it, so a render replays
// the screen.
//
// Order in step(): environment ← camera pose → objects (#515) → (director, #519). The objects step
// after the world so a spawn on this frame's drop is drawn this frame, by the overlay pass
// (core/viz/frame/ObjectOverlayPass) after the world is drawn. Their movement is #516's steppers over
// each object's Body; the events they hear are this Scene's own SoundEvents over the frame it is given,
// a pure function of those frames, so live and export raise the same events at the same frames. A style
// with no environment has an empty Scene that steps nothing, and its ShaderScene keeps today's path.
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
    // The style's palette this frame (what its own program was given), for the objects' material.
    object::OverlayPalette palette{};
};

// The scene camera's pose in the objects' convention (a position, a local → world quaternion, focal).
camera::Pose objectCamera(const viz::CameraPose& pose);

class Scene {
public:
    // The Scene for a style: its family's environment when one is ported (env::make), else none, and
    // the family's objects (env::Environment::objects) declared once.
    explicit Scene(std::string_view styleId);

    void step(const FrameInput& in);

    bool hasEnvironment() const { return env_ != nullptr; }
    env::Environment* environment() { return env_.get(); }
    const env::Environment* environment() const { return env_.get(); }
    uint64_t seed() const { return seed_; }

    object::ObjectSystem& objects() { return objects_; }
    const object::ObjectSystem& objects() const { return objects_; }
    // This frame's events (raised from the sound frame by step()).
    const signal::EventQueue& events() const { return events_; }
    // What the overlay pass draws the objects with: the world's look and the style's palette, as of
    // the last step().
    object::OverlayLook objectLook() const { return env_ ? env_->objectLook() : object::OverlayLook{}; }
    const object::OverlayPalette& palette() const { return palette_; }
    // Packs the overlay's instance block (object::packOverlay) at the pool's live cap.
    int packOverlay(object::OverlayFrame& out) const;

private:
    std::unique_ptr<env::Environment> env_;
    uint64_t seed_ = 0;
    bool seeded_ = false;
    // Objects (#515): the shapes (and, from #522, models) a family's spawners and the user place,
    // stepped after the world and drawn after it as an overlay pass. Particles (#517) and lights (#518)
    // get their own pools here.
    object::ObjectSystem objects_;
    signal::SoundEvents detector_;
    signal::EventQueue events_;
    double clock_ = 0.0;
    object::OverlayPalette palette_{};
    // Director (#519): at a section, a drop or a track boundary it picks what changes (a camera move,
    // new objects, an environment shift) from the music and the seed; it will set EnvInput's moments.
};

}  // namespace geode::scene
