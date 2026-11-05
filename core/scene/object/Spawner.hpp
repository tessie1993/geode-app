#pragma once

#include <cstddef>
#include <cstdint>

#include "scene/camera/Drivers.hpp"
#include "scene/object/Object.hpp"
#include "signal/events/Event.hpp"

// Spawners and a family's object declaration (#515). A style family's C++ says which objects its world
// starts with and which rules the music spawns more by; a style only tunes their numbers. A rule fires
// on a sound trigger (a core/signal event: beat, drop, section change, onset, ...), throws `count`
// objects from an emit shape, and gives each a short life. Music objects are Origin::Music, so a full
// pool drops them first.
//
// Deterministic: every random draw of a firing comes from (the play's seed, the rule's index, how many
// times it has fired), never from a clock or a frame count, so the same seed and the same events give
// the same objects at any frame rate and on every surface.

namespace geode::scene::object {

// Where a rule's emit point and directions are measured:
//   World        in the world
//   CameraFront  in the camera's frame at the moment it fires, then left in the world (World anchor):
//                thrown into view, and the camera flies on past them
//   Camera       in the camera's frame, and they stay there (Camera anchor): they ride with the view
enum class EmitFrame : std::uint8_t { World = 0, CameraFront, Camera };

// The emit shape: where each object starts and which way it is thrown (at `speed`, outward).
//   Point   at the centre, each thrown a random way
//   Sphere  on a sphere of `radius` round the centre, thrown outward
//   Ring    on a ring of `radius` in the frame's XY plane (facing the camera in CameraFront), evenly
//           spaced with a little jitter, thrown outward
//   Box     anywhere in the cube of half side `radius`, thrown away from the centre
enum class EmitShape : std::uint8_t { Point = 0, Sphere, Ring, Box };

struct SpawnRule {
    signal::EventKind trigger = signal::EventKind::Drop;
    float minStrength = 0.0f;  // an event weaker than this does not count
    int everyNth = 1;          // fire on the 1st, (n+1)th, ... matching event
    int count = 3;             // objects per firing
    int countJitter = 0;       // plus 0..countJitter more, drawn per firing
    int maxAlive = 16;         // this rule's own objects alive at once; past it a firing throws fewer

    EmitFrame frame = EmitFrame::CameraFront;
    EmitShape shape = EmitShape::Sphere;
    Vec3 centre{0.0f, 0.0f, 4.0f};
    float radius = 0.6f;
    float speed = 1.2f;         // world units/s outward
    float speedJitter = 0.3f;   // ± fraction
    float spinMax = 1.5f;       // rad/s, a random axis each

    float lifetime = 2.0f;        // seconds
    float lifetimeJitter = 0.25f; // ± fraction
    float scaleJitter = 0.25f;    // ± fraction of the template's scale
    float paletteSpread = 0.15f;  // 0..1 drawn onto the template's palette offset
    float stagger = 0.0f;         // seconds between one object's birth and the next's
    // Which ShapeTypes a firing draws from (bit = 1 << type); 0 keeps the template's.
    std::uint32_t shapeMask = 0;

    // Everything a rule does not draw: kind, material, fades, exits, death, sizes.
    ObjectSpec base{};
};

// A family's objects. The arrays are the family's (static storage); ObjectSystem copies what it keeps.
struct Declaration {
    const ObjectSpec* starting = nullptr;
    std::size_t startingCount = 0;
    const SpawnRule* spawners = nullptr;
    std::size_t spawnerCount = 0;
};

// The seed of one firing: (play seed, rule index, firing index) through an integer hash.
std::uint32_t firingSeed(std::uint64_t playSeed, std::size_t rule, std::uint32_t firing);

// How many objects a firing throws (count plus its jitter draw), before the caps.
int firingCount(const SpawnRule& rule, std::uint32_t seed);

// Object `index` of a firing: the rule's template with this child's draws applied, placed by the emit
// frame (the camera pose is the one at the firing). Pure.
ObjectSpec emitOne(const SpawnRule& rule, std::uint32_t seed, int index, int count, const camera::Pose& cameraAtFiring);

}  // namespace geode::scene::object
