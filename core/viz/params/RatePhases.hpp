#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "viz/params/Params.hpp"

namespace geode::viz {

// The running phase of every rate parameter (a meta row with kRate).
//
// A slot gets kRate only when something reads its phase (kReadRates below: the renderer reads Speed's for Life's
// births and Rotation's for the post pass); rate_phases_test holds the two lists equal, so a flag cannot be set
// for a phase nobody reads. Other rate params (Dive speed, Fly speed, Stirrer speed, Palette cycle) integrate
// in their own consumers for now and are not flagged.
//
// A rate is a speed per second. A consumer that needs a position from it must integrate it: `clock * rate` is elapsed
// time times the live dial, so every move of the dial (a slider, a wave, a macro, a route) shifts the position by
// uptime * change in one frame. Here the phase takes `phase += value * dt` each frame, `value` being the effective
// one (the params as they are drawn: modulation added to the base, then the single clamp), so a change of rate
// changes how fast the phase runs and never where it is. The plain value stays in the params for whoever wants the rate.
//
// Pure, single-threaded, one step a frame on the dt the picture's clock runs on, so a live frame and an exported
// one given the same inputs give the same bits. dt is used as given (no clamp here): a zero or non-finite dt,
// or a non-finite rate, adds nothing; a negative dt (Clock control's reverse) runs the phase back. Phases are doubles and are not wrapped (a float clock past 4096 s stalls at a
// slow rate, a double does not); the consumer wraps what it reads for its own period (wrapped()).
inline constexpr ParamSlot kReadRates[] = {ParamSlot::speed, ParamSlot::rotation};

class RatePhases {
public:
    static const std::vector<ParamSlot>& slots() {
        static const std::vector<ParamSlot> kSlots = [] {
            std::vector<ParamSlot> out;
            for (int i = 0; i < SceneParams::kFieldCount; ++i) {
                const ParamSlot slot = static_cast<ParamSlot>(i);
                if ((SceneParams::meta(slot).flags & kRate) != 0) out.push_back(slot);
            }
            return out;
        }();
        return kSlots;
    }

    void reset() { phase_.fill(0.0); }

    void step(const SceneParams& effective, float dt) {
        if (dt == 0.0f || !std::isfinite(dt)) return;
        for (ParamSlot slot : slots()) {
            const float rate = effective.getSlot(static_cast<int>(slot));
            if (std::isfinite(rate)) phase_[static_cast<size_t>(slot)] += static_cast<double>(rate) * static_cast<double>(dt);
        }
    }

    // The integral of the slot's value since reset(); 0 for a slot that is no rate.
    double phase(ParamSlot slot) const { return phase_[static_cast<size_t>(slot)]; }

    // The phase held to [0, period), for a consumer that draws it.
    double wrapped(ParamSlot slot, double period) const {
        double w = std::fmod(phase(slot), period);
        if (w < 0.0) {
            w += period;
            // A tiny negative remainder plus the period rounds up to the period itself: [0, period) is half open.
            if (w >= period) w = 0.0;
        }
        return w;
    }

private:
    std::array<double, SceneParams::kFieldCount> phase_{};
};

}  // namespace geode::viz
