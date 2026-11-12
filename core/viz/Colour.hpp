#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Your colours (Customize › Look): the colour maths the picker, the eyedropper and the renderer share.
// A colour is one packed 0xRRGGBB int, which a float carries exactly (24 bits), so it rides the params
// wire, presets and the look snapshot as an Int row like reactSeed. Header-only, so every test that
// links Params.cpp gets it without another source.
namespace geode::viz::colour {

inline constexpr int kNone = -1;
inline constexpr int kMaxStops = 5;
inline constexpr int kWhite = 0xFFFFFF;

struct Rgb {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

inline float wrap01(float x) { return x - std::floor(x); }

inline int channel(float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }

inline int pack(Rgb c) { return (channel(c.r) << 16) | (channel(c.g) << 8) | channel(c.b); }

inline Rgb unpack(int rgb) {
    const int c = std::clamp(rgb, 0, kWhite);
    return {static_cast<float>((c >> 16) & 0xFF) / 255.0f, static_cast<float>((c >> 8) & 0xFF) / 255.0f,
            static_cast<float>(c & 0xFF) / 255.0f};
}

inline bool valid(int rgb) { return rgb >= 0 && rgb <= kWhite; }

// h wraps (0..1 is one turn), s and v clamp to 0..1.
inline Rgb fromHsv(float h, float s, float v) {
    s = std::clamp(s, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    const float x = wrap01(h) * 6.0f;
    const int sector = static_cast<int>(x) % 6;
    const float f = x - std::floor(x);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - s * f);
    const float t = v * (1.0f - s * (1.0f - f));
    switch (sector) {
        case 0: return {v, t, p};
        case 1: return {q, v, p};
        case 2: return {p, v, t};
        case 3: return {p, q, v};
        case 4: return {t, p, v};
        default: return {v, p, q};
    }
}

// Hue 0..1 (0 for a grey, whose hue is undefined), saturation and value 0..1.
inline std::array<float, 3> toHsv(Rgb c) {
    const float hi = std::max({c.r, c.g, c.b});
    const float lo = std::min({c.r, c.g, c.b});
    const float d = hi - lo;
    float h = 0.0f;
    if (d > 0.0f) {
        if (hi == c.r) {
            h = (c.g - c.b) / d;
        } else if (hi == c.g) {
            h = 2.0f + (c.b - c.r) / d;
        } else {
            h = 4.0f + (c.r - c.g) / d;
        }
        h = wrap01(h / 6.0f);
    }
    return {h, hi > 0.0f ? d / hi : 0.0f, hi};
}

// The shell's accent for a palette: the middle of its hue span (base + range / 2, wrapped) at HSL saturation 0.85
// and lightness 0.6, bright enough to read on black. Ported from the Kotlin AppShell it replaces.
inline Rgb paletteAccent(float base, float range) {
    constexpr float kSaturation = 0.85f;
    constexpr float kLightness = 0.6f;
    const float v = kLightness + kSaturation * std::min(kLightness, 1.0f - kLightness);
    return fromHsv(base + range / 2.0f, 2.0f * (1.0f - kLightness / v), v);
}

// "#RRGGBB", "RRGGBB", "#RGB" or "RGB", either case, spaces around ignored; kNone for anything else.
inline int parseHex(std::string_view text) {
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
    if (!text.empty() && text.front() == '#') text.remove_prefix(1);
    if (text.size() != 3 && text.size() != 6) return kNone;
    int out = 0;
    for (char ch : text) {
        int d = 0;
        if (ch >= '0' && ch <= '9') {
            d = ch - '0';
        } else if (ch >= 'a' && ch <= 'f') {
            d = ch - 'a' + 10;
        } else if (ch >= 'A' && ch <= 'F') {
            d = ch - 'A' + 10;
        } else {
            return kNone;
        }
        out = (out << 4) | d;
        if (text.size() == 3) out = (out << 4) | d;
    }
    return out;
}

// "#RRGGBB", upper case; "" for a value that is not a colour.
inline std::string hex(int rgb) {
    if (!valid(rgb)) return {};
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out = "#000000";
    for (int i = 0; i < 6; ++i) out[static_cast<size_t>(6 - i)] = kDigits[(rgb >> (4 * i)) & 0xF];
    return out;
}

// The mean of n 0xAARRGGBB pixels (an Android bitmap's), alpha ignored: what the eyedropper takes
// under a finger, so one stray pixel of a noisy frame does not decide the colour.
inline int averageArgb(const int32_t* argb, int n) {
    if (!argb || n <= 0) return kNone;
    int64_t r = 0;
    int64_t g = 0;
    int64_t b = 0;
    for (int i = 0; i < n; ++i) {
        const auto px = static_cast<uint32_t>(argb[i]);
        r += (px >> 16) & 0xFF;
        g += (px >> 8) & 0xFF;
        b += px & 0xFF;
    }
    const auto mean = [n](int64_t sum) { return static_cast<int>((sum + n / 2) / n); };
    return (mean(r) << 16) | (mean(g) << 8) | mean(b);
}

// The built-in palettes' cosine ramp at t, as lib_palette's palProcedural draws one slot (before Hue
// shift and Hue range): 0.5 + 0.5 cos(2π(base + t·range + (0, 1/3, 2/3))).
inline Rgb procedural(float base, float range, float t) {
    constexpr float kTau = 6.2831f;
    const float h = base + t * range;
    return {0.5f + 0.5f * std::cos(kTau * h), 0.5f + 0.5f * std::cos(kTau * (h + 0.33f)),
            0.5f + 0.5f * std::cos(kTau * (h + 0.67f))};
}

// The set stops of a palette, in order: the leading run of valid colours (a gap ends it).
inline int count(const int* stops, int n) {
    int c = 0;
    while (c < n && c < kMaxStops && valid(stops[c])) ++c;
    return c;
}

// Your colours' ramp at t, as lib_palette's palStops draws it: the stops spaced evenly around a loop,
// each blending straight into the next and the last back into the first, so a pattern that wraps t
// shows no seam. One stop is a flat colour; none is black.
inline Rgb ramp(const int* stops, int n, float t) {
    const int c = count(stops, n);
    if (c == 0) return {};
    if (c == 1) return unpack(stops[0]);
    const float x = wrap01(t) * static_cast<float>(c);
    const int i = std::min(static_cast<int>(x), c - 1);
    const float f = x - static_cast<float>(i);
    const Rgb a = unpack(stops[i]);
    const Rgb b = unpack(stops[(i + 1) % c]);
    return {a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f};
}

// A hue gradient as stops: the cosine ramp sampled where ramp() puts each stop, so an old saved palette
// (a base hue and span) opens in the picker looking as it drew.
inline void stopsFromHues(float base, float range, int n, int* out) {
    const int c = std::clamp(n, 0, kMaxStops);
    for (int i = 0; i < c; ++i) out[i] = pack(procedural(base, range, static_cast<float>(i) / static_cast<float>(c)));
}

// The hue the first stop sits on, and how far round the wheel the stops reach from it: what the
// simulation and MilkDrop styles, which colour by one base hue and a span, take from your colours.
inline float baseHue(const int* stops, int n) { return count(stops, n) > 0 ? toHsv(unpack(stops[0]))[0] : 0.0f; }

inline float hueSpan(const int* stops, int n) {
    const int c = count(stops, n);
    if (c < 2) return 0.0f;
    const float h0 = toHsv(unpack(stops[0]))[0];
    float span = 0.0f;
    for (int i = 1; i < c; ++i) span = std::max(span, wrap01(toHsv(unpack(stops[i]))[0] - h0));
    return span;
}

// OKLab (Björn Ottosson's): lightness L 0..1 and two opponent axes, where equal steps look equal, so a
// palette blended through it does not grow the bright and dark bands an RGB or hue blend does.
struct Lab {
    float l = 0.0f;
    float a = 0.0f;
    float b = 0.0f;
};

inline float toLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
inline float fromLinear(float c) { return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }

inline Lab toOklab(Rgb c) {
    const float r = toLinear(c.r);
    const float g = toLinear(c.g);
    const float b = toLinear(c.b);
    const float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
    const float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
    const float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
    return {0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s, 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
            0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
}

// Out-of-gamut results clamp to 0..1 per channel.
inline Rgb fromOklab(Lab c) {
    const float l = c.l + 0.3963377774f * c.a + 0.2158037573f * c.b;
    const float m = c.l - 0.1055613458f * c.a - 0.0638541728f * c.b;
    const float s = c.l - 0.0894841775f * c.a - 1.2914855480f * c.b;
    const float l3 = l * l * l;
    const float m3 = m * m * m;
    const float s3 = s * s * s;
    const auto out = [](float v) { return std::clamp(fromLinear(std::max(v, 0.0f)), 0.0f, 1.0f); };
    return {out(4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3),
            out(-1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3),
            out(-0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3)};
}

// Recent colours (the picker's row): newest first, no repeats, at most kMaxRecent. The shell stores the text
// and hands it back; "#RRGGBB" joined by ','.
inline constexpr int kMaxRecent = 24;

namespace recent {

inline std::vector<int> push(std::vector<int> list, int rgb) {
    if (!valid(rgb)) return list;
    list.erase(std::remove(list.begin(), list.end(), rgb), list.end());
    list.insert(list.begin(), rgb);
    if (list.size() > static_cast<size_t>(kMaxRecent)) list.resize(static_cast<size_t>(kMaxRecent));
    return list;
}

// Unreadable entries are skipped, repeats and the overflow dropped, so any text reads as a valid list.
inline std::vector<int> decode(std::string_view text) {
    std::vector<int> out;
    size_t pos = 0;
    while (pos <= text.size() && out.size() < static_cast<size_t>(kMaxRecent)) {
        size_t end = text.find(',', pos);
        if (end == std::string_view::npos) end = text.size();
        const int rgb = parseHex(text.substr(pos, end - pos));
        if (valid(rgb) && std::find(out.begin(), out.end(), rgb) == out.end()) out.push_back(rgb);
        pos = end + 1;
    }
    return out;
}

inline std::string encode(const std::vector<int>& list) {
    std::string out;
    for (int rgb : list) {
        if (!valid(rgb)) continue;
        if (!out.empty()) out += ',';
        out += hex(rgb);
    }
    return out;
}

}  // namespace recent

}  // namespace geode::viz::colour
