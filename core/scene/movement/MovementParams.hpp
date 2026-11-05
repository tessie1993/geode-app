#pragma once

#include <span>

#include "scene/movement/Forces.hpp"
#include "scene/movement/Kinematics.hpp"
#include "scene/movement/Shape.hpp"
#include "scene/movement/Split.hpp"
#include "signal/params/ParamSchema.hpp"

// Every behaviour's numbers as Group A's param rows (core/signal/params, #413), so each is a route
// target with a base and this frame's modulation, clamped once by its range. A behaviour declares its
// rows here; the object (#515) that carries it owns a ParamStore over that schema per instance, so its
// rows are addressed as param.object.<instance>.<row id> and the route target lists are generated from
// these declarations. Movement adds no param system of its own.
//
// Row ids are stable ASCII, "movement.<behaviour>.<number>": save keys and wire names. Rows are in the
// order of each behaviour's enum, so an enum value is the row's ParamId. Rates (orbit's rate, drift's
// speed) are integrated by their steppers, never applied as offsets, so a route on a rate never jumps.
//
// Each *Params(values) reads a behaviour's struct from a store's effective values in schema order
// (ParamStore::effectiveAll). A missing row takes its default and every value passes the schema's one
// clamp, so a short or stale vector still gives a usable struct. It allocates nothing: call it per frame.

namespace geode::scene::movement {

using signal::ParamId;

enum OrbitParam : ParamId {
    kOrbitCentreX = 0, kOrbitCentreY, kOrbitCentreZ, kOrbitAxisX, kOrbitAxisY, kOrbitAxisZ,
    kOrbitRadius, kOrbitRate, kOrbitCapture, kOrbitParamCount,
};
enum DriftParam : ParamId {
    kDriftAmplitude = 0, kDriftFrequency, kDriftSpeed, kDriftResponse, kDriftSeed, kDriftParamCount,
};
enum FlockParam : ParamId {
    kFlockRadius = 0, kFlockSeparation, kFlockAlignment, kFlockCohesion, kFlockCruise, kFlockMinDistance,
    kFlockMaxAccel, kFlockParamCount,
};
enum FollowParam : ParamId {
    kFollowTargetX = 0, kFollowTargetY, kFollowTargetZ, kFollowSeconds, kFollowDamping, kFollowParamCount,
};
enum BounceParam : ParamId {
    kBounceAxisX = 0, kBounceAxisY, kBounceAxisZ, kBounceStrength, kBounceSeconds, kBounceDamping,
    kBounceParamCount,
};
enum ReactParam : ParamId {
    kReactMode = 0, kReactRadius, kReactStrength, kReactRestitution, kReactParamCount,
};
enum MotionParam : ParamId {
    kMotionDrag = 0, kMotionMaxSpeed, kMotionParamCount,
};
enum MorphParam : ParamId {
    kMorphDuration = 0, kMorphEase, kMorphParamCount,
};
enum CollapseParam : ParamId {
    kCollapseDuration = 0, kCollapseTwist, kCollapseEase, kCollapseKill, kCollapseParamCount,
};
enum SplitParam : ParamId {
    kSplitCount = 0, kSplitSpread, kSplitSpeed, kSplitSpeedJitter, kSplitChildScale, kSplitInherit,
    kSplitOffset, kSplitConserve, kSplitKeepParent, kSplitParamCount,
};

signal::ParamSchema orbitSchema();
signal::ParamSchema driftSchema();
signal::ParamSchema flockSchema();
signal::ParamSchema followSchema();
signal::ParamSchema bounceSchema();
signal::ParamSchema reactSchema();
signal::ParamSchema motionSchema();
signal::ParamSchema morphSchema();
signal::ParamSchema collapseSchema();
signal::ParamSchema splitSchema();

OrbitParams orbitParams(std::span<const float> values);
DriftParams driftParams(std::span<const float> values);
FlockParams flockParams(std::span<const float> values);
FollowParams followParams(std::span<const float> values);
BounceParams bounceParams(std::span<const float> values);
ReactParams reactParams(std::span<const float> values);
MotionParams motionParams(std::span<const float> values);
MorphParams morphParams(std::span<const float> values);
CollapseParams collapseParams(std::span<const float> values);
SplitParams splitParams(std::span<const float> values);

}  // namespace geode::scene::movement
