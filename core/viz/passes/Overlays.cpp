#include "viz/passes/Overlays.hpp"

namespace geode::viz {

void Overlays::recreate() {
    release();
    wanted_ = true;
}

fluid::FlowField* Overlays::useFlow() {
    if (!flow_ && wanted_) {
        flow_.emplace(loader_);
        flow_->create();
        if (width_ > 0 && height_ > 0) flow_->resize(width_, height_);
    }
    return flow();
}

void Overlays::resize(int width, int height) {
    width_ = width;
    height_ = height;
    if (flow_) flow_->resize(width, height);
}

void Overlays::release() {
    if (flow_) flow_->release();
    flow_.reset();
    wanted_ = false;
}

void Overlays::stepFlow(const GeodeSoundFrame& features, float dt, const SceneParams& p, bool live) {
    if (flow_) flow_->step(features, dt, p, live);
}

}  // namespace geode::viz
