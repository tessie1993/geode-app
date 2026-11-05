#pragma once

#include "scene/generator/Slot.hpp"
#include "signal/params/ParamSchema.hpp"

// The generators' params, declared through Group A's schema (core/signal/params) like the camera's
// (core/scene/camera/CameraParams), so each is a route target with a base and a modulation. Rows are in
// the order of GeneratorParam, so a GeneratorParam is the row's ParamId.
//
// The id a slot shows is a string and lives in the Assignment, never here: a float param cannot name a
// style. The pins are rows as well as Assignment flags so a saved look and the shell read one schema;
// the mix is a row so a route can fade a generator in and out.

namespace geode::scene::generator {

enum GeneratorParam : signal::ParamId {
    kPolicy = 0,  // Int 0..1: Policy (OnlyMe, DirectorSwaps); default OnlyMe
    kPinSky,      // Bool, one per Slot in Slot order
    kPinWall,
    kPinMaterial,
    kPinPlane,
    kMixSky,      // Float 0..1, one per Slot: how much of the generator shows over the surface's own look
    kMixWall,
    kMixMaterial,
    kMixPlane,
    kGeneratorParamCount,
};

constexpr GeneratorParam pinParam(Slot s) { return static_cast<GeneratorParam>(kPinSky + static_cast<int>(s)); }
constexpr GeneratorParam mixParam(Slot s) { return static_cast<GeneratorParam>(kMixSky + static_cast<int>(s)); }

// The schema, one row per GeneratorParam. Ids are stable ASCII: "scene.generators.policy",
// "scene.generators.pin.<slot>", "scene.generators.mix.<slot>" (slotName).
signal::ParamSchema generatorSchema();

}  // namespace geode::scene::generator
