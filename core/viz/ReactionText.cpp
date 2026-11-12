#include "viz/ReactionText.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "viz/ModTargets.hpp"
#include "viz/Reactions.hpp"
#include "viz/TableNumber.hpp"

namespace geode::viz::reactiontext {

namespace {

constexpr std::string_view kVersion = "reactions/2";
constexpr size_t kFields = 8;
constexpr int kFloats = ReactionEngine::kConfigFloats;

constexpr std::array<std::string_view, kReactModeCount> kModeNames = {"FOLLOW", "STEP", "SPIN", "JUMP", "TOGGLE"};
constexpr std::array<std::string_view, 4> kCurveNames = {"LINEAR", "EXPONENTIAL", "LOGARITHMIC", "SMOOTH"};
static_assert(kCurveNames.size() == static_cast<size_t>(ModCurve::Smooth) + 1);

bool isSpace(char c) {
    const auto u = static_cast<unsigned char>(c);
    return (u >= 9 && u <= 13) || (u >= 28 && u <= 32);
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}

// Lines end at "\r\n", "\n" or "\r".
std::vector<std::string_view> linesOf(std::string_view text) {
    std::vector<std::string_view> lines;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\n' && text[i] != '\r') continue;
        lines.push_back(text.substr(start, i - start));
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
        start = i + 1;
    }
    lines.push_back(text.substr(start));
    return lines;
}

std::vector<std::string_view> fieldsOf(std::string_view line) {
    std::vector<std::string_view> fields;
    size_t start = 0;
    for (size_t i = 0; i <= line.size(); ++i) {
        if (i < line.size() && line[i] != '|') continue;
        fields.push_back(line.substr(start, i - start));
        start = i + 1;
    }
    return fields;
}

int sourceNamed(std::string_view name) {
    for (int i = 0; i < kReactSourceCount; ++i) {
        if (name == reactSourceInfo(static_cast<ReactSource>(i)).name) return i;
    }
    return -1;
}

int targetNamed(std::string_view name) {
    for (int i = 0; i < modtargets::kTargetCount; ++i) {
        if (name == modtargets::name(static_cast<LfoTarget>(i))) return i;
    }
    return -1;
}

template <size_t N>
int indexOf(const std::array<std::string_view, N>& names, std::string_view name) {
    for (size_t i = 0; i < N; ++i) {
        if (names[i] == name) return static_cast<int>(i);
    }
    return -1;
}

// A float as the store wrote it, with or without a Java float suffix; `fallback` for anything else and for a
// value that is not finite.
float number(std::string_view field, float fallback) {
    std::string text(trim(field));
    if (!text.empty() && (text.back() == 'f' || text.back() == 'F' || text.back() == 'd' || text.back() == 'D')) text.pop_back();
    if (text.empty()) return fallback;
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    if (end != text.c_str() + text.size() || !std::isfinite(v)) return fallback;
    return v;
}

float clampTo(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool decodeRoute(std::string_view line, float* out) {
    const std::vector<std::string_view> f = fieldsOf(line);
    if (f.size() != kFields) return false;
    const int source = sourceNamed(f[1]);
    const int target = targetNamed(f[2]);
    if (source < 0 || target <= static_cast<int>(LfoTarget::None)) return false;
    const ReactRoute defaults;
    const int mode = indexOf(kModeNames, f[3]);
    const int curve = indexOf(kCurveNames, f[5]);
    out[0] = f[0] == "false" ? 0.0f : 1.0f;
    out[1] = static_cast<float>(source);
    out[2] = static_cast<float>(target);
    out[3] = static_cast<float>(mode < 0 ? static_cast<int>(defaults.mode) : mode);
    out[4] = clampTo(number(f[4], defaults.amount), kMinAmount, kMaxAmount);
    out[5] = static_cast<float>(curve < 0 ? static_cast<int>(defaults.curve) : curve);
    out[6] = clampTo(number(f[6], defaults.attackSeconds), 0.0f, kMaxAttackSeconds);
    out[7] = clampTo(number(f[7], defaults.releaseSeconds), 0.0f, kMaxReleaseSeconds);
    return true;
}

}  // namespace

std::string encode(const float* routes, int routeCount) {
    std::string text(kVersion);
    if (!routes) return text;
    const int n = routeCount < ReactionEngine::kRoutes ? routeCount : ReactionEngine::kRoutes;
    for (int i = 0; i < n; ++i) {
        const float* r = routes + static_cast<size_t>(i) * kFloats;
        const int source = static_cast<int>(r[1]);
        const int target = static_cast<int>(r[2]);
        const int mode = static_cast<int>(r[3]);
        const int curve = static_cast<int>(r[5]);
        if (source < 0 || source >= kReactSourceCount || target < 0 || target >= modtargets::kTargetCount || mode < 0 ||
            mode >= kReactModeCount) {
            continue;
        }
        const bool knownCurve = curve >= 0 && curve < static_cast<int>(kCurveNames.size());
        text += '\n';
        text += r[0] > 0.5f ? "true" : "false";
        text += '|';
        text += reactSourceInfo(static_cast<ReactSource>(source)).name;
        text += '|';
        text += modtargets::name(static_cast<LfoTarget>(target));
        text += '|';
        text += kModeNames[static_cast<size_t>(mode)];
        text += '|';
        text += tableNumber(r[4]);
        text += '|';
        text += kCurveNames[knownCurve ? static_cast<size_t>(curve) : 0];
        text += '|';
        text += tableNumber(r[6]);
        text += '|';
        text += tableNumber(r[7]);
    }
    return text;
}

int decode(std::string_view text, float* out) {
    const std::vector<std::string_view> lines = linesOf(text);
    if (trim(lines.front()) != kVersion) return -1;
    int count = 0;
    for (size_t i = 1; i < lines.size() && count < ReactionEngine::kRoutes; ++i) {
        const std::string_view line = trim(lines[i]);
        if (line.empty()) continue;
        if (decodeRoute(line, out + static_cast<size_t>(count) * kFloats)) ++count;
    }
    return count;
}

}  // namespace geode::viz::reactiontext
