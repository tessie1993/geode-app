#include "scene/movement/MovementParams.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace geode::scene::movement {

namespace {

using signal::ParamDef;
using signal::ParamKind;
using signal::ParamSchema;

constexpr float kWorld = 100.0f;  // a centre or a target anywhere within ±100 world units

// Each table's defaults are its struct's defaults (scene_movement_test checks that they agree).
constexpr std::array<ParamDef, kOrbitParamCount> kOrbitRows{{
    {"movement.orbit.centreX", ParamKind::Float, 0.0f, -kWorld, kWorld},
    {"movement.orbit.centreY", ParamKind::Float, 0.0f, -kWorld, kWorld},
    {"movement.orbit.centreZ", ParamKind::Float, 0.0f, -kWorld, kWorld},
    {"movement.orbit.axisX", ParamKind::Float, 0.0f, -1.0f, 1.0f},
    {"movement.orbit.axisY", ParamKind::Float, 1.0f, -1.0f, 1.0f},
    {"movement.orbit.axisZ", ParamKind::Float, 0.0f, -1.0f, 1.0f},
    {"movement.orbit.radius", ParamKind::Float, 1.0f, 0.0f, 50.0f},
    {"movement.orbit.rate", ParamKind::Float, 0.5f, -2.0f * kTwoPi, 2.0f * kTwoPi},
    {"movement.orbit.capture", ParamKind::Float, 0.5f, 0.0f, 10.0f},
}};
constexpr std::array<ParamDef, kDriftParamCount> kDriftRows{{
    {"movement.drift.amplitude", ParamKind::Float, 0.5f, 0.0f, 10.0f},
    {"movement.drift.frequency", ParamKind::Float, 0.5f, 0.01f, 10.0f},
    {"movement.drift.speed", ParamKind::Float, 0.2f, 0.0f, 10.0f},
    {"movement.drift.response", ParamKind::Float, 0.6f, 0.02f, 10.0f},
    {"movement.drift.seed", ParamKind::Int, 1.0f, 0.0f, 65535.0f},
}};
constexpr std::array<ParamDef, kFlockParamCount> kFlockRows{{
    {"movement.flock.radius", ParamKind::Float, 1.5f, 0.05f, 20.0f},
    {"movement.flock.separation", ParamKind::Float, 1.5f, 0.0f, 10.0f},
    {"movement.flock.alignment", ParamKind::Float, 1.0f, 0.0f, 10.0f},
    {"movement.flock.cohesion", ParamKind::Float, 0.5f, 0.0f, 10.0f},
    {"movement.flock.cruise", ParamKind::Float, 1.0f, 0.0f, 20.0f},
    {"movement.flock.minDistance", ParamKind::Float, 0.4f, 0.0f, 10.0f},
    {"movement.flock.maxAccel", ParamKind::Float, 8.0f, 0.0f, 100.0f},
}};
constexpr std::array<ParamDef, kFollowParamCount> kFollowRows{{
    {"movement.follow.targetX", ParamKind::Float, 0.0f, -kWorld, kWorld},
    {"movement.follow.targetY", ParamKind::Float, 0.0f, -kWorld, kWorld},
    {"movement.follow.targetZ", ParamKind::Float, 0.0f, -kWorld, kWorld},
    {"movement.follow.seconds", ParamKind::Float, 0.5f, 0.02f, 10.0f},
    {"movement.follow.damping", ParamKind::Float, 1.0f, 0.05f, 4.0f},
}};
constexpr std::array<ParamDef, kBounceParamCount> kBounceRows{{
    {"movement.bounce.axisX", ParamKind::Float, 0.0f, -1.0f, 1.0f},
    {"movement.bounce.axisY", ParamKind::Float, 1.0f, -1.0f, 1.0f},
    {"movement.bounce.axisZ", ParamKind::Float, 0.0f, -1.0f, 1.0f},
    {"movement.bounce.strength", ParamKind::Float, 1.5f, 0.0f, 20.0f},
    {"movement.bounce.seconds", ParamKind::Float, 0.35f, 0.02f, 5.0f},
    {"movement.bounce.damping", ParamKind::Float, 0.35f, 0.05f, 4.0f},
}};
constexpr std::array<ParamDef, kReactParamCount> kReactRows{{
    {"movement.react.mode", ParamKind::Int, 0.0f, 0.0f, 2.0f},
    {"movement.react.radius", ParamKind::Float, 3.0f, 0.0f, 50.0f},
    {"movement.react.strength", ParamKind::Float, 6.0f, 0.0f, 100.0f},
    {"movement.react.restitution", ParamKind::Float, 0.3f, 0.0f, 1.0f},
}};
constexpr std::array<ParamDef, kMotionParamCount> kMotionRows{{
    {"movement.motion.drag", ParamKind::Float, 0.8f, 0.0f, 20.0f},
    {"movement.motion.maxSpeed", ParamKind::Float, 0.0f, 0.0f, 100.0f},
}};
constexpr std::array<ParamDef, kMorphParamCount> kMorphRows{{
    {"movement.morph.duration", ParamKind::Float, 1.5f, 0.0f, 30.0f},
    {"movement.morph.ease", ParamKind::Int, 1.0f, 0.0f, static_cast<float>(kEaseCount - 1)},
}};
constexpr std::array<ParamDef, kCollapseParamCount> kCollapseRows{{
    {"movement.collapse.duration", ParamKind::Float, 0.8f, 0.01f, 30.0f},
    {"movement.collapse.twist", ParamKind::Float, kTwoPi, -4.0f * kTwoPi, 4.0f * kTwoPi},
    {"movement.collapse.ease", ParamKind::Int, 3.0f, 0.0f, static_cast<float>(kEaseCount - 1)},
    {"movement.collapse.kill", ParamKind::Bool, 1.0f, 0.0f, 1.0f},
}};
constexpr std::array<ParamDef, kSplitParamCount> kSplitRows{{
    {"movement.split.count", ParamKind::Int, 6.0f, 1.0f, static_cast<float>(kMaxSplitChildren)},
    {"movement.split.spread", ParamKind::Float, 0.8f, 0.0f, kPi},
    {"movement.split.speed", ParamKind::Float, 2.0f, 0.0f, 50.0f},
    {"movement.split.speedJitter", ParamKind::Float, 0.25f, 0.0f, 1.0f},
    {"movement.split.childScale", ParamKind::Float, 0.5f, 0.01f, 1.0f},
    {"movement.split.inherit", ParamKind::Float, 1.0f, 0.0f, 2.0f},
    {"movement.split.offset", ParamKind::Float, 0.5f, 0.0f, 4.0f},
    {"movement.split.conserve", ParamKind::Bool, 1.0f, 0.0f, 1.0f},
    {"movement.split.keepParent", ParamKind::Bool, 0.0f, 0.0f, 1.0f},
}};

template <std::size_t N>
ParamSchema schemaOf(const std::array<ParamDef, N>& rows) {
    return ParamSchema(std::vector<ParamDef>(rows.begin(), rows.end()));
}

// Row i of a behaviour's values, through the schema's one clamp; a missing row is its default.
template <std::size_t N>
float at(const std::array<ParamDef, N>& rows, std::span<const float> v, std::size_t i) {
    return ParamSchema::clampTo(rows[i], i < v.size() ? v[i] : rows[i].defaultValue);
}

template <std::size_t N>
int intAt(const std::array<ParamDef, N>& rows, std::span<const float> v, std::size_t i) {
    return static_cast<int>(std::lround(at(rows, v, i)));
}

template <std::size_t N>
bool boolAt(const std::array<ParamDef, N>& rows, std::span<const float> v, std::size_t i) {
    return at(rows, v, i) >= 0.5f;
}

}  // namespace

