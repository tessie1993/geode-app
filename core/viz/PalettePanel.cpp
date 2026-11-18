#include "viz/PalettePanel.hpp"

#include <algorithm>
#include <cmath>

#include "viz/Colour.hpp"
#include "viz/ControlRange.hpp"
#include "viz/Palette.hpp"
#include "viz/PanelText.hpp"
#include "viz/TableNumber.hpp"

namespace geode::viz::palettepanel {

namespace {

// "RRGGBB": the row's own spelling, without the '#' a screen reader would read out.
std::string hex6(int rgb) {
    const std::string h = colour::hex(rgb);
    return h.empty() ? std::string("000000") : h.substr(1);
}

std::string joined(const std::vector<int>& colours) {
    std::string out;
    for (size_t i = 0; i < colours.size(); ++i) {
        if (i > 0) out += ',';
        out += hex6(colours[i]);
    }
    return out;
}

// A row's free text: a '|' or a line end would break it.
std::string cell(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c == '|' || c == '\n' || c == '\r') c = ' ';
    }
    return out;
}

std::vector<std::string_view> lines(std::string_view text) {
    std::vector<std::string_view> out;
    size_t start = 0;
    while (start < text.size()) {
        const size_t end = std::min(text.find('\n', start), text.size());
        out.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

std::vector<int> hueStops(float base, float span, int n) {
    std::vector<int> out(static_cast<size_t>(n));
    colour::stopsFromHues(base, span, n, out.data());
    return out;
}

std::vector<int> setStops(const int* stops, int count) {
    std::vector<int> out;
    if (!stops) return out;
    for (int i = 0; i < count && static_cast<int>(out.size()) < colour::kMaxStops; ++i) {
        if (!colour::valid(stops[i])) break;
        out.push_back(stops[i]);
    }
    return out;
}

// Your colours' loop, the last sample a step short of its end so a strip shows every stop once.
std::vector<int> loopSwatch(const std::vector<int>& stops) {
    std::vector<int> out(static_cast<size_t>(kSwatchSamples));
    const int n = static_cast<int>(stops.size());
    for (int i = 0; i < kSwatchSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSwatchSamples);
        out[static_cast<size_t>(i)] = colour::pack(colour::ramp(stops.data(), n, t));
    }
    return out;
}

// A hue gradient's cosine ramp, end to end.
std::vector<int> hueSwatch(float base, float span) {
    std::vector<int> out(static_cast<size_t>(kSwatchSamples));
    for (int i = 0; i < kSwatchSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSwatchSamples - 1);
        out[static_cast<size_t>(i)] = colour::pack(colour::procedural(base, span, t));
    }
    return out;
}

// What a look's own palette paints, before Hue shift: its stops, else its hue ramp.
std::vector<int> lookSwatch(const SceneParams& p) {
    const auto stops = p.paletteStops();
    const std::vector<int> set = setStops(stops.data(), p.paletteStopCount());
    if (!set.empty()) return loopSwatch(set);
    return hueSwatch(p.paletteBase(), p.paletteRange());
}

// A stack layer's own run, first stop to last.
std::vector<int> layerSwatch(const palette::Palette& layer) {
    std::vector<int> out(static_cast<size_t>(kSwatchSamples));
    for (int i = 0; i < kSwatchSamples; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(kSwatchSamples - 1);
        out[static_cast<size_t>(i)] = colour::pack(palette::along(layer, u));
    }
    return out;
}

bool usesOwnColours(const SceneParams& p) {
    return p.paletteBaseOverride >= 0.0f || p.paletteRangeOverride >= 0.0f || p.paletteStop0 != SceneParams::kNoStop;
}

}  // namespace

std::string encode(const Maker& m) { return std::to_string(m.selected) + "|" + joined(m.stops); }

Maker decodeMaker(std::string_view text) {
    Maker m;
    const size_t bar = text.find('|');
    if (bar != std::string_view::npos) {
        const std::string_view head = text.substr(0, bar);
        int selected = 0;
        // Any count past the stops clamps below, so a long run of digits stops growing at the cap.
        for (char c : head) selected = (c >= '0' && c <= '9') ? std::min(selected * 10 + (c - '0'), colour::kMaxStops) : selected;
        m.selected = selected;
        std::string_view rest = text.substr(bar + 1);
        while (!rest.empty() && static_cast<int>(m.stops.size()) < colour::kMaxStops) {
            const size_t comma = std::min(rest.find(','), rest.size());
            const int rgb = colour::parseHex(rest.substr(0, comma));
            if (colour::valid(rgb)) m.stops.push_back(rgb);
            rest = comma < rest.size() ? rest.substr(comma + 1) : std::string_view();
        }
    }
    if (m.stops.empty()) m.stops.push_back(colour::kWhite);
    m.selected = std::clamp(m.selected, 0, static_cast<int>(m.stops.size()) - 1);
    return m;
}

Maker open(const int* stops, int count, float base, float span) {
    Maker m;
    m.stops = setStops(stops, count);
    if (m.stops.empty()) m.stops = hueStops(base, span, kMakerHueStops);
    return m;
}

