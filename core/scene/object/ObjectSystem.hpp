#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "scene/camera/Drivers.hpp"
#include "scene/object/ObjectPool.hpp"
#include "scene/object/Spawner.hpp"
#include "signal/events/Event.hpp"

// geode::scene::object's frame (#515): the pool, a family's declaration and its spawners, stepped once a
// frame after the environment, and what the objects hand on: the shapes to draw (packShapes, the
// overlay pass's instance array) and a subject for the camera (cameraTarget).
//
// A frame, in order:
//   1. beginFrame() clears every live object's route modulation; then this frame's routes write theirs
//      through ObjectPool::modulate (a route never outlives its frame, the camera's rule);
//   2. step(): every live object's params advance (glides, trials), and it ages by dt x life.ageRate;
//      from its birth on, its rates are integrated (travel from vel, orientation from spin, the orbit
//      phase from target.orbitRate) over exactly the part of the frame after its birth;
//   3. its life moves on: entry, the death trigger (time, beats, an event), the exit, and its slot is
//      freed when the exit is done;
//   4. every spawner hears the frame's events, in order, and fires on its trigger;
//   5. packShapes() and cameraTarget() read the result, modulation included.
// Everything is a function of the seed, the events and dt, so live and export (the same Renderer path)
// get the same objects, and 30, 60 and 120 fps agree at the frames they share.
//
// Pure: no GL, no allocation after construction, no locks. One thread (the renderer's).

namespace geode::scene::object {

struct FrameIn {
    float dt = 0.0f;
    const signal::EventQueue* events = nullptr;  // this frame's events (core/signal/events), or none
    camera::Pose camera{};                       // this frame's camera, for Camera anchors and emit frames
};

// One shape as the overlay pass uploads it: five vec4s, std140-ready (80 bytes; 64 of them are 5 KB,
// well inside GLES 3's 16 KB minimum uniform block).
struct ShapeGpu {
    float posScale[4];  // world position, drawn scale (scale × the life's scale)
    float orient[4];    // world orientation quaternion x, y, z, w (local → world)
    float size[4];      // shape.size x, y, z, shape.round
    float material[4];  // palette offset, glow, drawn opacity, the environment's material id
    float life[4];      // ShapeType, seconds since birth, burst 0..1, collapse 0..1
};
static_assert(sizeof(ShapeGpu) == 80, "ShapeGpu is five std140 vec4s");

// What an object offers the camera for LockOn, Orbit, Follow and LookAt: where to stand and which way to
// look (a camera::Pose, in the camera's own convention) and how much it asks (its presence: a subject
// fading in or out takes or lets go of the camera gently). core/scene/camera does not read it yet: a
// target driver layer there would blend `pose` by `weight`, under keyframes and takeover.
struct CameraTarget {
    bool valid = false;
    std::uint32_t instance = 0;
    TargetMode mode = TargetMode::None;
    Vec3 subject{0.0f, 0.0f, 0.0f};  // the object's world position
    float radius = 0.0f;             // a bound on its drawn size
    camera::Pose pose{};
    float weight = 0.0f;
};

class ObjectSystem {
public:
    static constexpr std::size_t kMaxSpawners = 8;
    static constexpr std::size_t kMaxStarting = 16;

    explicit ObjectSystem(std::size_t capacity = ObjectPool::kMaxShapesAndModels);

    // A new play: every object goes (the user's too: a preset or project places them again), the firing
    // counts start over on this seed, and the declaration's starting objects are placed.
    void reset(std::uint64_t seed);
    // A family's objects (copied: up to kMaxSpawners rules and kMaxStarting starting objects). The
    // family's and the music's objects go and the new starting objects are placed; the user's stay.
    void declare(const Declaration& d);
    // A user's object: never evicted for another; it lives as its spec says (Death::Never stays until
    // kill()). 0 when refused.
    std::uint32_t place(const ObjectSpec& spec);
    // Starts the object's exit (frees it at once if it was not born yet). False when it is not live.
    bool kill(std::uint32_t instance);

    void beginFrame();
    void step(const FrameIn& in);

    ObjectPool& pool() { return pool_; }
    const ObjectPool& pool() const { return pool_; }
    std::uint64_t seed() const { return seed_; }
    std::size_t spawnerCount() const { return ruleCount_; }
    // How many times rule i has fired since the last reset.
    std::uint32_t fires(std::size_t rule) const { return rule < ruleCount_ ? fires_[rule] : 0; }

    // The drawable shapes (born, visible), slot order, at the camera of the last step. Returns how many
    // were written.
    std::size_t packShapes(ShapeGpu* out, std::size_t max) const;
    // The subject the objects offer a camera at `current`: the first Target-anchored, born object with a
    // target mode, a user's before a family's before the music's, older first.
    CameraTarget cameraTarget(const camera::Pose& current) const;

    // The world pose an object is drawn at (Camera anchors ride the camera of the last step).
    bool worldPose(std::uint32_t instance, Vec3& pos, Quat& orient) const;
    // The look of an object's life now.
    LifeLook look(std::uint32_t instance) const;

private:
    void stepObject(ObjectPool::Slot& s, float dt, const signal::EventQueue* events);
    void fire(std::size_t rule);
    void placeStarting();
    void worldOf(const ObjectPool::Slot& s, Vec3& pos, Quat& orient) const;

    ObjectPool pool_;
    std::uint64_t seed_ = 0;
    std::array<SpawnRule, kMaxSpawners> rules_{};
    std::array<std::uint32_t, kMaxSpawners> matched_{};
    std::array<std::uint32_t, kMaxSpawners> fires_{};
    std::size_t ruleCount_ = 0;
    std::array<ObjectSpec, kMaxStarting> starting_{};
    std::size_t startingCount_ = 0;
    camera::Pose camera_{};
};

}  // namespace geode::scene::object
