#include "viz/Overlays.hpp"

namespace geode::viz {

void Overlays::recreate() {
    release();
    flow_.emplace(loader_);
    flow_->create();
}

void Overlays::resize(int width, int height) {
    if (flow_) flow_->resize(width, height);
}

void Overlays::release() {
    if (flow_) flow_->release();
    flow_.reset();
}

void Overlays::stepFlow(const GeodeSoundFrame& features, float dt, const SceneParams& p) {
    if (flow_) flow_->step(features, dt, p);
}

}  // namespace geode::viz
