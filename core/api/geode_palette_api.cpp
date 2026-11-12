// The palette stack's C ABI (viz/Palette.hpp): the library, every edit of a stack or a palette as text in,
// text out, the ramp the swatches draw, an old look's colours as a stack, and the picker's recent colours.
// Pure, any thread; marshalled by geode_viz_jni.cpp.
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "api/geode_api.h"
#include "viz/Colour.hpp"
#include "viz/Palette.hpp"
#include "viz/Params.hpp"

namespace palette = geode::viz::palette;
namespace colour = geode::viz::colour;
using geode::viz::SceneParams;

namespace {

palette::Stack stackOf(const char* text) { return palette::decode(text ? text : ""); }
palette::Palette paletteOf(const char* line) { return palette::decodePalette(line ? line : ""); }

// Valid until this thread's next palette call, like the macro bank's.
const char* handOut(std::string text) {
    thread_local std::string held;
    held = std::move(text);
    return held.c_str();
}

const char* handOut(const palette::Stack& s) { return handOut(palette::encode(s)); }
// An invalid palette hands out "": the shell keeps what it had.
const char* handOut(const palette::Palette& p) { return handOut(palette::valid(p) ? palette::encode(p) : std::string()); }

palette::Amounts amountsOf(const float* amounts) {
    if (!amounts) return palette::defaultAmounts();
    palette::Amounts a{};
    std::copy(amounts, amounts + palette::kMaxLayers, a.begin());
    return a;
}

void writePacked(const std::vector<colour::Rgb>& colours, int* out) {
    for (size_t i = 0; i < colours.size(); ++i) out[i] = colour::pack(colours[i]);
}

}  // namespace

