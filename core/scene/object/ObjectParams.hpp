#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "signal/params/ParamSchema.hpp"

// geode::scene::object's params (#515, spec #511, building #302): every property of every object is a
// row of Group A's schema (core/signal/params), so every one is a route target with a base and this
// frame's modulation, clamped once by its range. The Scene adds no param system of its own.
//
// One schema per kind: the base rows every kind shares (ObjectParam, rows 0..kBaseParamCount-1), then
// the kind's own rows (ShapeParam for Shape, from kBaseParamCount on). So an ObjectParam is the same
// ParamId in every kind's schema, and a kind's row is its enum value.
//
// Row ids are local and stable ASCII ("pos.x", "shape.type"): save keys. A route addresses one row of one
// object as `param.object.<instance>.<row id>`, the instance being the object's serial (ObjectPool hands
// them out from 1, never reused while the pool lives): formatTarget() and parseTarget() below.
//
// Rates (vel.*, spin.*, target.orbitRate, life.ageRate) are integrated over dt by ObjectSystem, never
// applied as offsets, so changing a speed never makes an object jump. Offsets (pos.*, rot.*) are held:
// the object sits at pos + the travel its velocity has integrated, turned by its integrated spin and
// then by rot.

namespace geode::scene::object {

// The kinds. Shape is the first; Particles (#517), Light (#518) and Model (#522) take their own rows and
// (Particles, Light) their own pools. Append only: the value is saved with a user's objects.
enum class Kind : std::uint8_t { Shape = 0, Particles = 1, Light = 2, Model = 3 };
constexpr int kKindCount = 4;

// The base rows, shared by every kind.
enum ObjectParam : signal::ParamId {
    // Transform
    kPosX = 0, kPosY, kPosZ,          // world units, in the anchor's frame: held offset
    kVelX, kVelY, kVelZ,              // world units/s in the anchor's frame: integrated into travel
    kRotYaw, kRotPitch, kRotRoll,     // radians, a turn held on top of the integrated spin
    kSpinX, kSpinY, kSpinZ,           // rad/s about the anchor frame's axes (movement::Body's spin): integrated
    kScale,                           // uniform scale
    // Material: the environment's, so a shape is made of the style's own stuff
    kPalette,                         // 0..1 offset on the environment's palette (circular)
    kGlow,                            // emissive added to the lit colour (luminance)
    kOpacity,                         // 0..1 (luminance)
    kMaterial,                        // Int: which of the environment's materials (its uniforms say what each is)
    // Life
    kBirth,                           // seconds after spawn before the entry starts (spawners stagger with it)
    kLifetime,                        // seconds from birth to the death trigger, for death = Time
    kFadeIn,                          // seconds the entry takes
    kFadeOut,                         // seconds the exit takes
    kFadeCurve,                       // 0 linear .. 1 smoothstep: the fade's shape, in and out
    kAgeRate,                         // how fast the object's age runs (1 = seconds): integrated
    kDeath,                           // Int: Death
    kDeathBeats,                      // Int: beats from birth, for death = Beats
    kDeathEvent,                      // Int: a signal::EventKind, for death = Event
    kEntry,                           // Int: Exit, run backwards on the way in
    kExit,                            // Int: Exit
    kBurstSpread,                     // how far a Burst throws the object out (times its scale)
    // Anchor and camera target
    kAnchor,                          // Int: Anchor
    kTargetMode,                      // Int: TargetMode, what a Target-anchored object offers the camera
    kTargetDistance,                  // world units the camera keeps from it (LockOn, Orbit, Follow)
    kTargetHeight,                    // world units above it (Orbit, Follow)
    kTargetOrbitRate,                 // rad/s round it (Orbit): integrated
    kBaseParamCount,
};

// Shape's own rows, after the base rows.
enum ShapeParam : signal::ParamId {
    kShapeType = kBaseParamCount,     // Int: ShapeType
    kShapeSizeX,                      // per type, see ShapeType
    kShapeSizeY,
    kShapeSizeZ,
    kShapeRound,                      // world units the surface is rounded by (an SDF's r subtracted)
    kShapeParamEnd,
};
constexpr std::size_t kShapeParamCount = kShapeParamEnd;

// The schema of a kind: the base rows, then its own. Built once per kind, function-static, so a pool's
// stores are copied from it at construction and never after. A kind with no rows of its own yet
// (Particles, Light, Model) gets the base rows alone.
const signal::ParamSchema& objectSchema(Kind kind);

// Route addressing: `param.object.<instance>.<row id>`.
constexpr std::string_view kTargetPrefix = "param.object.";

struct TargetAddress {
    std::uint32_t instance = 0;  // 0 is never an object
    std::string_view param;      // the row id, a view into the parsed text
};

// Writes the address into `out` (NUL-terminated when cap > 0) and returns its length, or 0 when it does
// not fit (nothing usable is written then).
std::size_t formatTarget(char* out, std::size_t cap, std::uint32_t instance, std::string_view paramId);
// The instance and row id of an address, or nothing when it is not one: a missing prefix, an instance
// that is not a plain decimal in 1..2^32-1, or an empty row id.
std::optional<TargetAddress> parseTarget(std::string_view address);

}  // namespace geode::scene::object
