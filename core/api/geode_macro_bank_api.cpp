#include <cstdint>
#include <string>
#include <utility>

#include "api/geode_api.h"
#include "viz/CustomizeMap.hpp"
#include "viz/MacroBank.hpp"

namespace macrobank = geode::viz::macrobank;

namespace {

macrobank::Bank bankOf(const char* text) { return macrobank::decode(text ? text : ""); }

// Valid until this thread's next macro bank call, like geode_customize_shown.
const char* handOut(std::string text) {
    thread_local std::string held;
    held = std::move(text);
    return held.c_str();
}

const char* handOut(const macrobank::Bank& bank) { return handOut(macrobank::encode(bank)); }

}  // namespace

extern "C" {

void geode_macro_bank_set(const char* bank) try {
    macrobank::setGlobal(bankOf(bank));
} catch (...) {
}

const char* geode_macro_bank_default(void) try {
    static const std::string kText = macrobank::encode(macrobank::defaultBank());
    return kText.c_str();
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_targets_text(void) try {
    return macrobank::targetsText().c_str();
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_normalize(const char* bank) try {
    return handOut(bankOf(bank));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_set_value(const char* bank, int knob_id, float value) try {
    return handOut(macrobank::setValue(bankOf(bank), knob_id, value));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_rename(const char* bank, int knob_id, const char* name) try {
    return handOut(macrobank::rename(bankOf(bank), knob_id, name ? name : ""));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_add_knob(const char* bank, const char* name) try {
    return handOut(macrobank::addKnob(bankOf(bank), name ? name : ""));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_remove_knob(const char* bank, int knob_id) try {
    return handOut(macrobank::removeKnob(bankOf(bank), knob_id));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_duplicate_knob(const char* bank, int knob_id) try {
    return handOut(macrobank::duplicateKnob(bankOf(bank), knob_id));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_move_knob(const char* bank, int knob_id, int delta) try {
    return handOut(macrobank::moveKnob(bankOf(bank), knob_id, delta));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_reset_knob(const char* bank, int knob_id) try {
    return handOut(macrobank::resetKnob(bankOf(bank), knob_id));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_reset_all(const char* bank) try {
    return handOut(macrobank::resetAll(bankOf(bank)));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_set_link(const char* bank, int knob_id, const char* target, float up, float down) try {
    return handOut(macrobank::setLink(bankOf(bank), knob_id, macrobank::targetNamed(target ? target : ""), up, down));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_add_link(const char* bank, int knob_id, const char* target) try {
    return handOut(macrobank::addLink(bankOf(bank), knob_id, macrobank::targetNamed(target ? target : "")));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_remove_link(const char* bank, int knob_id, const char* target) try {
    return handOut(macrobank::removeLink(bankOf(bank), knob_id, macrobank::targetNamed(target ? target : "")));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_set_curve(const char* bank, int knob_id, const char* target, const char* curve) try {
    const auto shape = macrobank::curveNamed(curve ? curve : "");
    const macrobank::Bank parsed = bankOf(bank);
    if (!shape) return handOut(macrobank::normalized(parsed));
    return handOut(macrobank::setCurve(parsed, knob_id, macrobank::targetNamed(target ? target : ""), *shape));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_add_knob_for(const char* bank, const char* control) try {
    return handOut(macrobank::addKnobFor(bankOf(bank), control ? control : ""));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_roll(const char* bank, int knob_id, long long seed) try {
    return handOut(macrobank::roll(bankOf(bank), knob_id, static_cast<std::uint64_t>(seed)));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_roll_ranges(const char* bank, int knob_id, long long seed) try {
    return handOut(macrobank::rollRanges(bankOf(bank), knob_id, static_cast<std::uint64_t>(seed)));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_add_random(const char* bank, int knob_id, const char* style_id, int count, long long seed) try {
    const auto kept = geode::viz::customize::kept();
    const macrobank::Reach reach = macrobank::reachMask(std::string(style_id ? style_id : ""));
    return handOut(macrobank::addRandom(bankOf(bank), knob_id, count, reach, *kept, static_cast<std::uint64_t>(seed)));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_roll_values(const char* bank, const char* style_id, long long seed) try {
    return handOut(macrobank::rollValues(bankOf(bank), std::string(style_id ? style_id : ""), static_cast<std::uint64_t>(seed)));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_hide_knob(const char* bank, int knob_id) try {
    return handOut(macrobank::hideKnob(bankOf(bank), knob_id));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_show_knob(const char* bank, int knob_id) try {
    return handOut(macrobank::showKnob(bankOf(bank), knob_id));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_auto_name(const char* bank, int knob_id) try {
    return handOut(macrobank::autoName(bankOf(bank), knob_id));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_controls(const char* bank) try {
    return handOut(macrobank::controlsText(bankOf(bank)));
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_live(const char* bank, const char* style_id) try {
    return handOut(macrobank::liveText(bankOf(bank), style_id ? style_id : ""));
} catch (...) {
    return nullptr;
}

float geode_macro_bank_modulated(float turn, float add) {
    return macrobank::modulated(turn, add);
}

const char* geode_macro_bank_live_mods(void) try {
    return handOut(macrobank::liveModsText());
} catch (...) {
    return nullptr;
}

const char* geode_macro_bank_editor(const char* bank, const char* style_id) try {
    return handOut(macrobank::editorText(bankOf(bank), style_id ? style_id : ""));
} catch (...) {
    return nullptr;
}

}
