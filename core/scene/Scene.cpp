#include "scene/Scene.hpp"

#include "scene/Math.hpp"
#include "scene/movement/Body.hpp"

namespace geode::scene {

camera::Pose objectCamera(const viz::CameraPose& pose) {
    camera::Pose c;
    c.pos = pose.pos;
    c.orient = fromBasis(pose.right, pose.up, pose.fwd);
    c.focal = pose.focal;
    return c;
}

Scene::Scene(std::string_view styleId) : env_(env::make(styleId)) {
    if (env_) objects_.declare(env_->objects());
}

void Scene::step(const FrameInput& in) {
    if (!env_) return;
    // A new seed is a new play: the world and its objects start over from it.
    if (!seeded_ || in.playSeed != seed_) {
        env_->reset(in.playSeed);
        objects_.reset(in.playSeed);
        detector_.reset();
        clock_ = 0.0;
        seed_ = in.playSeed;
        seeded_ = true;
    }
    palette_ = in.palette;

    env::EnvInput e;
    e.sound = in.sound;
    e.knobs = in.knobs;
    e.controls = in.controls;
    // The music's own moments; the director (#519) will decide these instead.
    e.section = in.sound != nullptr && in.sound->sectionChange > 0.5f;
    e.drop = in.sound != nullptr && in.sound->drop > 0.5f;
    e.camera = in.camera;
    e.freeCamera = in.freeCamera;
    env_->step(e, in.dt);

    // Objects, after the world: this frame's events from the sound frame, then the pool steps and the
    // spawners fire at the camera of this frame.
    const float dt = movement::clampDt(in.dt);
    clock_ += dt;
    events_.clear();
    if (in.sound != nullptr) detector_.detect(*in.sound, clock_, dt, events_);
    object::FrameIn o;
    o.dt = dt;
    o.events = &events_;
    if (in.camera != nullptr) o.camera = objectCamera(*in.camera);
    objects_.beginFrame();
    objects_.step(o);

    // The director (#519) reads what the frame did here.
}

int Scene::packOverlay(object::OverlayFrame& out) const {
    return object::packOverlay(objects_, objectLook(), palette_, objects_.pool().liveCap(), out);
}

}  // namespace geode::scene
