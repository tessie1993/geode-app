#include "viz/Palette.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <optional>

#include "viz/TableNumber.hpp"

namespace geode::viz::palette {

namespace {

constexpr std::array<const char*, 2> kInterpNames = {"SMOOTH", "STEPS"};
constexpr std::array<const char*, 2> kWrapNames = {"MIRROR", "LOOP"};
constexpr std::array<const char*, 3> kSpaceNames = {"RGB", "HUE", "OKLAB"};
constexpr std::array<const char*, 3> kSingleNames = {"SHADES", "FLAT", "TO_BLACK"};
constexpr std::array<const char*, 6> kSourceNames = {"BUILT_IN", "COLOUR_MAP", "STYLE", "MILK", "ARTWORK", "YOURS"};
constexpr std::array<const char*, 2> kBlendNames = {"SHARE", "MIX"};
constexpr std::array<const char*, 2> kEdgeNames = {"SOFT", "HARD"};

template <typename E, size_t N>
const char* nameIn(const std::array<const char*, N>& names, E v) {
    const auto i = static_cast<size_t>(v);
    return i < N ? names[i] : names[0];
}

template <typename E, size_t N>
std::optional<E> named(const std::array<const char*, N>& names, std::string_view s) {
    for (size_t i = 0; i < N; ++i) {
        if (s == names[i]) return static_cast<E>(i);
    }
    return std::nullopt;
}

// Text the codec can carry and a label can show: no control characters, no field separator, no outer
// spaces, at most maxBytes without splitting a UTF-8 character (MacroBank's rule).
std::string cleanText(std::string_view raw, size_t maxBytes) {
    std::string out;
    for (const char ch : raw) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7f || ch == '|') continue;
        out += ch;
    }
    const size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    out.erase(0, first);
    if (out.size() > maxBytes) {
        size_t cut = maxBytes;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out.resize(cut);
    }
    out.erase(out.find_last_not_of(' ') + 1);
    return out;
}

std::vector<std::string_view> split(std::string_view line, char sep) {
    std::vector<std::string_view> out;
    size_t pos = 0;
    while (true) {
        const size_t at = line.find(sep, pos);
        if (at == std::string_view::npos) {
            out.push_back(line.substr(pos));
            return out;
        }
        out.push_back(line.substr(pos, at - pos));
        pos = at + 1;
    }
}

