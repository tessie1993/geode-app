#include "scene/env/MorphWorldEnv.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "viz/Catalogue.hpp"
#include "viz/world/WorldAssets.hpp"

namespace geode::scene::env {

namespace {

namespace world = viz::world;

float clampDt(float dt) { return std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.1f) : 0.0f; }

float ease(float v, float target, float tau, float dt) { return v + (target - v) * (1.0f - std::exp(-dt / tau)); }

float smoothstep(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

}  // namespace

signal::ParamSchema morphWorldSchema() {
    using signal::ParamDef;
    using signal::ParamKind;
    // The shape controls' ranges are the ones MorphDriver already clamps them to, so declaring them
    // here changes no value the driver sees. Intensity is brightness-like (event pulses rate-limited).
    std::vector<ParamDef> rows{
        {"env.morph", ParamKind::Float, 0.0f, 0.0f, 1.0f},
        {"env.warp", ParamKind::Float, 0.0f, 0.0f, 1.0f},
        {"env.turbulence", ParamKind::Float, 0.0f, 0.0f, 1.5f},
        {"env.detail", ParamKind::Float, 1.0f, viz::catalogue::kMarchDetail.min, viz::catalogue::kMarchDetail.max},
        {"env.intensity", ParamKind::Float, 1.0f, 0.0f, 4.0f, true},
        {"env.corridor", ParamKind::Bool, 1.0f, 0.0f, 1.0f},
        {"env.repeatFloor", ParamKind::Float, 0.25f, 0.0f, 1.0f},
    };
    return signal::ParamSchema(std::move(rows));
}

MorphWorldEnv::MorphWorldEnv(std::string variant) : variant_(std::move(variant)), store_(morphWorldSchema()) {}

void MorphWorldEnv::reset(uint64_t seed) {
    driver_.reset(seed);
    // MorphDriver::reset forgets the GL fact; the textures are still bound, so say so again.
    driver_.setAssetsBound(assetsBound_);
    hasEye_ = false;
    eye_ = {0.0f, 0.0f, 0.0f};
    vel_ = {0.0f, 0.0f, 0.0f};
    tube_ = 0.0f;
    publish(false);
}

void MorphWorldEnv::setAssetsBound(bool bound) {
    assetsBound_ = bound;
    driver_.setAssetsBound(bound);
    // The driver publishes it on its next step; the block this frame already says so.
    uniforms_[world::Assets] = world::hasShapes() && bound ? 1.0f : 0.0f;
}

void MorphWorldEnv::step(const EnvInput& in, float dt) {
    dt = clampDt(dt);
    store_.advance(dt);
    // The shape controls are SceneParams' (what Customize and presets set); a route modulates on top.
    const float controls[5] = {in.controls.morph, in.controls.warp, in.controls.turbulence, in.controls.detail,
                               in.controls.intensity};
    for (signal::ParamId id = kMwMorph; id <= kMwIntensity; ++id) {
        store_.input(id, signal::ParamInput::set(controls[id], signal::Origin::Engine));
    }

    world::DriverInput d = in.sound ? world::DriverInput::fromSound(*in.sound) : world::DriverInput{};
    d.section = in.section;
    d.drop = in.drop;
    d.morph = store_.effective(kMwMorph);
    d.warp = store_.effective(kMwWarp);
    d.turbulence = store_.effective(kMwTurbulence);
    d.detail = store_.effective(kMwDetail);
    d.intensity = store_.effective(kMwIntensity);
    d.knobs = in.knobs;
    driver_.step(d, dt);

    if (in.camera != nullptr) {
        // The camera was stepped before this: the world's z phase and wake follow the eye.
        driver_.travel(in.camera->pos, dt);
        Vec3 delta{0.0f, 0.0f, 0.0f};
        if (hasEye_) {
            for (int i = 0; i < 3; ++i) delta[i] = in.camera->pos[i] - eye_[i];
            // The rig's travel wraps; the heading must not see the jump.
            const float wrap = viz::CameraRig::kTravelWrap;
            if (delta[2] > wrap * 0.5f) delta[2] -= wrap;
            if (delta[2] < -wrap * 0.5f) delta[2] += wrap;
        }
        hasEye_ = true;
        eye_ = in.camera->pos;
        if (dt > 0.0f) {
            for (int i = 0; i < 3; ++i) vel_[i] = ease(vel_[i], std::clamp(delta[i] / dt, -20.0f, 20.0f), 0.4f, dt);
        }
    }

    // The tube is an option for forward flight only: along +z and moving. A turn of the heading, a
    // hover or a slide closes it, gently.
    const float speed = std::sqrt(vel_[0] * vel_[0] + vel_[1] * vel_[1] + vel_[2] * vel_[2]);
    const float along = speed > 1e-4f ? vel_[2] / speed : 0.0f;
    const float want = (store_.effective(kMwCorridor) >= 0.5f ? 1.0f : 0.0f) * smoothstep(0.5f, 0.85f, along) *
                       smoothstep(0.15f, 0.6f, speed);
    tube_ = dt > 0.0f ? ease(tube_, want, kTubeEase, dt) : tube_;

    publish(in.freeCamera);
    store_.clearModulation();
}

void MorphWorldEnv::publish(bool freeCamera) {
    uniforms_ = driver_.uniforms();
    view_ = driver_.live();
    way_ = driver_.way();
    if (!freeCamera) return;
    const float floor = store_.effective(kMwRepeatFloor);
    view_[world::RepXY] = std::max(view_[world::RepXY], floor);
    view_[world::Corridor] = tube_ * view_[world::Corridor] + (1.0f - tube_) * kTubeClosed;
    uniforms_[world::RepXY] = view_[world::RepXY];
    uniforms_[world::Corridor] = view_[world::Corridor];
}

UniformBlock MorphWorldEnv::uniforms() const { return {"uWorld", uniforms_.data(), world::kVec4}; }

float MorphWorldEnv::sdf(const Vec3& p) const { return world::sdf(view_, way_, p); }

}  // namespace geode::scene::env
