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
                                 const float* params) {
    customize::RowContext context;
    context.styleId = style_id ? style_id : "";
    context.query = query ? query : "";
    context.transition = transition != 0;
    context.liquidMode = liquid_mode;
    return handOut(customize::rows(keys ? keys : "", look(params), context));
}

const char* geode_panel_text(const char* id) { return geode::viz::panel::text(id ? id : ""); }

const char* geode_style_label(const char* style_id) { return handOut(geode::viz::catalogue::styleLabel(style_id ? style_id : "")); }

const char* geode_style_family_label(const char* family) { return handOut(geode::viz::catalogue::familyLabel(family ? family : "")); }

const char* geode_palette_maker_start(const float* params) {
    const SceneParams p = look(params);
    const auto stops = p.paletteStops();
    return handOut(palettepanel::encode(palettepanel::open(stops.data(), p.paletteStopCount(), p.paletteBase(), p.paletteRange())));
}

const char* geode_palette_maker_from_look(const float* params) {
    const SceneParams p = look(params);
    return handOut(palettepanel::encode(palettepanel::open(nullptr, 0, p.paletteBase(), p.paletteRange())));
}

const char* geode_palette_maker_open(const int* stops, int count, float base, float span) {
    return handOut(palettepanel::encode(palettepanel::open(stops, stops ? count : 0, base, span)));
}

const char* geode_palette_maker_edit(const char* state, int op, int arg) {
    const auto edit = std::clamp(op, 0, 3);
    return handOut(palettepanel::encode(palettepanel::edit(makerOf(state), static_cast<palettepanel::Edit>(edit), arg)));
}

int geode_palette_maker_stops(const char* state, int* out, int capacity) { return copyOut(makerOf(state).stops, out, capacity); }

const char* geode_palette_maker_view(const char* state) { return handOut(palettepanel::makerView(makerOf(state))); }

int geode_palette_saved_colours(const int* stops, int count, float base, float span, int* out, int capacity) {
    return copyOut(palettepanel::stackColours(stops, stops ? count : 0, base, span), out, capacity);
}

const char* geode_palette_saved_swatch(const int* stops, int count, float base, float span) {
    return handOut(palettepanel::savedSwatch(stops, stops ? count : 0, base, span));
}

const char* geode_palette_list_view(const float* params, const char* custom_id, const char* saved_ids, const char* saved_names) {
    return handOut(palettepanel::listView(look(params), custom_id ? custom_id : "", saved_ids ? saved_ids : "", saved_names ? saved_names : ""));
}

const char* geode_palette_stack_card(const char* stack, const float* params, int saved_count) {
    return handOut(palettepanel::stackCard(stack ? stack : "", look(params), saved_count));
}

}  // extern "C"