bool parseInt(std::string_view s, int& out) {
    const char* end = s.data() + s.size();
    const auto result = std::from_chars(s.data(), end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

bool parseFloat(std::string_view s, float& out) {
    if (s.empty() || s.size() > 31) return false;
    const std::string text(s);
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    if (end != text.c_str() + text.size() || !std::isfinite(v)) return false;
    out = v;
    return true;
}

colour::Rgb lerp(colour::Rgb a, colour::Rgb b, float f) {
    return {a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f};
}

colour::Rgb blend(colour::Rgb a, colour::Rgb b, float f, Space space) {
    switch (space) {
        case Space::Rgb: return lerp(a, b, f);
        case Space::Hue: {
            auto ha = colour::toHsv(a);
            auto hb = colour::toHsv(b);
            // A grey has no hue of its own: it takes its neighbour's, so a blend to grey does not swing round the wheel.
            if (ha[1] <= 0.0f) ha[0] = hb[0];
            if (hb[1] <= 0.0f) hb[0] = ha[0];
            const float dh = colour::wrap01(hb[0] - ha[0] + 0.5f) - 0.5f;
            return colour::fromHsv(ha[0] + dh * f, ha[1] + (hb[1] - ha[1]) * f, ha[2] + (hb[2] - ha[2]) * f);
        }
        case Space::Oklab: {
            const colour::Lab la = colour::toOklab(a);
            const colour::Lab lb = colour::toOklab(b);
            return colour::fromOklab({la.l + (lb.l - la.l) * f, la.a + (lb.a - la.a) * f, la.b + (lb.b - la.b) * f});
        }
    }
    return a;
}

struct Drawn {
    std::vector<Stop> stops;  // sorted by pos
    Space space = Space::Oklab;
};

// The stops a palette draws, sorted by position. One colour becomes the run its Single option names,
// always through OKLab: Shades from a dark to a light of the colour with the colour itself halfway, ToBlack
// from the colour to black.
Drawn drawn(const Palette& p) {
    Drawn d;
    d.space = p.space;
    if (p.stops.size() == 1) {
        const int c = p.stops[0].rgb;
        d.space = Space::Oklab;
        switch (p.single) {
            case Single::Flat: d.stops = {{c, 0.0f}}; break;
            case Single::ToBlack: d.stops = {{c, 0.0f}, {0x000000, 1.0f}}; break;
            case Single::Shades: {
                const colour::Lab lab = colour::toOklab(colour::unpack(c));
                const int dark = colour::pack(colour::fromOklab({lab.l * 0.25f, lab.a * 0.5f, lab.b * 0.5f}));
                const int light = colour::pack(colour::fromOklab({lab.l + (1.0f - lab.l) * 0.75f, lab.a * 0.35f, lab.b * 0.35f}));
                d.stops = {{dark, 0.0f}, {c, 0.5f}, {light, 1.0f}};
                break;
            }
        }
        return d;
    }
    d.stops = p.stops;
    std::stable_sort(d.stops.begin(), d.stops.end(), [](const Stop& a, const Stop& b) { return a.pos < b.pos; });
    return d;
}

colour::Rgb between(const Stop& a, const Stop& b, float f, Interp interp, Space space) {
    if (interp == Interp::Steps) return colour::unpack(f < 0.5f ? a.rgb : b.rgb);
    return blend(colour::unpack(a.rgb), colour::unpack(b.rgb), std::clamp(f, 0.0f, 1.0f), space);
}

// u along the run, the ends held: before the first stop its colour, after the last stop the last.
colour::Rgb openRun(const Drawn& d, Interp interp, float u) {
    const auto& s = d.stops;
    if (s.empty()) return {};
    if (s.size() == 1 || u <= s.front().pos) return colour::unpack(s.front().rgb);
    if (u >= s.back().pos) return colour::unpack(s.back().rgb);
    for (size_t i = 0; i + 1 < s.size(); ++i) {
        if (u < s[i + 1].pos) {
            const float gap = s[i + 1].pos - s[i].pos;
            return between(s[i], s[i + 1], gap > 0.0f ? (u - s[i].pos) / gap : 1.0f, interp, d.space);
        }
    }
    return colour::unpack(s.back().rgb);
}

// u round a loop of length 1: after the last stop the run blends on into the first, one turn later.
colour::Rgb loopRun(const Drawn& d, Interp interp, float u) {
    const auto& s = d.stops;
    if (s.empty()) return {};
    if (s.size() == 1) return colour::unpack(s.front().rgb);
    const Stop& first = s.front();
    const Stop& last = s.back();
    if (u < first.pos || u >= last.pos) {
        const float from = u < first.pos ? last.pos - 1.0f : last.pos;
        const float gap = first.pos + 1.0f - last.pos;
        return between(last, first, gap > 0.0f ? (u - from) / gap : 1.0f, interp, d.space);
    }
    return openRun(d, interp, u);
}

Palette classicEntry(std::string_view id, std::string_view name, float base, float range) {
    if (range >= 1.0f) return fromHueRamp(name, Source::BuiltIn, id, base, range);
    // One run of the ramp, t from 0 to 1, both ends kept: mirrored, it comes back the way it went.
    std::vector<int> colours;
    for (int i = 0; i < kMaxStops; ++i) {
        colours.push_back(colour::pack(colour::procedural(base, range, static_cast<float>(i) / static_cast<float>(kMaxStops - 1))));
    }
    Palette p = fromColours(Source::BuiltIn, id, name, colours.data(), static_cast<int>(colours.size()));
    p.space = Space::Rgb;
    return p;
}

Palette listed(std::string_view id, std::string_view label, Source source, Wrap wrap, Space space, std::initializer_list<int> colours) {
    const std::vector<int> c(colours);
    Palette p = fromColours(source, id, label, c.data(), static_cast<int>(c.size()));
    p.space = space;
    if (p.wrap != wrap) setOption(p, name(wrap));
    return p;
}

std::vector<Entry> buildLibrary() {
    std::vector<Entry> out;
#define GEODE_CLASSIC(id, name, base, range) out.push_back({id, classicEntry(id, name, base, range)});
#define GEODE_COLOURS(id, name, source, wrap, space, ...) \
    out.push_back({id, listed(id, name, Source::source, Wrap::wrap, Space::space, {__VA_ARGS__})});
#include "viz/PaletteTable.inc"
#undef GEODE_COLOURS
#undef GEODE_CLASSIC
    return out;
}

bool evenlySpread(const Palette& p, Wrap wrap) {
    const std::vector<float> even = spread(static_cast<int>(p.stops.size()), wrap);
    for (size_t i = 0; i < p.stops.size(); ++i) {
        if (std::fabs(p.stops[i].pos - even[i]) > 1e-4f) return false;
    }
    return true;
}

// A palette's share of t under Share, after the ones before it.
struct Part {
    size_t layer;
    float start;
    float width;
};

// Just short of 1: a palette's own colour at the very end of its share.
constexpr float kBeforeOne = 1.0f - 1e-6f;

// How far a soft edge reaches each way: kSoftReach, but at most a quarter of either share, so the middle half
// of every share stays pure however small it is.
float softHalf(float a, float b) { return std::min(kSoftReach, 0.25f * std::min(a, b)); }

float smooth(float x) {
    const float c = std::clamp(x, 0.0f, 1.0f);
    return c * c * (3.0f - 2.0f * c);
}

float amountOf(const Amounts& amounts, size_t i) {
    const float a = amounts[i];
    return std::isfinite(a) ? std::clamp(a, 0.0f, 1.0f) : 0.0f;
}

}  // namespace

static_assert(kMaxLayers == 8, "one paletteAmount row per layer (viz/ParamTable.inc)");

Amounts amountsOf(const SceneParams& p) {
    return {p.paletteAmount0, p.paletteAmount1, p.paletteAmount2, p.paletteAmount3,
            p.paletteAmount4, p.paletteAmount5, p.paletteAmount6, p.paletteAmount7};
}

Amounts defaultAmounts() { return amountsOf(SceneParams{}); }

bool Ramp::update(std::shared_ptr<const Stack> stack, const Amounts& amounts) {
    if (!stack || isUnset(*stack)) {
        const bool was = active();
        stack_.reset();
        return was;
    }
    Amounts read{};
    for (size_t i = 0; i < std::min(stack->layers.size(), read.size()); ++i) read[i] = amountOf(amounts, i);
    if (stack == stack_ && read == amounts_) return false;
    stack_ = std::move(stack);
    amounts_ = read;
    const std::vector<colour::Rgb> colours = compose(*stack_, amounts_);
    bytes_.resize(colours.size() * 3);
    for (size_t i = 0; i < colours.size(); ++i) {
        const int rgb = colour::pack(colours[i]);
        bytes_[3 * i] = static_cast<std::uint8_t>((rgb >> 16) & 0xFF);
        bytes_[3 * i + 1] = static_cast<std::uint8_t>((rgb >> 8) & 0xFF);
        bytes_[3 * i + 2] = static_cast<std::uint8_t>(rgb & 0xFF);
    }
    return true;
}

const char* name(Interp v) { return nameIn(kInterpNames, v); }
const char* name(Wrap v) { return nameIn(kWrapNames, v); }
const char* name(Space v) { return nameIn(kSpaceNames, v); }
const char* name(Single v) { return nameIn(kSingleNames, v); }
const char* name(Source v) { return nameIn(kSourceNames, v); }
const char* name(Blend v) { return nameIn(kBlendNames, v); }
const char* name(Edge v) { return nameIn(kEdgeNames, v); }

std::vector<float> spread(int n, Wrap wrap) {
    std::vector<float> out;
    if (n <= 0) return out;
    const int steps = wrap == Wrap::Mirror ? n - 1 : n;
    for (int i = 0; i < n; ++i) out.push_back(steps > 0 ? static_cast<float>(i) / static_cast<float>(steps) : 0.0f);
    return out;
}

Palette normalized(Palette p) {
    p.name = cleanText(p.name, kMaxNameBytes);
    p.ref = cleanText(p.ref, kMaxRefBytes);
    std::vector<Stop> kept;
    for (const Stop& s : p.stops) {
        if (!colour::valid(s.rgb) || static_cast<int>(kept.size()) >= kMaxStops) continue;
        kept.push_back({s.rgb, std::isfinite(s.pos) ? std::clamp(s.pos, 0.0f, 1.0f) : 0.0f});
    }
    p.stops = std::move(kept);
    if (static_cast<int>(p.interp) < 0 || static_cast<int>(p.interp) >= static_cast<int>(kInterpNames.size())) p.interp = Interp::Smooth;
    if (static_cast<int>(p.wrap) < 0 || static_cast<int>(p.wrap) >= static_cast<int>(kWrapNames.size())) p.wrap = Wrap::Mirror;
    if (static_cast<int>(p.space) < 0 || static_cast<int>(p.space) >= static_cast<int>(kSpaceNames.size())) p.space = Space::Oklab;
    if (static_cast<int>(p.single) < 0 || static_cast<int>(p.single) >= static_cast<int>(kSingleNames.size())) p.single = Single::Shades;
    if (static_cast<int>(p.source) < 0 || static_cast<int>(p.source) >= static_cast<int>(kSourceNames.size())) p.source = Source::Yours;
    return p;
}

bool valid(const Palette& p) { return !p.stops.empty(); }

Stack normalized(Stack s) {
    std::vector<Palette> kept;
    for (Palette& p : s.layers) {
        Palette n = normalized(std::move(p));
        if (valid(n) && static_cast<int>(kept.size()) < kMaxLayers) kept.push_back(std::move(n));
    }
    s.layers = std::move(kept);
    if (s.blend != Blend::Mix) s.blend = Blend::Share;
    if (s.edge != Edge::Hard) s.edge = Edge::Soft;
    return s;
}

Palette fromColours(Source source, std::string_view ref, std::string_view name, const int* rgb, int n) {
    Palette p;
    p.source = source;
    p.ref = std::string(ref);
    p.name = std::string(name);
    for (int i = 0; rgb && i < n && static_cast<int>(p.stops.size()) < kMaxStops; ++i) {
        if (colour::valid(rgb[i])) p.stops.push_back({rgb[i], 0.0f});
    }
    return spreadEvenly(normalized(std::move(p)));
}

colour::Rgb along(const Palette& p, float u) {
    const Drawn d = drawn(p);
    return openRun(d, p.interp, std::isfinite(u) ? std::clamp(u, 0.0f, 1.0f) : 0.0f);
}

colour::Rgb at(const Palette& p, float t) {
    const Drawn d = drawn(p);
    const float w = std::isfinite(t) ? colour::wrap01(t) : 0.0f;
    if (p.wrap == Wrap::Loop) return loopRun(d, p.interp, w);
    return openRun(d, p.interp, w < 0.5f ? 2.0f * w : 2.0f - 2.0f * w);
}

colour::Rgb at(const Stack& s, const Amounts& amounts, float t) {
    if (isUnset(s)) return at(unsetFallback(), t);
    const float w = std::isfinite(t) ? colour::wrap01(t) : 0.0f;
    float total = 0.0f;
    for (size_t i = 0; i < s.layers.size(); ++i) total += amountOf(amounts, i);
    if (s.layers.size() == 1 || total <= 1e-6f) return at(s.layers.front(), w);
    if (s.blend == Blend::Mix) {
        colour::Rgb sum{};
        for (size_t i = 0; i < s.layers.size(); ++i) {
            const float a = amountOf(amounts, i) / total;
            if (a <= 0.0f) continue;
            const colour::Rgb c = at(s.layers[i], w);
            sum = {sum.r + c.r * a, sum.g + c.g * a, sum.b + c.b * a};
        }
        return sum;
    }
    // Share: each layer draws its whole self across its own part of t, in stack order.
    std::vector<Part> parts;
    float start = 0.0f;
    for (size_t i = 0; i < s.layers.size(); ++i) {
        const float width = amountOf(amounts, i) / total;
        if (width <= 0.0f) continue;
        parts.push_back({i, start, width});
        start += width;
    }
    size_t k = parts.size() - 1;
    for (size_t j = 0; j < parts.size(); ++j) {
        if (w < parts[j].start + parts[j].width) {
            k = j;
            break;
        }
    }
    const Part& part = parts[k];
    const colour::Rgb own = at(s.layers[part.layer], std::clamp((w - part.start) / part.width, 0.0f, kBeforeOne));
    if (s.edge == Edge::Hard || parts.size() < 2) return own;
    // Soft: across each line the two palettes' colours there cross-fade, half each on the line itself, so the
    // ramp is continuous; t = 0 and 1 are a line too, between the last palette and the first.
    const Part& prev = parts[(k + parts.size() - 1) % parts.size()];
    const Part& next = parts[(k + 1) % parts.size()];
    const float fromLeft = w - part.start;
    const float toRight = part.start + part.width - w;
    const float left = softHalf(prev.width, part.width);
    const float right = softHalf(part.width, next.width);
    if (fromLeft < left) return lerp(at(s.layers[prev.layer], kBeforeOne), own, smooth((fromLeft + left) / (2.0f * left)));
    if (toRight < right) return lerp(at(s.layers[next.layer], 0.0f), own, smooth((toRight + right) / (2.0f * right)));
    return own;
}

std::vector<colour::Rgb> compose(const Stack& s, const Amounts& amounts, int samples) {
    std::vector<colour::Rgb> out;
    const int n = std::max(samples, 0);
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) out.push_back(at(s, amounts, (static_cast<float>(i) + 0.5f) / static_cast<float>(n)));
    return out;
}

