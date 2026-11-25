// C ABI over the modulation the app holds while it runs (viz/modulation/ModState). See geode_modulation_api.h. The reaction
// edit sits beside the style traits it deals from (geode_viz_api.cpp), the wave roll beside the other rolls
// (geode_params_api.cpp).
#include <mutex>
#include <optional>
#include <string>

#include "api/geode_modulation_api.h"
#include "api/geode_text_out.hpp"
#include "viz/modulation/ModState.hpp"

namespace modstate = geode::viz::modstate;

namespace {

std::string text(const char* s) { return s ? std::string(s) : std::string(); }

}  // namespace

extern "C" {

void geode_mod_state_load(const char* lfos, const char* adsrs, const char* reactions, unsigned mask) try {
    auto& s = modstate::shared();
    std::lock_guard<std::mutex> lock(s.lock);
    s.state.load(text(lfos), text(adsrs), reactions ? std::optional<std::string>(reactions) : std::nullopt, mask);
    const modstate::Hand hand = s.state.hand();
    geode_hand_set(hand.mask, hand.user ? 1 : 0);
} catch (...) {
}

void geode_mod_state_lfo_set(int slot, int field, float value) try {
    auto& s = modstate::shared();
    std::lock_guard<std::mutex> lock(s.lock);
    s.state.setLfo(slot, field, value);
} catch (...) {
}

void geode_mod_state_adsr_set(int slot, int field, float value) try {
    auto& s = modstate::shared();
    std::lock_guard<std::mutex> lock(s.lock);
    s.state.setAdsr(slot, field, value);
} catch (...) {
}

void geode_mod_state_keep_load(const char* document) try {
    auto& s = modstate::shared();
    std::lock_guard<std::mutex> lock(s.lock);
    s.state.loadKeep(text(document));
    geode_director_keep_set(s.state.keep().c_str());
} catch (...) {
}

void geode_mod_state_keep_toggle(const char* key) try {
    auto& s = modstate::shared();
    std::lock_guard<std::mutex> lock(s.lock);
    s.state.toggleKeep(text(key));
    geode_director_keep_set(s.state.keep().c_str());
} catch (...) {
}

int geode_mod_state_text(int which, char* out, int capacity) try {
    auto& s = modstate::shared();
    std::lock_guard<std::mutex> lock(s.lock);
    switch (which) {
        case GEODE_MOD_STATE_LFOS: return geode::api::textOut(s.state.lfos(), out, capacity);
        case GEODE_MOD_STATE_ADSRS: return geode::api::textOut(s.state.adsrs(), out, capacity);
        case GEODE_MOD_STATE_REACTIONS: return geode::api::textOut(s.state.reactions().value_or(std::string()), out, capacity);
        case GEODE_MOD_STATE_KEEP: return geode::api::textOut(s.state.keep(), out, capacity);
        default: return geode::api::textOut(std::string(), out, capacity);
    }
} catch (...) {
    return geode::api::textOut(std::string(), out, capacity);
}

unsigned geode_mod_state_hand_mask(void) try {
    auto& s = modstate::shared();
    std::lock_guard<std::mutex> lock(s.lock);
    return s.state.hand().mask;
} catch (...) {
    return 0u;
}

}  // extern "C"
