#include "stage/StagePacing.hpp"

#include <cmath>

namespace geode::stage {

StagePacing::Tick StagePacing::step(int64_t frameTimeNs) {
    namespace frame = viz::frame;
    Tick tick;
    // The cap read here, each vsync, so a new one lands on the next tick; Follow device takes its cap from the
    // panel the state has measured.
    const float cap = frame::pacerCapFps(state_, cap_);
    tick.dt = frame::Pacer::onVsync(state_, frameTimeNs, cap);
    // The state records the cap it paced to; Follow device's moves with the panel.
    if (state_.capMilliHz != askedCapMilliHz_) {
        askedCapMilliHz_ = state_.capMilliHz;
        tick.restateRate = true;
        tick.rateFps = frameRateFor(cap_);
    }
    pacedMilliHz_ = state_.pacedMilliHz;
    tick.draw = tick.dt > 0.0;
    return tick;
}

float StagePacing::frameRateFor(float capFps) {
    // Follow device states no rate: the system keeps choosing the panel's mode.
    if (viz::frame::isLiveFpsFollow(capFps)) return 0.0f;
    return std::isfinite(capFps) && capFps > 0.0f ? capFps : 0.0f;
}

}  // namespace geode::stage