bool isUnset(const Stack& s) { return s.layers.empty(); }

Stack resolveDefault(const Stack& s, const Palette& styleDefault) {
    if (!isUnset(s) || !valid(styleDefault)) return s;
    Stack out = s;
    out.layers.push_back(normalized(styleDefault));
    return out;
}

const Palette& unsetFallback() {
    static const Palette kSpectrum = [] {
        const auto& classic = SceneParams::palettes()[0];
        return fromHueRamp(classic.name, Source::BuiltIn, library()[0].id, classic.base, classic.range);
    }();
    return kSpectrum;
}

Stack addLayer(Stack s, const Palette& p) {
    if (!valid(normalized(p)) || static_cast<int>(s.layers.size()) >= kMaxLayers) return normalized(std::move(s));
    s.layers.push_back(p);
    return normalized(std::move(s));
}

Stack removeLayer(Stack s, int layer) {
    if (layer >= 0 && layer < static_cast<int>(s.layers.size())) s.layers.erase(s.layers.begin() + layer);
    return normalized(std::move(s));
}

Stack moveLayer(Stack s, int layer, int delta) {
    const int n = static_cast<int>(s.layers.size());
    if (layer < 0 || layer >= n) return normalized(std::move(s));
    const int to = std::clamp(layer + delta, 0, n - 1);
    Palette moved = std::move(s.layers[static_cast<size_t>(layer)]);
    s.layers.erase(s.layers.begin() + layer);
    s.layers.insert(s.layers.begin() + to, std::move(moved));
    return normalized(std::move(s));
}