ParamSchema orbitSchema() { return schemaOf(kOrbitRows); }
ParamSchema driftSchema() { return schemaOf(kDriftRows); }
ParamSchema flockSchema() { return schemaOf(kFlockRows); }
ParamSchema followSchema() { return schemaOf(kFollowRows); }
ParamSchema bounceSchema() { return schemaOf(kBounceRows); }
ParamSchema reactSchema() { return schemaOf(kReactRows); }
ParamSchema motionSchema() { return schemaOf(kMotionRows); }
ParamSchema morphSchema() { return schemaOf(kMorphRows); }
ParamSchema collapseSchema() { return schemaOf(kCollapseRows); }
ParamSchema splitSchema() { return schemaOf(kSplitRows); }

OrbitParams orbitParams(std::span<const float> v) {
    const auto& r = kOrbitRows;
    OrbitParams p;
    p.centre = {at(r, v, kOrbitCentreX), at(r, v, kOrbitCentreY), at(r, v, kOrbitCentreZ)};
    p.axis = {at(r, v, kOrbitAxisX), at(r, v, kOrbitAxisY), at(r, v, kOrbitAxisZ)};
    p.radius = at(r, v, kOrbitRadius);
    p.rate = at(r, v, kOrbitRate);
    p.capture = at(r, v, kOrbitCapture);
    return p;
}

