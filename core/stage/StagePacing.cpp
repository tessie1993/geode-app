#include "stage/StagePacing.hpp"

namespace geode::stage {

StagePacing::Tick StagePacing::step(int64_t frameTimeNs, bool stageBusy) {
    namespace frame = viz::frame;
    Tick tick;
    // The cap read here, each vsync, so a new one lands on the next tick; Follow device takes its cap from the
    // panel the state has measured.
    const float cap = frame::pacerCapFps(state_, cap_);
    tick.dt = frame::Pacer::onVsync(state_, frameTimeNs, cap, stageBusy);
    // The state records the cap it paced to.
    if (state_.capMilliHz != askedCapMilliHz_) {
        askedCapMilliHz_ = state_.capMilliHz;
        tick.restateRate = true;
        tick.rateFps = frameRateFor(cap_);
    }
    pacedMilliHz_ = state_.pacedMilliHz;
    tick.draw = tick.dt > 0.0;
    return tick;
}

}  // namespace geode::stage