Stack setLayer(Stack s, int layer, const Palette& p) {
    const int n = static_cast<int>(s.layers.size());
    if (!valid(normalized(p))) return normalized(std::move(s));
    if (layer >= 0 && layer < n) {
        s.layers[static_cast<size_t>(layer)] = p;
    } else if (layer == n && n < kMaxLayers) {
        s.layers.push_back(p);
    }
    return normalized(std::move(s));
}

Palette setStop(Palette p, int stop, int rgb, float pos) {
    if (stop < 0 || stop >= static_cast<int>(p.stops.size()) || !colour::valid(rgb)) return normalized(std::move(p));
    Stop& s = p.stops[static_cast<size_t>(stop)];
    s.rgb = rgb;
    if (pos >= 0.0f && std::isfinite(pos)) s.pos = pos;
    return normalized(std::move(p));
}

Palette addStop(Palette p, int rgb, float pos) {
    if (!colour::valid(rgb) || static_cast<int>(p.stops.size()) >= kMaxStops) return normalized(std::move(p));
    const bool append = !(pos >= 0.0f && std::isfinite(pos));
    p.stops.push_back({rgb, append ? 1.0f : pos});
    return append ? spreadEvenly(std::move(p)) : normalized(std::move(p));
}

Palette removeStop(Palette p, int stop) {
    if (stop >= 0 && stop < static_cast<int>(p.stops.size()) && p.stops.size() > 1) p.stops.erase(p.stops.begin() + stop);
    return normalized(std::move(p));
}