extern "C" {

const char* geode_palette_library_text(void) { return palette::libraryText().c_str(); }

const char* geode_palette_library_entry(int index) {
    const auto& lib = palette::library();
    if (index < 0 || index >= static_cast<int>(lib.size())) return "";
    return handOut(palette::encode(lib[static_cast<size_t>(index)].palette));
}

int geode_palette_library_index(const char* id) { return id ? palette::libraryIndex(id) : -1; }

const char* geode_palette_colour_map_name(int index) {
    if (index < 0 || index >= palette::kColourMapCount) return "";
    return palette::library()[static_cast<size_t>(palette::kColourMapFirst + index)].palette.name.c_str();
}

const char* geode_palette_stack_normalize(const char* stack) { return handOut(stackOf(stack)); }

const char* geode_palette_stack_unset(void) {
    static const std::string kText = palette::encode(palette::Stack{});
    return kText.c_str();
}

int geode_palette_stack_is_unset(const char* stack) { return palette::isUnset(stackOf(stack)) ? 1 : 0; }

int geode_palette_stack_layer_count(const char* stack) { return static_cast<int>(stackOf(stack).layers.size()); }

const char* geode_palette_stack_layer(const char* stack, int layer) {
    const palette::Stack s = stackOf(stack);
    if (layer < 0 || layer >= static_cast<int>(s.layers.size())) return "";
    return handOut(s.layers[static_cast<size_t>(layer)]);
}

const char* geode_palette_stack_resolve_default(const char* stack, const char* style_default) {
    return handOut(palette::resolveDefault(stackOf(stack), paletteOf(style_default)));
}

const char* geode_palette_stack_editor(const char* stack) {
    const palette::Stack s = stackOf(stack);
    const int layers = static_cast<int>(s.layers.size());
    // A name is free text: a '|' or a line end in it would break the row.
    const auto cell = [](std::string name) {
        for (char& c : name) {
            if (c == '|' || c == '\n' || c == '\r') c = ' ';
        }
        return name;
    };
    std::string out = "palettestack|1|" + std::to_string(layers) + "|" + std::to_string(palette::kMaxLayers - layers) + "|" +
                      palette::name(s.blend) + "\n";
    for (int i = 0; i < layers; ++i) {
        out += "layer|" + std::to_string(i) + "|" + cell(s.layers[static_cast<size_t>(i)].name) + "\n";
    }
    const auto& lib = palette::library();
    for (size_t i = 0; i < lib.size(); ++i) {
        if (palette::offered(static_cast<int>(i))) out += "pick|" + std::to_string(i) + "|" + cell(lib[i].palette.name) + "\n";
    }
    return handOut(std::move(out));
}

const char* geode_palette_stack_add_library(const char* stack, int index) {
    const auto& lib = palette::library();
    if (index < 0 || index >= static_cast<int>(lib.size())) return handOut(stackOf(stack));
    return handOut(palette::addLayer(stackOf(stack), lib[static_cast<size_t>(index)].palette));
}

const char* geode_palette_stack_add_colours(const char* stack, const char* name, const int* rgb, int count) {
    if (!rgb || count <= 0) return handOut(stackOf(stack));
    return handOut(palette::addLayer(stackOf(stack), palette::fromColours(palette::Source::Yours, "", name ? name : "", rgb, count)));
}

void geode_palette_stack_layer_swatch(const char* stack, int layer, int samples, int* out) {
    if (!out || samples <= 0) return;
    const palette::Stack s = stackOf(stack);
    std::fill(out, out + samples, 0);
    if (layer < 0 || layer >= static_cast<int>(s.layers.size())) return;
    geode_palette_swatch(palette::encode(s.layers[static_cast<size_t>(layer)]).c_str(), samples, out);
}

void geode_palette_library_swatch(int index, int samples, int* out) {
    if (!out || samples <= 0) return;
    const auto& lib = palette::library();
    std::fill(out, out + samples, 0);
    if (index < 0 || index >= static_cast<int>(lib.size())) return;
    geode_palette_swatch(palette::encode(lib[static_cast<size_t>(index)].palette).c_str(), samples, out);
}

const char* geode_palette_stack_add(const char* stack, const char* palette_line) {
    return handOut(palette::addLayer(stackOf(stack), paletteOf(palette_line)));
}

const char* geode_palette_stack_set_layer(const char* stack, int layer, const char* palette_line) {
    return handOut(palette::setLayer(stackOf(stack), layer, paletteOf(palette_line)));
}

const char* geode_palette_stack_remove(const char* stack, int layer) { return handOut(palette::removeLayer(stackOf(stack), layer)); }

const char* geode_palette_stack_move(const char* stack, int layer, int delta) {
    return handOut(palette::moveLayer(stackOf(stack), layer, delta));
}

const char* geode_palette_stack_set_blend(const char* stack, const char* blend) {
    palette::Stack s = stackOf(stack);
    const std::string b = blend ? blend : "";
    if (b == palette::name(palette::Blend::Share)) s.blend = palette::Blend::Share;
    if (b == palette::name(palette::Blend::Mix)) s.blend = palette::Blend::Mix;
    return handOut(s);
}

const char* geode_palette_stack_set_edge(const char* stack, const char* edge) {
    palette::Stack s = stackOf(stack);
    const std::string e = edge ? edge : "";
    if (e == palette::name(palette::Edge::Soft)) s.edge = palette::Edge::Soft;
    if (e == palette::name(palette::Edge::Hard)) s.edge = palette::Edge::Hard;
    return handOut(s);
}

void geode_palette_stack_set(const char* stack) { palette::setGlobal(stackOf(stack)); }

const char* geode_palette_stack_set_full_range(const char* stack, int on) {
    palette::Stack s = stackOf(stack);
    s.fullRange = on != 0;
    return handOut(s);
}

const char* geode_palette_from_colours(const char* source, const char* ref, const char* name, const int* rgb, int count) {
    palette::Source src = palette::Source::Yours;
    const std::string want = source ? source : "";
    for (int i = 0; i <= static_cast<int>(palette::Source::Yours); ++i) {
        if (want == palette::name(static_cast<palette::Source>(i))) src = static_cast<palette::Source>(i);
    }
    return handOut(palette::fromColours(src, ref ? ref : "", name ? name : "", rgb, std::max(count, 0)));
}

const char* geode_palette_set_stop(const char* palette_line, int stop, int rgb, float pos) {
    return handOut(palette::setStop(paletteOf(palette_line), stop, rgb, pos));
}

const char* geode_palette_add_stop(const char* palette_line, int rgb, float pos) {
    return handOut(palette::addStop(paletteOf(palette_line), rgb, pos));
}

const char* geode_palette_remove_stop(const char* palette_line, int stop) {
    return handOut(palette::removeStop(paletteOf(palette_line), stop));
}

const char* geode_palette_set_option(const char* palette_line, const char* option) {
    palette::Palette p = paletteOf(palette_line);
    palette::setOption(p, option ? option : "");
    return handOut(p);
}

const char* geode_palette_rename(const char* palette_line, const char* name) {
    palette::Palette p = paletteOf(palette_line);
    p.name = name ? name : "";
    return handOut(palette::normalized(std::move(p)));
}

const char* geode_palette_spread(const char* palette_line) { return handOut(palette::spreadEvenly(paletteOf(palette_line))); }

void geode_palette_swatch(const char* palette_line, int samples, int* out) {
    if (!out || samples <= 0) return;
    const palette::Palette p = paletteOf(palette_line);
    for (int i = 0; i < samples; ++i) {
        out[i] = colour::pack(palette::along(p, samples > 1 ? static_cast<float>(i) / static_cast<float>(samples - 1) : 0.0f));
    }
}

void geode_palette_stack_ramp(const char* stack, const float* amounts, int samples, int* out) {
    if (!out || samples <= 0) return;
    writePacked(palette::compose(stackOf(stack), amountsOf(amounts), samples), out);
}

const char* geode_palette_stack_from_params(const float* params, float* amounts_out) {
    const SceneParams p = params ? SceneParams::fromWire(params) : SceneParams{};
    palette::Amounts amounts{};
    const palette::Stack s = palette::fromLegacy(p, amounts);
    if (amounts_out) std::copy(amounts.begin(), amounts.end(), amounts_out);
    return handOut(s);
}

const char* geode_colour_recent_push(const char* recent, int rgb) {
    return handOut(colour::recent::encode(colour::recent::push(colour::recent::decode(recent ? recent : ""), rgb)));
}

int geode_colour_recent_read(const char* recent, int* out, int max) {
    const auto list = colour::recent::decode(recent ? recent : "");
    const int n = std::min(static_cast<int>(list.size()), std::max(max, 0));
    for (int i = 0; out && i < n; ++i) out[i] = list[static_cast<size_t>(i)];
    return out ? n : static_cast<int>(list.size());
}

}  // extern "C"
