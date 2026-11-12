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

void geode_macro_bank_set(const char* bank) { macrobank::setGlobal(bankOf(bank)); }

const char* geode_macro_bank_default(void) {
    static const std::string kText = macrobank::encode(macrobank::defaultBank());
    return kText.c_str();
}

const char* geode_macro_bank_targets_text(void) { return macrobank::targetsText().c_str(); }

const char* geode_macro_bank_normalize(const char* bank) { return handOut(bankOf(bank)); }

const char* geode_macro_bank_set_value(const char* bank, int knob_id, float value) {
    return handOut(macrobank::setValue(bankOf(bank), knob_id, value));
}

const char* geode_macro_bank_rename(const char* bank, int knob_id, const char* name) {
    return handOut(macrobank::rename(bankOf(bank), knob_id, name ? name : ""));
}

const char* geode_macro_bank_add_knob(const char* bank, const char* name) {
    return handOut(macrobank::addKnob(bankOf(bank), name ? name : ""));
}

const char* geode_macro_bank_remove_knob(const char* bank, int knob_id) {
    return handOut(macrobank::removeKnob(bankOf(bank), knob_id));
}

const char* geode_macro_bank_duplicate_knob(const char* bank, int knob_id) {
    return handOut(macrobank::duplicateKnob(bankOf(bank), knob_id));
}

const char* geode_macro_bank_move_knob(const char* bank, int knob_id, int delta) {
    return handOut(macrobank::moveKnob(bankOf(bank), knob_id, delta));
}

const char* geode_macro_bank_reset_knob(const char* bank, int knob_id) {
    return handOut(macrobank::resetKnob(bankOf(bank), knob_id));
}

const char* geode_macro_bank_set_link(const char* bank, int knob_id, const char* target, float up, float down) {
    return handOut(macrobank::setLink(bankOf(bank), knob_id, macrobank::targetNamed(target ? target : ""), up, down));
}

const char* geode_macro_bank_add_link(const char* bank, int knob_id, const char* target) {
    return handOut(macrobank::addLink(bankOf(bank), knob_id, macrobank::targetNamed(target ? target : "")));
}

const char* geode_macro_bank_remove_link(const char* bank, int knob_id, const char* target) {
    return handOut(macrobank::removeLink(bankOf(bank), knob_id, macrobank::targetNamed(target ? target : "")));
}

const char* geode_macro_bank_set_curve(const char* bank, int knob_id, const char* target, const char* curve) {
    const auto shape = macrobank::curveNamed(curve ? curve : "");
    const macrobank::Bank parsed = bankOf(bank);
    if (!shape) return handOut(macrobank::normalized(parsed));
    return handOut(macrobank::setCurve(parsed, knob_id, macrobank::targetNamed(target ? target : ""), *shape));
}

const char* geode_macro_bank_add_knob_for(const char* bank, const char* control) {
    return handOut(macrobank::addKnobFor(bankOf(bank), control ? control : ""));
}

const char* geode_macro_bank_roll(const char* bank, int knob_id, long long seed) {
    return handOut(macrobank::roll(bankOf(bank), knob_id, static_cast<std::uint64_t>(seed)));
}

const char* geode_macro_bank_roll_ranges(const char* bank, int knob_id, long long seed) {
    return handOut(macrobank::rollRanges(bankOf(bank), knob_id, static_cast<std::uint64_t>(seed)));
}

const char* geode_macro_bank_add_random(const char* bank, int knob_id, const char* style_id, int count, long long seed) {
    const auto kept = geode::viz::customize::kept();
    const macrobank::Reach reach = macrobank::reachMask(std::string(style_id ? style_id : ""));
    return handOut(macrobank::addRandom(bankOf(bank), knob_id, count, reach, *kept, static_cast<std::uint64_t>(seed)));
}

const char* geode_macro_bank_roll_values(const char* bank, const char* style_id, long long seed) {
    return handOut(macrobank::rollValues(bankOf(bank), std::string(style_id ? style_id : ""), static_cast<std::uint64_t>(seed)));
}

const char* geode_macro_bank_hide_knob(const char* bank, int knob_id) { return handOut(macrobank::hideKnob(bankOf(bank), knob_id)); }

const char* geode_macro_bank_show_knob(const char* bank, int knob_id) { return handOut(macrobank::showKnob(bankOf(bank), knob_id)); }

const char* geode_macro_bank_auto_name(const char* bank, int knob_id) { return handOut(macrobank::autoName(bankOf(bank), knob_id)); }

const char* geode_macro_bank_controls(const char* bank) { return handOut(macrobank::controlsText(bankOf(bank))); }

const char* geode_macro_bank_live(const char* bank, const char* style_id) {
    return handOut(macrobank::liveText(bankOf(bank), style_id ? style_id : ""));
}

const char* geode_macro_bank_live_mods(void) { return handOut(macrobank::liveModsText()); }

const char* geode_macro_bank_editor(const char* bank, const char* style_id) {
    return handOut(macrobank::editorText(bankOf(bank), style_id ? style_id : ""));
}

}