bool setOption(Palette& p, std::string_view option) {
    if (const auto v = named<Interp>(kInterpNames, option)) {
        p.interp = *v;
    } else if (const auto w = named<Wrap>(kWrapNames, option)) {
        const bool even = evenlySpread(p, p.wrap);
        p.wrap = *w;
        if (even) p = spreadEvenly(std::move(p));
    } else if (const auto s = named<Space>(kSpaceNames, option)) {
        p.space = *s;
    } else if (const auto one = named<Single>(kSingleNames, option)) {
        p.single = *one;
    } else {
        return false;
    }
    return true;
}

Palette spreadEvenly(Palette p) {
    p = normalized(std::move(p));
    const std::vector<float> even = spread(static_cast<int>(p.stops.size()), p.wrap);
    for (size_t i = 0; i < p.stops.size(); ++i) p.stops[i].pos = even[i];
    return p;
}

std::string encode(const Palette& palette) {
    const Palette p = normalized(palette);
    std::string out = std::string("palette|") + name(p.source) + "|" + p.ref + "|" + p.name + "|" + name(p.interp) + "|" + name(p.wrap) +
                      "|" + name(p.space) + "|" + name(p.single) + "|";
    for (size_t i = 0; i < p.stops.size(); ++i) {
        if (i > 0) out += ',';
        out += colour::hex(p.stops[i].rgb).substr(1) + "@" + tableNumber(p.stops[i].pos);
    }
    return out;
}