Maker edit(Maker m, Edit op, int arg) {
    if (m.stops.empty()) m.stops.push_back(colour::kWhite);
    const int last = static_cast<int>(m.stops.size()) - 1;
    m.selected = std::clamp(m.selected, 0, last);
    switch (op) {
        case Edit::Select: m.selected = std::clamp(arg, 0, last); break;
        case Edit::Set:
            if (colour::valid(arg)) m.stops[static_cast<size_t>(m.selected)] = arg;
            break;
        case Edit::Add:
            if (static_cast<int>(m.stops.size()) < colour::kMaxStops) {
                m.stops.push_back(m.stops[static_cast<size_t>(m.selected)]);
                m.selected = static_cast<int>(m.stops.size()) - 1;
            }
            break;
        case Edit::Remove:
            if (m.stops.size() > 1) {
                m.stops.erase(m.stops.begin() + m.selected);
                m.selected = std::min(m.selected, static_cast<int>(m.stops.size()) - 1);
            }
            break;
    }
    return m;
}

std::string makerView(const Maker& m) {
    const int n = static_cast<int>(m.stops.size());
    std::string out = "maker|" + std::to_string(kTextVersion) + "|" + std::to_string(m.selected) + "|" + (n > 1 ? "1" : "0") + "|" +
                      (n < colour::kMaxStops ? "1" : "0") + "|" + joined(loopSwatch(m.stops)) + "\n";
    for (int i = 0; i < n; ++i) {
        const int rgb = m.stops[static_cast<size_t>(i)];
        out += "stop|" + std::to_string(i) + "|" + hex6(rgb) + "|" + (i == m.selected ? "1" : "0") + "|" +
               cell(panel::format("palette_stop", {std::to_string(i + 1), std::to_string(n), colour::hex(rgb)})) + "\n";
    }
    return out;
}

std::vector<int> stackColours(const int* stops, int count, float base, float span) {
    std::vector<int> out = setStops(stops, count);
    return out.empty() ? hueStops(base, span, kStackHueStops) : out;
}

std::string savedSwatch(const int* stops, int count, float base, float span) {
    const std::vector<int> set = setStops(stops, count);
    return joined(set.empty() ? hueSwatch(base, span) : loopSwatch(set));
}

std::string listView(const SceneParams& p, std::string_view customId, std::string_view savedIds, std::string_view savedNames) {
    const auto& builtIn = SceneParams::palettes();
    const int listed = SceneParams::kListedPalettes;
    const auto ids = lines(savedIds);
    const auto names = lines(savedNames);
    int selected = -1;
    if (!usesOwnColours(p)) {
        // A retired built-in (past the listed rows) still draws; the list shows no row for it.
        if (p.palette >= 0 && p.palette < listed) selected = p.palette;
    } else {
        for (size_t i = 0; i < ids.size(); ++i) {
            if (!customId.empty() && ids[i] == customId) {
                selected = listed + static_cast<int>(i);
                break;
            }
        }
    }
    std::string out = "list|" + std::to_string(kTextVersion) + "|" + std::to_string(selected) + "|" + cell(panel::text("look_summary_custom")) +
                      "|" + joined(lookSwatch(p)) + "\n";
    for (int i = 0; i < listed; ++i) out += "option|" + std::to_string(i) + "|-1|" + cell(builtIn[static_cast<size_t>(i)].name) + "\n";
    for (size_t i = 0; i < names.size(); ++i) out += "option|-1|" + std::to_string(i) + "|" + cell(names[i]) + "\n";
    return out;
}

std::string stackCard(std::string_view stackText, const SceneParams& p, int savedCount) {
    const palette::Stack s = palette::decode(stackText);
    const int layers = static_cast<int>(s.layers.size());
    std::string out = "card|" + std::to_string(kTextVersion) + "|" + cell(panel::text("palette_stack_heading")) + "|" +
                      cell(panel::text("palette_stack_explainer")) + "\n";
    for (int i = 0; i < layers && i < palette::kMaxLayers; ++i) {
        const auto& layer = s.layers[static_cast<size_t>(i)];
        const std::string field = "paletteAmount" + std::to_string(i);
        control::Range range{0.0f, 1.0f};
        control::of(field, range);
        out += "layer|" + std::to_string(i) + "|" + cell(layer.name) + "|" + field + "|" + tableNumber(p.get(field)) + "|" +
               tableNumber(range.min) + "|" + tableNumber(range.max) + "|" + (i > 0 ? "1" : "0") + "|" +
               cell(panel::format("palette_stack_up", {layer.name})) + "|" + cell(panel::format("palette_stack_remove", {layer.name})) + "|" +
               cell(panel::format("palette_stack_amount", {layer.name})) + "|" + joined(layerSwatch(layer)) + "\n";
    }
    if (layers > 1) {
        out += "blend|" + cell(panel::text("palette_stack_blend")) + "|" + (s.blend == palette::Blend::Mix ? "1" : "0") + "\n";
        out += std::string("blendoption|") + palette::name(palette::Blend::Share) + "|" + panel::text("palette_stack_share") + "\n";
        out += std::string("blendoption|") + palette::name(palette::Blend::Mix) + "|" + panel::text("palette_stack_mix") + "\n";
    }
    const auto& lib = palette::library();
    bool picks = false;
    for (size_t i = 0; i < lib.size(); ++i) picks = picks || palette::offered(static_cast<int>(i));
    if (layers < palette::kMaxLayers && picks) {
        out += "add|" + cell(panel::text("palette_stack_add")) + "|" + panel::text("palette_stack_add_mark") + "\n";
        for (size_t i = 0; i < lib.size(); ++i) {
            if (palette::offered(static_cast<int>(i))) out += "pick|" + std::to_string(i) + "|" + cell(lib[i].palette.name) + "\n";
        }
        if (savedCount > 0) out += "yours|" + cell(panel::text("palette_stack_add_yours")) + "|" + panel::text("palette_stack_add_mark") + "\n";
    }
    return out;
}

}  // namespace geode::viz::palettepanel
