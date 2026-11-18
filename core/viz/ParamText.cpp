#include "viz/ParamText.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <utility>

namespace geode::viz {
namespace {

constexpr float kDefaultEpsilon = 1e-4f;
constexpr std::string_view kMinus = "\xE2\x88\x92";    // U+2212
constexpr std::string_view kTimes = "\xC3\x97";        // U+00D7
constexpr std::string_view kDegrees = "\xC2\xB0";      // U+00B0

using P = ParamUnit;
constexpr std::array<std::pair<std::string_view, ParamUnit>, 21> kUnits{{
    {"Speed", P::Times},
    {"Zoom", P::Times},
    {"Rotation", P::Times},
    {"Audio drive", P::Times},
    {"Bass gain", P::Times},
    {"Mid gain", P::Times},
    {"Treble gain", P::Times},
    {"Particle size", P::Times},
    {"Tile", P::Times},
    {"Detail", P::Times},
    {"Fly speed", P::Times},
    {"Field of view", P::Times},
    {"Reaction strength", P::Times},
    {"Field scale", P::Times},
    {"Stirrer speed", P::Times},
    {"Hue shift", P::TurnDegrees},
    {"Fundamental (Hz)", P::Hertz},
    {"Particle life (s)", P::Seconds},
    {"Fluid curl", P::Plain},
    {"Flow curl", P::Plain},
    {"Kaleidoscope", P::Folds},
}};

// The nearest whole number, a half rounding up (toward +infinity); NaN is 0.
long long nearest(float v) {
    if (std::isnan(v)) return 0;
    const double r = std::floor(static_cast<double>(v) + 0.5);
    return static_cast<long long>(std::clamp(r, -2147483648.0, 2147483647.0));
}

// `v` to `places` decimals (1 or 2), a half rounding away from zero. A float times 100 is exact in a
// double, so the tie is seen as a tie.
std::string decimals(float v, int places) {
    if (std::isnan(v)) return "NaN";
    if (std::isinf(v)) return v < 0.0f ? "-Infinity" : "Infinity";
    const double scale = places == 1 ? 10.0 : 100.0;
    char digits[64];
    std::snprintf(digits, sizeof digits, "%.0f", std::floor(std::fabs(static_cast<double>(v)) * scale + 0.5));
    std::string text = digits;
    const auto width = static_cast<size_t>(places) + 1;
    if (text.size() < width) text.insert(0, width - text.size(), '0');
    text.insert(text.size() - static_cast<size_t>(places), 1, '.');
    return std::signbit(v) ? "-" + text : text;
}

// Two decimals with the trailing zeros gone: 1.80 is 1.8, 2.00 is 2.
std::string trimmed(float v) {
    std::string text = decimals(v, 2);
    if (text.find('.') != std::string::npos) {
        text.erase(text.find_last_not_of('0') + 1);
        if (text.back() == '.') text.pop_back();
    }
    return text == "-0" ? "0" : text;
}

std::string minusSigned(std::string text) {
    if (!text.empty() && text.front() == '-') text.replace(0, 1, kMinus);
    return text;
}

std::string percent(float value, bool bipolar) {
    const long long pct = nearest(value * 100.0f);
    if (!bipolar) return std::to_string(pct) + " %";
    if (pct > 0) return "+" + std::to_string(pct) + " %";
    if (pct < 0) return std::string(kMinus) + std::to_string(-pct) + " %";
    return "0 %";
}

std::string hertz(float value) {
    if (value >= 1000.0f) return decimals(value / 1000.0f, 1) + " kHz";
    if (value >= 10.0f) return std::to_string(nearest(value)) + " Hz";
    return trimmed(value) + " Hz";
}

}  // namespace

ParamUnit paramUnitFor(std::string_view key, float lo, float hi) {
    for (const auto& [name, unit] : kUnits) {
        if (name == key) return unit;
    }
    if (lo >= -1.0f && hi <= 1.0f) return ParamUnit::Percent;
    if (lo >= 0.0f && hi <= 2.0f) return ParamUnit::Times;
    return ParamUnit::Plain;
}

std::string paramText(float value, ParamUnit unit, float def, bool bipolar) {
    // The folds the shaders draw (SceneParams::kaleidoFoldCount, the same half-up rounding), its default "Off" too.
    if (unit == ParamUnit::Folds) {
        const long long folds = nearest(value);
        return folds < 2 ? "Off" : std::to_string(folds);
    }
    if (!std::isnan(def) && std::fabs(value - def) < kDefaultEpsilon) return "Default";
    switch (unit) {
        case ParamUnit::Percent:
            return percent(value, bipolar);
        case ParamUnit::Times:
            return minusSigned(trimmed(value)) + std::string(kTimes);
        case ParamUnit::Seconds:
            if (std::fabs(value) >= 1.0f) return minusSigned(decimals(value, 1)) + " s";
            return minusSigned(std::to_string(nearest(value * 1000.0f))) + " ms";
        case ParamUnit::Hertz:
            return hertz(value);
        case ParamUnit::TurnDegrees:
            return minusSigned(std::to_string(nearest(value * 360.0f))) + std::string(kDegrees);
        case ParamUnit::Plain:
        case ParamUnit::Folds:
            break;
    }
    return minusSigned(trimmed(value));
}

}  // namespace geode::viz