Palette decodePalette(std::string_view line) {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const std::vector<std::string_view> f = split(line, '|');
    Palette p;
    if (f.size() != 9 || f[0] != "palette") return p;
    const auto source = named<Source>(kSourceNames, f[1]);
    const auto interp = named<Interp>(kInterpNames, f[4]);
    const auto wrap = named<Wrap>(kWrapNames, f[5]);
    const auto space = named<Space>(kSpaceNames, f[6]);
    const auto single = named<Single>(kSingleNames, f[7]);
    if (!source || !interp || !wrap || !space || !single) return p;
    p.source = *source;
    p.ref = std::string(f[2]);
    p.name = std::string(f[3]);
    p.interp = *interp;
    p.wrap = *wrap;
    p.space = *space;
    p.single = *single;
    for (std::string_view stop : split(f[8], ',')) {
        const size_t at = stop.find('@');
        if (at == std::string_view::npos) continue;
        const int rgb = colour::parseHex(stop.substr(0, at));
        float pos = 0.0f;
        if (rgb == colour::kNone || stop.substr(0, at).size() != 6 || !parseFloat(stop.substr(at + 1), pos)) continue;
        p.stops.push_back({rgb, pos});
    }
    return normalized(std::move(p));
}

std::string encode(const Stack& stack) {
    const Stack s = normalized(stack);
    std::string out = "palettes|" + std::to_string(kTextVersion) + "|" + name(s.blend) + "|" + (s.fullRange ? "1" : "0") + "|" + name(s.edge) + "\n";
    for (const Palette& p : s.layers) out += encode(p) + "\n";
    return out;
}

Stack decode(std::string_view text) {
    Stack s;
    bool header = false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;
        if (!header) {
            const std::vector<std::string_view> f = split(line, '|');
            int version = 0;
            std::optional<Blend> blend;
            std::optional<Edge> edge;
            if (f.size() == 5) {
                blend = named<Blend>(kBlendNames, f[2]);
                edge = named<Edge>(kEdgeNames, f[4]);
            }
            if (f.size() != 5 || f[0] != "palettes" || !parseInt(f[1], version) || version != kTextVersion || !blend || !edge ||
                (f[3] != "0" && f[3] != "1")) {
                return {};
            }
            s.blend = *blend;
            s.fullRange = f[3] == "1";
            s.edge = *edge;
            header = true;
        } else {
            // A row this version does not know (a later PR's) is skipped, not fatal.
            Palette p = decodePalette(line);
            if (valid(p) && static_cast<int>(s.layers.size()) < kMaxLayers) s.layers.push_back(std::move(p));
        }
    }
    return header ? normalized(std::move(s)) : Stack{};
}

