#pragma once

#include <cstdint>

#include "scene/Math.hpp"
#include "scene/camera/CameraMath.hpp"
#include "scene/movement/Body.hpp"
#include "scene/object/ObjectParams.hpp"
#include "signal/events/Event.hpp"
#include "signal/params/ParamStore.hpp"

// geode::scene::object: the base object every kind extends (#515, spec #511, building #302's shared
// properties). An object is three things:
//   - its params (ObjectParams.hpp): what it is, every property a route target;
//   - its Body (movement::Body): where it is this frame, integrated from those params by ObjectSystem;
//   - its Life: how far into its entry, its life and its exit it is.
// Values that describe an object to create (a family's starting objects, a spawner's template, a
// user's placement) are an ObjectSpec, written into a fresh store by writeSpec().
//
// Pure: no GL, no allocation, no locks. One thread (the renderer's).

namespace geode::scene::object {

// The kinematic state is #516's movement::Body (pos, vel, orient, spin, scale, age, alive), so an
// object hands its body straight to the movement steppers. For an object:
//   pos     position in the anchor's frame: the pos params + the travel integrated from vel
//   vel     this frame's velocity in the anchor's frame (the vel params)
//   orient  the integrated spin (rot is laid on top when the object is drawn)
//   spin    this frame's angular velocity, rad/s about the anchor frame's axes (the spin params),
//           turned by movement::turnAndAge's rule (exact for a constant spin)
//   scale   this frame's scale param (Life multiplies it when drawn)
//   age     seconds of life since spawn, at life.ageRate
//   alive   false once the slot is free
using movement::Body;

// SDF primitives (textbook distance functions, Hart 1996 and Quilez's catalogue of exact forms). What
// shape.size means for each:
//   Sphere      x = radius
//   Box         x, y, z = half extents
//   Torus       x = ring radius, y = tube radius (ring in the object's XZ plane)
//   Capsule     x = radius, y = half the segment's length (along the object's Y)
//   Cylinder    x = radius, y = half height (along Y)
//   Octahedron  x = centre-to-vertex
// shape.round rounds every one (its distance less the radius). Append only: the value is saved.
enum class ShapeType : std::uint8_t { Sphere = 0, Box, Torus, Capsule, Cylinder, Octahedron };
constexpr int kShapeTypeCount = 6;

// How an object comes in and leaves (life.entry runs one backwards, life.exit forwards):
//   Fade      opacity to 0
//   Collapse  scale to 0, folding in on itself (the shader reads the progress as an implode)
//   Burst     thrown out to 1 + burstSpread times its scale while it fades (the shader shatters it)
enum class Exit : std::uint8_t { Fade = 0, Collapse, Burst };
constexpr int kExitCount = 3;

// What ends a life. Never is a user's or a family's persistent object: only kill() ends it.
enum class Death : std::uint8_t { Time = 0, Beats, Event, Never };
constexpr int kDeathCount = 4;

// Where an object's pos and vel are measured:
//   World   in the world
//   Camera  in the camera's own frame (X right, Y up, Z forward): it rides with the camera
//   Target  in the world, and offered to the camera as a subject (target.mode says how)
enum class Anchor : std::uint8_t { World = 0, Camera, Target };
constexpr int kAnchorCount = 3;

// What a Target-anchored object offers the camera (ObjectSystem::cameraTarget):
//   LookAt  the camera flies its own path and turns to look at it
//   LockOn  the camera closes to target.distance along its line of sight and holds it centred
//   Orbit   the camera circles it at target.distance and target.height, target.orbitRate round
//   Follow  the camera trails it at target.distance behind its motion, target.height up
enum class TargetMode : std::uint8_t { None = 0, LookAt, LockOn, Orbit, Follow };
constexpr int kTargetModeCount = 5;

// Who made an object. Music objects are short-lived (a pool past its cap drops the oldest of them
// first); Starting (a family's) and User objects persist.
enum class Origin : std::uint8_t { Starting = 0, Music, User };

// A whole object to create. Defaults are objectSchema()'s defaults.
struct ObjectSpec {
    Kind kind = Kind::Shape;
    // Transform
    Vec3 pos{0.0f, 0.0f, 0.0f};
    Vec3 vel{0.0f, 0.0f, 0.0f};
    camera::Look rot{};
    Vec3 spin{0.0f, 0.0f, 0.0f};
    float scale = 1.0f;
    // Material
    float palette = 0.0f;
    float glow = 0.0f;
    float opacity = 1.0f;
    int material = 0;
    // Life
    float birth = 0.0f;
    float lifetime = 4.0f;
    float fadeIn = 0.25f;
    float fadeOut = 0.5f;
    float fadeCurve = 1.0f;
    float ageRate = 1.0f;
    Death death = Death::Time;
    int deathBeats = 8;
    signal::EventKind deathEvent = signal::EventKind::Drop;
    Exit entry = Exit::Fade;
    Exit exit = Exit::Fade;
    float burstSpread = 1.5f;
    // Anchor
    Anchor anchor = Anchor::World;
    TargetMode target = TargetMode::None;
    float targetDistance = 3.0f;
    float targetHeight = 0.5f;
    float targetOrbitRate = 0.4f;
    // Shape
    ShapeType shape = ShapeType::Sphere;
    Vec3 size{0.5f, 0.5f, 0.5f};
    float round = 0.0f;
};

// Writes every row of the spec into a store of its kind's schema, as the engine (Origin::Engine), after
// letting go of any finger, trial or timeline left on the rows by the slot's last object, so the store
// is at rest on the spec's values with no modulation. Never allocates.
void writeSpec(const ObjectSpec& spec, signal::ParamStore& store);

enum class Phase : std::uint8_t {
    Waiting,   // spawned, before life.birth: drawn at nothing
    Entering,  // inside life.fadeIn
    Living,
    Exiting,   // the exit is running
};

struct Life {
    Phase phase = Phase::Waiting;
    float exitAge = 0.0f;  // age units since the death trigger (Exiting only)
    int beats = 0;         // Beat events heard since birth
};

// What the life does to the drawn object this frame.
struct LifeLook {
    float opacity = 0.0f;   // multiplies the material's opacity
    float scale = 0.0f;     // multiplies the scale
    float burst = 0.0f;     // 0..1, a Burst's progress (in or out): the shader shatters by it
    float collapse = 0.0f;  // 0..1, a Collapse's progress: the shader implodes by it
};

// The fade's shape: t mixed toward smoothstep(t) by curve (0..1); t clamped to 0..1.
float fadeCurve(float t, float curve);

// The look of a life from the object's effective params and its age. Pure: the same age gives the same
// look at any frame rate.
LifeLook lifeLook(const signal::ParamStore& params, const Life& life, float age);

}  // namespace geode::scene::object