DriftParams driftParams(std::span<const float> v) {
    const auto& r = kDriftRows;
    DriftParams p;
    p.amplitude = at(r, v, kDriftAmplitude);
    p.frequency = at(r, v, kDriftFrequency);
    p.speed = at(r, v, kDriftSpeed);
    p.response = at(r, v, kDriftResponse);
    p.seed = static_cast<std::uint32_t>(intAt(r, v, kDriftSeed));
    return p;
}

FlockParams flockParams(std::span<const float> v) {
    const auto& r = kFlockRows;
    FlockParams p;
    p.radius = at(r, v, kFlockRadius);
    p.separation = at(r, v, kFlockSeparation);
    p.alignment = at(r, v, kFlockAlignment);
    p.cohesion = at(r, v, kFlockCohesion);
    p.cruise = at(r, v, kFlockCruise);
    p.minDistance = at(r, v, kFlockMinDistance);
    p.maxAccel = at(r, v, kFlockMaxAccel);
    return p;
}

FollowParams followParams(std::span<const float> v) {
    const auto& r = kFollowRows;
    FollowParams p;
    p.target = {at(r, v, kFollowTargetX), at(r, v, kFollowTargetY), at(r, v, kFollowTargetZ)};
    p.seconds = at(r, v, kFollowSeconds);
    p.damping = at(r, v, kFollowDamping);
    return p;
}

BounceParams bounceParams(std::span<const float> v) {
    const auto& r = kBounceRows;
    BounceParams p;
    p.axis = {at(r, v, kBounceAxisX), at(r, v, kBounceAxisY), at(r, v, kBounceAxisZ)};
    p.strength = at(r, v, kBounceStrength);
    p.seconds = at(r, v, kBounceSeconds);
    p.damping = at(r, v, kBounceDamping);
    return p;
}

ReactParams reactParams(std::span<const float> v) {
    const auto& r = kReactRows;
    ReactParams p;
    p.mode = intAt(r, v, kReactMode);
    p.radius = at(r, v, kReactRadius);
    p.strength = at(r, v, kReactStrength);
    p.restitution = at(r, v, kReactRestitution);
    return p;
}

MotionParams motionParams(std::span<const float> v) {
    const auto& r = kMotionRows;
    MotionParams p;
    p.drag = at(r, v, kMotionDrag);
    p.maxSpeed = at(r, v, kMotionMaxSpeed);
    return p;
}

MorphParams morphParams(std::span<const float> v) {
    const auto& r = kMorphRows;
    MorphParams p;
    p.duration = at(r, v, kMorphDuration);
    p.ease = intAt(r, v, kMorphEase);
    return p;
}

CollapseParams collapseParams(std::span<const float> v) {
    const auto& r = kCollapseRows;
    CollapseParams p;
    p.duration = at(r, v, kCollapseDuration);
    p.twist = at(r, v, kCollapseTwist);
    p.ease = intAt(r, v, kCollapseEase);
    p.kill = boolAt(r, v, kCollapseKill);
    return p;
}

SplitParams splitParams(std::span<const float> v) {
    const auto& r = kSplitRows;
    SplitParams p;
    p.count = intAt(r, v, kSplitCount);
    p.spread = at(r, v, kSplitSpread);
    p.speed = at(r, v, kSplitSpeed);
    p.speedJitter = at(r, v, kSplitSpeedJitter);
    p.childScale = at(r, v, kSplitChildScale);
    p.inherit = at(r, v, kSplitInherit);
    p.offset = at(r, v, kSplitOffset);
    p.conserve = boolAt(r, v, kSplitConserve);
    p.keepParent = boolAt(r, v, kSplitKeepParent);
    return p;
}

}  // namespace geode::scene::movement
