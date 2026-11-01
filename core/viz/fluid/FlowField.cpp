#include "viz/fluid/FlowField.hpp"

#include <algorithm>

namespace geode::viz::fluid {

void FlowField::create() {
    release();
    sim_.simRes = kGridRes;
    sim_.pressureIterations = 12;
    sim_.create();
}

void FlowField::step(const GeodeSoundFrame& features, float dt, const SceneParams& p) {
    if (!sim_.available()) return;
    sim_.curlStrength = std::clamp(p.flowCurl, 0.0f, 50.0f);
    emitters_.forceScale = std::clamp(p.flowForce, 0.0f, 3.0f);
    emitters_.stirrerSpeed = std::clamp(p.speed, 0.1f, 2.0f);
    const float simDt = std::clamp(dt, 0.0f, 1.0f / 30.0f);
    emitters_.tick(features, simDt, sim_.aspect(), 0.0f, 1.0f, splats_);
    for (const Splat& s : splats_) sim_.queueSplat(s);
    sim_.step(simDt);
}

}  // namespace geode::viz::fluid
