#include "viz/panel/PanelText.hpp"

namespace geode::viz::panel {

namespace {

// In page order. An id is the name of the shell's string resource; args is how many placeholders it has.
struct Word {
    const char* id;
    int args;
};

constexpr Word kWords[] = {
    // Customize's pieces (CustomizeRows draws each by its piece row).
    {"macro_editor_title", 0},
    {"customize_your_colours", 0},
    {"customize_palette_from_artwork", 0},
    {"customize_personality", 0},
    {"customize_new_personality", 0},
    {"customize_moments", 0},
    {"customize_reactions", 0},
    {"customize_envelopes", 0},
    {"customize_waves", 0},
    {"customize_randomize_waves", 0},
    {"customize_layers", 0},
    {"customize_shader_code", 0},
    {"customize_rise", 0},
    {"customize_fall", 0},
    {"customize_transition_length", 0},
    {"customize_liquid_mode", 0},
    {"customize_shape_held", 1},
    // A reaction's one-line summary: source, target, mode, amount.
    {"customize_reaction_summary", 4},
    // A reaction or wave aimed at a control this style does not draw (the wave asks for another).
    {"customize_target_inert", 1},
    {"customize_target_inert_pick", 1},
    // A control's label and its value, as one line.
    {"customize_label_value", 2},
    // Look's palette list on a style the stack does not draw.
    {"look_summary_custom", 0},
    // Your colours (Customize › Look).
    {"palette_stop", 3},
    {"palette_stop_add", 0},
    {"palette_stop_remove", 0},
    {"palette_from_current", 0},
    {"palette_name_label", 0},
    {"palette_save", 0},
    {"palette_saved_heading", 0},
    {"palette_none_yet", 0},
    {"palette_edit", 0},
    {"palette_delete", 0},
    // The palette stack (Customize › Look › Palettes).
    {"palette_stack_heading", 0},
    {"palette_stack_explainer", 0},
    {"palette_stack_add", 0},
    {"palette_stack_add_yours", 0},
    {"palette_stack_add_mark", 0},
    {"palette_stack_remove", 1},
    {"palette_stack_up", 1},
    {"palette_stack_amount", 1},
    {"palette_stack_share", 0},
    {"palette_stack_mix", 0},
    {"palette_stack_blend", 0},
};

constexpr char kUnitSeparator = '\x1f';

}  // namespace

int argCount(std::string_view id) {
    for (const Word& word : kWords) {
        if (id == word.id) return word.args;
    }
    return -1;
}

bool has(std::string_view id) { return argCount(id) >= 0; }

std::string ref(std::string_view id, std::initializer_list<std::string> args) {
    if (id.empty()) return {};
    std::string out = "id:" + std::string(id);
    // Exactly as many arguments as the word has placeholders: a missing one is blank and an extra one is
    // dropped, so the shell's format never runs short.
    const auto* arg = args.begin();
    for (int i = 0; i < argCount(id); ++i) {
        out += kUnitSeparator;
        if (arg == args.end()) continue;
        for (const char c : *arg) out += c == kUnitSeparator ? ' ' : c;
        ++arg;
    }
    return out;
}

std::string ids() {
    std::string out;
    for (const Word& word : kWords) out += std::string(word.id) + " " + std::to_string(word.args) + "\n";
    return out;
}

}  // namespace geode::viz::panel
