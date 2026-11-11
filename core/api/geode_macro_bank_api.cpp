#include <string>
#include <utility>

#include "api/geode_api.h"
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

const char* geode_macro_bank_set_link(const char* bank, int knob_id, const char* target, float up, float down) {
    return handOut(macrobank::setLink(bankOf(bank), knob_id, macrobank::targetNamed(target ? target : ""), up, down));
}

const char* geode_macro_bank_remove_link(const char* bank, int knob_id, const char* target) {
    return handOut(macrobank::removeLink(bankOf(bank), knob_id, macrobank::targetNamed(target ? target : "")));
}

const char* geode_macro_bank_live(const char* bank, const char* style_id) {
    return handOut(macrobank::liveText(bankOf(bank), style_id ? style_id : ""));
}

}