const std::vector<Entry>& library() {
    static const std::vector<Entry> kLibrary = buildLibrary();
    return kLibrary;
}

int libraryIndex(std::string_view id) {
    const auto& lib = library();
    for (size_t i = 0; i < lib.size(); ++i) {
        if (lib[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

const std::string& libraryText() {
    static const std::string kText = [] {
        const auto& lib = library();
        std::string out = "library|" + std::to_string(kTextVersion) + "|" + std::to_string(lib.size()) + "\n";
        for (size_t i = 0; i < lib.size(); ++i) {
            const Palette& p = lib[i].palette;
            out += "entry|" + std::to_string(i) + "|" + lib[i].id + "|" + name(p.source) + "|" + p.name + "|" + name(p.interp) + "|" +
                   name(p.wrap) + "|" + name(p.space) + "|" + name(p.single) + "|" + std::to_string(p.stops.size()) + "\n";
        }
        return out;
    }();
    return kText;
}

Palette fromHueRamp(std::string_view name, Source source, std::string_view ref, float base, float range) {
    // A ramp of whole turns (Spectrum) ends on the hue it starts on: its stops sit at i/16 and the last blends
    // back into the first. Any other ends elsewhere, at base + range: its stops run 0..1 inclusive and nothing
    // blends back, so the last sixteenth reaches that end as the old cosine did instead of turning toward base.
    const bool periodic = range != 0.0f && std::fabs(range - std::round(range)) < 1e-6f;
    const int steps = periodic ? kMaxStops : kMaxStops - 1;
    std::vector<int> colours;
    for (int i = 0; i < kMaxStops; ++i) {
        colours.push_back(colour::pack(colour::procedural(base, range, static_cast<float>(i) / static_cast<float>(steps))));
    }
    // fromColours spreads them at i/15 (a mirror's even spread), which the open run keeps as it is.
    Palette p = fromColours(source, ref, name, colours.data(), static_cast<int>(colours.size()));
    p.space = Space::Rgb;
    if (periodic) {
        setOption(p, "LOOP");
    } else {
        p.wrap = Wrap::Loop;
    }
    return p;
}

Stack fromLegacy(const SceneParams& p, Amounts& amounts) {
    amounts = defaultAmounts();
    Stack s;
    s.blend = Blend::Share;
    // Old ramps never met another under Share; Hard keeps that so, whatever the look becomes later.
    s.edge = Edge::Hard;
    const auto& lib = library();
    if (p.paletteLut >= 0) {
        // A colour map replaced the whole ramp.
        const int index = kColourMapFirst + std::min(p.paletteLut, kColourMapCount - 1);
        s.layers.push_back(lib[static_cast<size_t>(index)].palette);
        amounts[0] = 1.0f;
        return normalized(std::move(s));
    }
    const auto stops = p.paletteStops();
    const int count = p.paletteStopCount();
    if (count > 0) {
        // Your colours: evenly round a loop, blended in RGB; one was a flat colour.
        Palette yours = fromColours(Source::Yours, "", "Your colours", stops.data(), count);
        yours.space = Space::Rgb;
        yours.single = Single::Flat;
        setOption(yours, "LOOP");
        s.layers.push_back(std::move(yours));
    } else {
        const bool own = p.paletteBaseOverride >= 0.0f || p.paletteRangeOverride >= 0.0f;
        const int index = std::clamp(p.palette, 0, kClassicCount - 1);
        s.layers.push_back(own ? fromHueRamp("Gradient", Source::Yours, "", p.paletteBase(), p.paletteRange())
                               : fromHueRamp(SceneParams::palettes()[static_cast<size_t>(index)].name, Source::BuiltIn,
                                             lib[static_cast<size_t>(index)].id, p.paletteBase(), p.paletteRange()));
    }
    return normalized(std::move(s));
}

}  // namespace geode::viz::palette
