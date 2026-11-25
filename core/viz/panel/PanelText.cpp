#include "viz/panel/PanelText.hpp"

namespace geode::viz::panel {

namespace {

// In page order. An id is the name of the shell's string resource.
constexpr const char* kIds[] = {
    // Customize's pieces (CustomizeRows draws each by its piece row).
    "macro_editor_title",
    "customize_your_colours",
    "customize_palette_from_artwork",
    "customize_personality",
    "customize_new_personality",
    "customize_moments",
    "customize_reactions",
    "customize_envelopes",
    "customize_waves",
    "customize_randomize_waves",
    "customize_layers",
    "customize_shader_code",
    "customize_rise",
    "customize_fall",
    "customize_transition_length",
    "customize_liquid_mode",
    "customize_shape_held",
    // A reaction's one-line summary: source, target, mode, amount.
    "customize_reaction_summary",
    // A reaction or wave aimed at a control this style does not draw (the wave asks for another).
    "customize_target_inert",
    "customize_target_inert_pick",
    // A control's label and its value, as one line.
    "customize_label_value",
    // Look's palette list on a style the stack does not draw.
    "look_summary_custom",
    // Your colours (Customize › Look).
    "palette_stop",
    "palette_stop_add",
    "palette_stop_remove",
    "palette_from_current",
    "palette_name_label",
    "palette_save",
    "palette_saved_heading",
    "palette_none_yet",
    "palette_edit",
    "palette_delete",
    // The palette stack (Customize › Look › Palettes).
    "palette_stack_heading",
    "palette_stack_explainer",
    "palette_stack_add",
    "palette_stack_add_yours",
    "palette_stack_add_mark",
    "palette_stack_remove",
    "palette_stack_up",
    "palette_stack_amount",
    "palette_stack_share",
    "palette_stack_mix",
    "palette_stack_blend",
};

constexpr char kUnitSeparator = '\x1f';

}  // namespace

bool has(std::string_view id) {
    for (const char* known : kIds) {
        if (id == known) return true;
    }
    return false;
}

std::string ref(std::string_view id, std::initializer_list<std::string> args) {
    if (id.empty()) return {};
    std::string out = "id:" + std::string(id);
    for (const std::string& arg : args) {
        out += kUnitSeparator;
        for (const char c : arg) out += c == kUnitSeparator ? ' ' : c;
    }
    return out;
}

std::string ids() {
    std::string out;
    for (const char* id : kIds) out += std::string(id) + "\n";
    return out;
}

}  // namespace geode::viz::panel
