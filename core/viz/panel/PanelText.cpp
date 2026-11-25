#include "viz/panel/PanelText.hpp"

#include <vector>

namespace geode::viz::panel {

namespace {

struct Row {
    const char* id;
    const char* text;
};

// In page order. An id is the string resource's old name, so a search of the history finds where it came from.
constexpr Row kRows[] = {
    // Customize's pieces (CustomizeRows draws each by its piece row).
    {"macro_editor_title", "Edit knobs"},
    {"customize_your_colours", "Your colours"},
    {"customize_palette_from_artwork", "Take the colours from the artwork"},
    {"customize_personality", "Personality"},
    {"customize_new_personality", "New personality"},
    {"customize_moments", "Moments"},
    {"customize_reactions", "Reactions"},
    {"customize_envelopes", "Envelopes"},
    {"customize_waves", "Waves"},
    {"customize_randomize_waves", "Randomize waves"},
    {"customize_layers", "Layers"},
    {"customize_shader_code", "Shader code"},
    {"customize_rise", "Rise"},
    {"customize_fall", "Fall"},
    {"customize_transition_length", "Transition length"},
    {"customize_liquid_mode", "Liquid mode"},
    {"customize_shape_held", "Shape set by this style: %1$s."},
    // A reaction's one-line summary: source, target, mode, amount.
    {"customize_reaction_summary", "When %1$s → %2$s · %3$s %4$s"},
    // A reaction or wave aimed at a control this style does not draw (the wave asks for another).
    {"customize_target_inert", "%1$s does nothing on this style."},
    {"customize_target_inert_pick", "%1$s does nothing on this style - pick another."},
    // A control's label and its value, as one line.
    {"customize_label_value", "%1$s · %2$s"},
    // Look's palette list on a style the stack does not draw.
    {"look_summary_custom", "Custom palette"},
    // Your colours (Customize › Look).
    {"palette_stop", "Colour %1$d of %2$d, %3$s"},
    {"palette_stop_add", "Add a colour"},
    {"palette_stop_remove", "Remove this colour"},
    {"palette_from_current", "Start from this palette"},
    {"palette_name_label", "Palette name"},
    {"palette_save", "Save palette"},
    {"palette_saved_heading", "Your palettes"},
    {"palette_none_yet", "None yet: make one above and save it."},
    {"palette_edit", "Edit"},
    {"palette_delete", "Delete"},
    // The palette stack (Customize › Look › Palettes).
    {"palette_stack_heading", "Palettes"},
    {"palette_stack_explainer", "Stack palettes and every style takes its colours from them. With none, the style keeps the palette above."},
    {"palette_stack_add", "Add palette"},
    {"palette_stack_add_yours", "Add one of yours"},
    {"palette_stack_add_mark", "+"},
    {"palette_stack_remove", "Remove %1$s"},
    {"palette_stack_up", "Move %1$s up"},
    {"palette_stack_amount", "Amount of %1$s"},
    {"palette_stack_share", "Side by side"},
    {"palette_stack_mix", "Mixed"},
    {"palette_stack_blend", "How the palettes combine"},
};

}  // namespace

const char* text(std::string_view id) {
    for (const auto& row : kRows) {
        if (id == row.id) return row.text;
    }
    return "";
}

std::string format(std::string_view id, std::initializer_list<std::string> args) {
    const std::vector<std::string> given(args);
    const std::string_view in = text(id);
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '%' || i + 1 >= in.size()) {
            out += in[i];
            continue;
        }
        if (in[i + 1] == '%') {
            out += '%';
            ++i;
            continue;
        }
        // "%<n>$s" or "%<n>$d": one digit is enough for every row here.
        if (i + 3 < in.size() && in[i + 1] >= '1' && in[i + 1] <= '9' && in[i + 2] == '$' && (in[i + 3] == 's' || in[i + 3] == 'd')) {
            const size_t n = static_cast<size_t>(in[i + 1] - '1');
            if (n < given.size()) out += given[n];
            i += 3;
            continue;
        }
        out += in[i];
    }
    return out;
}

std::string ids() {
    std::string out;
    for (const auto& row : kRows) out += std::string(row.id) + "\n";
    return out;
}

}  // namespace geode::viz::panel
