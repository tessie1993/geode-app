// C ABI over what Customize draws: the control rows (viz/CustomizeRows), the panel's words (viz/PanelText), the
// style names (viz/StyleNames) and the palette pages (viz/PalettePanel). See geode_api.h. Pure, any thread.
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "api/geode_api.h"
#include "viz/CustomizeRows.hpp"
#include "viz/PalettePanel.hpp"
#include "viz/PanelText.hpp"
#include "viz/Params.hpp"
#include "viz/StyleNames.hpp"

namespace customize = geode::viz::customize;
namespace palettepanel = geode::viz::palettepanel;
using geode::viz::SceneParams;

namespace {

// Valid until this thread's next call into this file.
const char* handOut(std::string text) {
    thread_local std::string held;
    held = std::move(text);
    return held.c_str();
}

SceneParams look(const float* params) { return params ? SceneParams::fromWire(params) : SceneParams{}; }

palettepanel::Maker makerOf(const char* state) { return palettepanel::decodeMaker(state ? state : ""); }

int copyOut(const std::vector<int>& colours, int* out, int capacity) {
    const int n = std::min(static_cast<int>(colours.size()), std::max(capacity, 0));
    if (out) std::copy(colours.begin(), colours.begin() + n, out);
    return out ? n : 0;
}

}  // namespace

extern "C" {

const char* geode_customize_rows(const char* style_id, const char* keys, const char* query, int transition, int liquid_mode,
                                 const float* params) try {
    customize::RowContext context;
    context.styleId = style_id ? style_id : "";
    context.query = query ? query : "";
    context.transition = transition != 0;
    context.liquidMode = liquid_mode;
    return handOut(customize::rows(keys ? keys : "", look(params), context));
} catch (...) {
    return nullptr;
}

int geode_customize_matches(const char* query, const char* text) try {
    return customize::matches(query ? query : "", text ? text : "") ? 1 : 0;
} catch (...) {
    return 1;
}

int geode_customize_query_blank(const char* query) try {
    return customize::blank(query ? query : "") ? 1 : 0;
} catch (...) {
    return 1;
}

const char* geode_panel_format(const char* id, const char* const* args, int count) try {
    std::vector<std::string> filled;
    for (int i = 0; args && i < count; ++i) filled.emplace_back(args[i] ? args[i] : "");
    // The placeholders reach at most four arguments; a missing one is dropped as an empty text is.
    filled.resize(4);
    std::string text = geode::viz::panel::format(id ? id : "", {filled[0], filled[1], filled[2], filled[3]});
    return handOut(std::move(text));
} catch (...) {
    return nullptr;
}

const char* geode_panel_text(const char* id) try {
    return geode::viz::panel::text(id ? id : "");
} catch (...) {
    return nullptr;
}

const char* geode_style_label(const char* style_id) try {
    return handOut(geode::viz::catalogue::styleLabel(style_id ? style_id : ""));
} catch (...) {
    return nullptr;
}

const char* geode_style_family_label(const char* family) try {
    return handOut(geode::viz::catalogue::familyLabel(family ? family : ""));
} catch (...) {
    return nullptr;
}

const char* geode_palette_maker_start(const float* params) try {
    const SceneParams p = look(params);
    const auto stops = p.paletteStops();
    return handOut(palettepanel::encode(palettepanel::open(stops.data(), p.paletteStopCount(), p.paletteBase(), p.paletteRange())));
} catch (...) {
    return nullptr;
}

const char* geode_palette_maker_from_look(const float* params) try {
    const SceneParams p = look(params);
    // The look's own colours, as geode_palette_maker_start has them: its stops are the user's and are not narrowed
    // to a hue ramp.
    const auto stops = p.paletteStops();
    return handOut(palettepanel::encode(palettepanel::open(stops.data(), p.paletteStopCount(), p.paletteBase(), p.paletteRange())));
} catch (...) {
    return nullptr;
}

const char* geode_palette_maker_open(const int* stops, int count, float base, float span) try {
    return handOut(palettepanel::encode(palettepanel::open(stops, stops ? count : 0, base, span)));
} catch (...) {
    return nullptr;
}

const char* geode_palette_maker_edit(const char* state, int op, int arg) try {
    const auto edit = std::clamp(op, 0, 3);
    return handOut(palettepanel::encode(palettepanel::edit(makerOf(state), static_cast<palettepanel::Edit>(edit), arg)));
} catch (...) {
    return nullptr;
}

int geode_palette_maker_stops(const char* state, int* out, int capacity) try {
    return copyOut(makerOf(state).stops, out, capacity);
} catch (...) {
    return 0;
}

const char* geode_palette_maker_view(const char* state) try {
    return handOut(palettepanel::makerView(makerOf(state)));
} catch (...) {
    return nullptr;
}

int geode_palette_saved_colours(const int* stops, int count, float base, float span, int* out, int capacity) try {
    return copyOut(palettepanel::stackColours(stops, stops ? count : 0, base, span), out, capacity);
} catch (...) {
    return 0;
}

const char* geode_palette_saved_swatch(const int* stops, int count, float base, float span) try {
    return handOut(palettepanel::savedSwatch(stops, stops ? count : 0, base, span));
} catch (...) {
    return nullptr;
}

const char* geode_palette_list_view(const float* params, const char* custom_id, const char* saved_ids, const char* saved_names) try {
    return handOut(palettepanel::listView(look(params), custom_id ? custom_id : "", saved_ids ? saved_ids : "", saved_names ? saved_names : ""));
} catch (...) {
    return nullptr;
}

const char* geode_palette_stack_card(const char* stack, const float* params, int saved_count) try {
    return handOut(palettepanel::stackCard(stack ? stack : "", look(params), saved_count));
} catch (...) {
    return nullptr;
}

}  // extern "C"
