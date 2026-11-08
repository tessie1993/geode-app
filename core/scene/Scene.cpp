#include "scene/Scene.hpp"

namespace geode::scene {

Scene::Scene(std::string_view styleId) : env_(env::make(styleId)) {}

void Scene::step(const FrameInput& in) {
    if (!env_) return;
    // A new seed is a new play: the world starts over from it.
    if (!seeded_ || in.playSeed != seed_) {
        env_->reset(in.playSeed);
        seed_ = in.playSeed;
        seeded_ = true;
    }

    // Objects (#515) step here, before the world, once they exist.

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

    // The director (#519) reads what the frame did here.
}

}  // namespace geode::scene
