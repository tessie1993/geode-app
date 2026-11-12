#include "stage/StagePacing.hpp"

namespace geode::stage {

StagePacing::Tick StagePacing::step(int64_t frameTimeNs, int rungsDown) {
    namespace frame = viz::frame;
    Tick tick;
    // The cap read here, each vsync, so a new one lands on the next tick; Follow device takes its cap from the
    // panel the state has measured.
    const float cap = frame::pacerCapFps(state_, cap_, rungsDown);
    tick.dt = frame::Pacer::onVsync(state_, frameTimeNs, cap);
    // The thermal step moves the cap without any call from the shell; the state records the cap it paced to.
    if (state_.capMilliHz != askedCapMilliHz_) {
        askedCapMilliHz_ = state_.capMilliHz;
        tick.restateRate = true;
        tick.rateFps = frameRateFor(cap_, rungsDown);
    }
    pacedMilliHz_ = state_.pacedMilliHz;
    tick.draw = tick.dt > 0.0;
    return tick;
}

float StagePacing::frameRateFor(float capFps, int rungsDown) {
    // Follow device states no rate, hot or cool: the system keeps choosing the panel's mode, and a hot phone's
    // step is the pacer's divide of whatever it chose.
    if (viz::frame::isLiveFpsFollow(capFps)) return 0.0f;
    return viz::frame::thermalStepCapFps(capFps, rungsDown);
}

}  // namespace geode::stage
