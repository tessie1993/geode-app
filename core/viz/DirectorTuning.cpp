#include "viz/DirectorTuning.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <optional>

namespace geode::viz::director {

namespace {

// In Tune order. The defaults are the numbers the director used before they were settings (the lean,
// the glide range, the long stretch), and for what is new (returns, the arc, styles) the owner's ask:
// returns grow, the song climbs, the style progresses.
constexpr std::array<TuneSpec, kTunes> kSpecs = {{
    {"lean_speed", "Build: faster", "How much faster the look runs while a build leans toward its drop.", 0.0f, 1.0f, 0.3f, 0.0f, ""},
    {"lean_closer", "Build: closer", "How much the camera closes in while a build leans toward its drop.", 0.0f, 1.0f, 0.4f, 0.0f, ""},
    {"lean_rough", "Build: rougher", "How much rougher the look gets while a build leans toward its drop.", 0.0f, 1.0f, 0.25f, 0.0f, ""},
    {"lean_in_bars", "Build: lean in over", "Bars the build's lean takes to come in.", 1.0f, 16.0f, 4.0f, 1.0f, " bars"},
    {"lean_out_bars", "Build: let go over", "Bars it takes to let go when a build fades without a drop.", 0.25f, 8.0f, 1.0f, 0.25f, " bars"},
    {"glide_driven_bars", "Driven music: change over", "How a new look lands when the music feels powerful or joyful.", 0.25f, 4.0f, 0.25f, 0.25f, " bars"},
    {"glide_calm_bars", "Calm music: change over", "How a new look lands when the music feels peaceful or tender.", 0.25f, 8.0f, 2.0f, 0.25f, " bars"},
    {"return_growth", "Returns grow", "How far a returning section's look moves on each time it comes back.", 0.0f, 1.0f, 0.35f, 0.0f, ""},
    {"return_morph_bars", "Returns morph over", "The shortest morph into a returning section's look. A return never cuts.", 0.25f, 8.0f, 1.0f, 0.25f, " bars"},
    {"arc_depth", "Song arc", "How far the look travels as the song climbs to its peak and relaxes in breakdowns.", 0.0f, 1.0f, 1.0f, 0.0f, ""},
    {"arc_complexity", "Arc: complexity", "How much busier the look gets at the song's peak.", 0.0f, 1.0f, 0.3f, 0.0f, ""},
    {"arc_detail", "Arc: detail", "How much finer the detail gets at the song's peak.", 0.0f, 1.0f, 0.2f, 0.0f, ""},
    {"arc_fractal", "Arc: fractal", "How much deeper the fractal goes at the song's peak.", 0.0f, 1.0f, 0.4f, 0.0f, ""},
    {"arc_formula", "Arc: formula", "How far the formula travels at the song's peak.", 0.0f, 1.0f, 0.4f, 0.0f, ""},
    {"arc_phrases", "Arc: climb over", "Phrases until the song's arc has climbed half way.", 1.0f, 64.0f, 8.0f, 1.0f, " phrases"},
    {"arc_bars", "Arc: move over", "Bars the arc takes to move to a new section's level.", 1.0f, 16.0f, 4.0f, 1.0f, " bars"},
    {"long_seconds", "New look after", "Seconds with no new section or drop before a new look anyway.", 30.0f, 240.0f, 75.0f, 5.0f, " s"},
    {"styles_progress", "Styles progress", "On: each section steps the style on to a related one. Off: the style stays.", 0.0f, 1.0f, 1.0f, 1.0f, ""},
}};

float held(const TuneSpec& s, float v) {
    if (!std::isfinite(v)) return s.def;
    v = std::clamp(v, s.min, s.max);
    if (s.step > 0.0f) v = std::clamp(s.min + std::round((v - s.min) / s.step) * s.step, s.min, s.max);
    return v;
}

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

// strtof, not from_chars: the NDK's libc++ has no float from_chars on older API levels (as LayerState).
std::optional<float> floatOf(std::string_view s) {
    if (s.empty()) return std::nullopt;
    const std::string text(s);
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    if (end != text.c_str() + text.size()) return std::nullopt;
    return v;
}

int indexOf(std::string_view key) {
    for (int i = 0; i < kTunes; ++i) {
        if (key == kSpecs[static_cast<size_t>(i)].key) return i;
    }
    return -1;
}

std::mutex gLock;
std::shared_ptr<const Tuning> gTuning;  // guarded by gLock; null is the defaults

}  // namespace

std::span<const TuneSpec> tuneSpecs() { return kSpecs; }
const TuneSpec& spec(Tune t) { return kSpecs[static_cast<size_t>(t)]; }

Tuning::Tuning() {
    for (int i = 0; i < kTunes; ++i) v[static_cast<size_t>(i)] = kSpecs[static_cast<size_t>(i)].def;
}

const Tuning& defaultTuning() {
    static const Tuning kDefaults;
    return kDefaults;
}

Tuning parseTuning(std::string_view text) {
    Tuning t;
    while (!text.empty()) {
        const size_t nl = text.find('\n');
        const std::string_view line = trimmed(text.substr(0, nl));
        text = nl == std::string_view::npos ? std::string_view() : text.substr(nl + 1);
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const int i = indexOf(trimmed(line.substr(0, eq)));
        const std::optional<float> value = floatOf(trimmed(line.substr(eq + 1)));
        if (i < 0 || !value) continue;
        t.v[static_cast<size_t>(i)] = held(kSpecs[static_cast<size_t>(i)], *value);
    }
    return t;
}

std::string tuningText(const Tuning& t) {
    std::string out;
    char buf[32];
    for (int i = 0; i < kTunes; ++i) {
        std::snprintf(buf, sizeof buf, "%.6g", static_cast<double>(t.v[static_cast<size_t>(i)]));
        out += kSpecs[static_cast<size_t>(i)].key;
        out += '=';
        out += buf;
        out += '\n';
    }
    return out;
}

std::string tuningWith(std::string_view text, int index, float value) {
    Tuning t = parseTuning(text);
    if (index >= 0 && index < kTunes) t.v[static_cast<size_t>(index)] = held(kSpecs[static_cast<size_t>(index)], value);
    return tuningText(t);
}

std::string valueText(const Tuning& t, int index) {
    if (index < 0 || index >= kTunes) return {};
    const TuneSpec& s = kSpecs[static_cast<size_t>(index)];
    const float v = t.v[static_cast<size_t>(index)];
    if (isSwitch(s)) return v > 0.5f ? "On" : "Off";
    char buf[48];
    if (s.step >= 1.0f) {
        std::snprintf(buf, sizeof buf, "%.0f%s", static_cast<double>(v), s.unit);
    } else {
        std::snprintf(buf, sizeof buf, "%.2f%s", static_cast<double>(v), s.unit);
    }
    return buf;
}

void setTuning(std::string_view text) {
    auto next = std::make_shared<const Tuning>(parseTuning(text));
    std::lock_guard<std::mutex> guard(gLock);
    gTuning = std::move(next);
}

std::shared_ptr<const Tuning> tuning() {
    std::lock_guard<std::mutex> guard(gLock);
    if (gTuning) return gTuning;
    static const std::shared_ptr<const Tuning> kDefaults = std::make_shared<const Tuning>();
    return kDefaults;
}

}  // namespace geode::viz::director
