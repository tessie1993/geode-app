#include "viz/fluid/FlowField.hpp"
#include "viz/fluid/FluidMath.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz::fluid {

void FlowField::create() {
    release();
    sim_.simRes = kGridRes;
    sim_.pressureIterations = 12;
    sim_.create();
}

void FlowField::queueKick(float clipX, float clipY, float velX, float velY, float radius) {
    if (!sim_.available()) return;
    // The sim's x runs over the aspect, like the emitters' anchors.
    const float a = sim_.aspect();
    const float x = clipX * a;
    sim_.queueSplat({x, clipY, x, clipY, std::clamp(radius, 0.01f, 0.4f), velX * a * kKickForce, velY * kKickForce, 0.0f, 0.0f, 0.0f});
}

void FlowField::queueVortex(float clipX, float clipY, float spin, float radius) {
    constexpr int kArms = 6;
    const float a = std::max(sim_.aspect(), 1e-3f);
    for (int i = 0; i < kArms; i++) {
        const float t = 6.2831853f * static_cast<float>(i) / kArms;
        queueKick(clipX + std::cos(t) * radius / a, clipY + std::sin(t) * radius, -std::sin(t) * spin, std::cos(t) * spin, radius * 0.6f);
    }
}

void FlowField::setGrid(int cells, bool macCormack) {
    sim_.macCormack = macCormack;
    if (cells > 0 && sim_.available()) sim_.applyResolution(cells, sim_.dyeRes);
}

void FlowField::step(const GeodeSoundFrame& features, float dt, const SceneParams& p) {
    if (!sim_.available()) return;
    sim_.curlStrength = std::clamp(p.flowCurl, 0.0f, 50.0f);
    emitters_.forceScale = std::clamp(p.flowForce, 0.0f, 3.0f);
    emitters_.stirrerSpeed = std::clamp(p.speed, 0.1f, 2.0f);
    // The whole frame; FluidSim::step splits it into stable steps (#383).
    const float simDt = math::frameSimDt(dt);
    emitters_.tick(features, simDt, sim_.aspect(), 0.0f, 1.0f, splats_);
    for (const Splat& s : splats_) sim_.queueSplat(s);
    sim_.step(simDt);
}

}  // namespace geode::viz::fluid
