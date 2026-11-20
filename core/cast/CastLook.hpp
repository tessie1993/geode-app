#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "api/geode_api.h"

// The look the cast's own renderer draws (output/android/CastRenderHost), handed in by the shell from any thread
// and read on the cast's render thread. A deep copy of a geode_viz_look, with a version that goes up on each set,
// so the render thread applies it when it changed and once on a renderer it just made. Which parts of it differ
// from the last one is geode_viz_set_look's to say. A null text stays null (no reactions: the style's own routes).
// Pure; host-tested (core/tests/cast_look_test.cpp).
namespace geode::cast {

class CastLook {
public:
    void set(const geode_viz_look& look) {
        std::lock_guard<std::mutex> lock(mutex_);
        scene_ = copy(look.scene_id);
        transition_ = copy(look.transition_id);
        lfos_ = copy(look.lfos);
        adsrs_ = copy(look.adsrs);
        reactions_ = copy(look.reactions);
        params_.assign(look.params, look.params + (look.params ? std::max(look.param_count, 0) : 0));
        hasParams_ = look.params != nullptr;
        tuning_.assign(look.tuning, look.tuning + (look.tuning ? std::max(look.tuning_count, 0) : 0));
        hasTuning_ = look.tuning != nullptr;
        reducedMotion_ = look.reduced_motion;
        transitionMs_ = look.transition_ms;
        ++version_;
    }

    // Calls apply(look) with the stored look when it is newer than `seen`, and records it as seen. False when none
    // was set yet or `seen` is current. The look's pointers are valid only inside apply.
    template <class Apply>
    bool take(uint64_t& seen, Apply&& apply) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (version_ == 0 || version_ == seen) return false;
        geode_viz_look look{};
        look.scene_id = scene_ ? scene_->c_str() : nullptr;
        look.params = hasParams_ ? params_.data() : nullptr;
        look.param_count = static_cast<int>(params_.size());
        look.lfos = lfos_ ? lfos_->c_str() : nullptr;
        look.adsrs = adsrs_ ? adsrs_->c_str() : nullptr;
        look.tuning = hasTuning_ ? tuning_.data() : nullptr;
        look.tuning_count = static_cast<int>(tuning_.size());
        look.reactions = reactions_ ? reactions_->c_str() : nullptr;
        look.reduced_motion = reducedMotion_;
        look.transition_id = transition_ ? transition_->c_str() : nullptr;
        look.transition_ms = transitionMs_;
        apply(static_cast<const geode_viz_look&>(look));
        seen = version_;
        return true;
    }

private:
    static std::optional<std::string> copy(const char* text) {
        if (!text) return std::nullopt;
        return std::string(text);
    }

    mutable std::mutex mutex_;
    uint64_t version_ = 0;
    std::optional<std::string> scene_, transition_, lfos_, adsrs_, reactions_;
    std::vector<float> params_, tuning_;
    bool hasParams_ = false;
    bool hasTuning_ = false;
    int reducedMotion_ = 0;
    long long transitionMs_ = 0;
};

}  // namespace geode::cast
