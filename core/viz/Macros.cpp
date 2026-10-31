#include "viz/Macros.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace geode::viz::macros {

namespace {

// One field a knob turns, over the range its Customize slider covers. Each field has one owner.
struct Drive {
    Macro macro;
    float SceneParams::*field;
    float lo;
    float hi;
    bool inverted;
};

constexpr std::array<Drive, 30> kDrives = {{
    {Macro::Energy, &SceneParams::reactAmount, 0.0f, 2.0f, false},
    {Macro::Energy, &SceneParams::audioDrive, 0.2f, 2.5f, false},
    {Macro::Energy, &SceneParams::motionAmount, 0.0f, 1.0f, false},
    {Macro::Energy, &SceneParams::bloom, 0.0f, 0.25f, false},
    {Macro::Energy, &SceneParams::intensity, 0.2f, 1.25f, false},
    {Macro::Speed, &SceneParams::speed, 0.05f, 4.0f, false},
    {Macro::Speed, &SceneParams::camFly, 0.0f, 2.0f, false},
    {Macro::Speed, &SceneParams::cycleSpeed, 0.02f, 0.6f, false},
    {Macro::Speed, &SceneParams::motionDrift, 0.0f, 1.0f, false},
    {Macro::Complexity, &SceneParams::tile, 1.0f, 6.0f, false},
    {Macro::Complexity, &SceneParams::warp, 0.0f, 1.0f, false},
    {Macro::Complexity, &SceneParams::twist, -1.0f, 1.0f, false},
    {Macro::Complexity, &SceneParams::morph, 0.0f, 1.0f, false},
    {Macro::Mood, &SceneParams::temperature, -1.0f, 1.0f, false},
    {Macro::Mood, &SceneParams::saturation, 0.0f, 1.5f, false},
    {Macro::Mood, &SceneParams::brightness, 0.2f, 1.25f, false},
    {Macro::Mood, &SceneParams::contrast, 0.3f, 1.25f, false},
    {Macro::Mood, &SceneParams::hueRange, 0.0f, 1.5f, false},
    {Macro::Space, &SceneParams::camFov, 0.5f, 2.0f, false},
    {Macro::Space, &SceneParams::zoom, 0.3f, 3.0f, true},
    {Macro::Space, &SceneParams::camOrbit, 0.0f, 1.0f, false},
    {Macro::Space, &SceneParams::motionBreath, 0.0f, 1.0f, false},
    {Macro::Space, &SceneParams::vignette, 0.0f, 1.0f, true},
    {Macro::Space, &SceneParams::fisheye, -1.0f, 1.0f, false},
    {Macro::Chaos, &SceneParams::turbulence, 0.0f, 1.5f, false},
    {Macro::Chaos, &SceneParams::glitch, 0.0f, 0.25f, false},
    {Macro::Chaos, &SceneParams::chromaAb, 0.0f, 1.0f, false},
    {Macro::Chaos, &SceneParams::ripple, 0.0f, 1.0f, false},
    {Macro::Chaos, &SceneParams::camShake, 0.0f, 1.0f, false},
    {Macro::Chaos, &SceneParams::grain, 0.0f, 1.0f, false},
}};

// Full-turn routes; the amount scales with the knob.
struct SoundRoute {
    Macro macro;
    ReactSource source;
    LfoTarget target;
    float amount;
    float attack;
    float release;
};

constexpr std::array<SoundRoute, kRoutes> kSound = {{
    {Macro::Energy, ReactSource::BassAtt, LfoTarget::Bloom, 0.3f, 0.02f, 0.3f},
    {Macro::Speed, ReactSource::CurvedTime, LfoTarget::Speed, 0.4f, 0.3f, 0.6f},
    {Macro::Complexity, ReactSource::DensitySlow, LfoTarget::Morph, 0.4f, 0.3f, 0.8f},
    {Macro::Mood, ReactSource::PitchHeight, LfoTarget::Temperature, 0.3f, 0.3f, 0.8f},
    {Macro::Space, ReactSource::StereoPan, LfoTarget::DriftX, 0.3f, 0.3f, 0.6f},
    {Macro::Chaos, ReactSource::KickRandom, LfoTarget::Twist, 0.5f, 0.05f, 0.1f},
    {Macro::Chaos, ReactSource::SnareToggle, LfoTarget::ChromaAb, 0.4f, 0.03f, 0.03f},
}};
static_assert(kRoutes <= ReactionEngine::kRoutes, "the macro routes fit one reaction engine");

constexpr float kSurpriseReach = 0.8f;

// ×0.5 at -1, ×2 at +1: a doubling either way reads as "much more" without blowing out.
float doubling(float x) { return std::exp2(x); }

float knob(const MacroKnobs& k, Macro m) { return k[static_cast<size_t>(m)]; }

float clampKnob(float v) { return std::isfinite(v) ? std::clamp(v, -1.0f, 1.0f) : 0.0f; }

std::array<std::atomic<float>, kMacroCount> gKnobs{};

const std::array<Starter, kStarterCount> kStarters = {{
    //            Energy Speed  Complex Mood   Space  Chaos
    {"As styled", {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}},
    {"Calm", {-0.6f, -0.6f, -0.3f, -0.2f, 0.3f, -0.8f}},
    {"Groove", {0.3f, 0.2f, 0.1f, 0.2f, 0.0f, -0.2f}},
    {"Rave", {0.9f, 0.7f, 0.4f, 0.5f, -0.2f, 0.5f}},
    {"Dreamy", {-0.3f, -0.4f, 0.4f, 0.4f, 0.6f, -0.5f}},
    {"Wide", {0.1f, -0.1f, -0.2f, 0.0f, 1.0f, -0.3f}},
    {"Glitch", {0.5f, 0.3f, 0.6f, -0.3f, -0.3f, 1.0f}},
}};

}  // namespace

MacroKnobs clamped(const float* values, int count) {
    MacroKnobs k{};
    for (int i = 0; i < kMacroCount && values && i < count; ++i) k[static_cast<size_t>(i)] = clampKnob(values[i]);
    return k;
}

void setGlobal(const MacroKnobs& knobs) {
    for (int i = 0; i < kMacroCount; ++i) gKnobs[static_cast<size_t>(i)].store(clampKnob(knobs[static_cast<size_t>(i)]), std::memory_order_relaxed);
}

MacroKnobs global() {
    MacroKnobs k{};
    for (int i = 0; i < kMacroCount; ++i) k[static_cast<size_t>(i)] = gKnobs[static_cast<size_t>(i)].load(std::memory_order_relaxed);
    return k;
}

SceneParams apply(const SceneParams& p, const MacroKnobs& k) {
    SceneParams out = p;
    for (const Drive& d : kDrives) {
        const float x = knob(k, d.macro);
        if (x == 0.0f) continue;
        const float up = d.inverted ? d.lo : d.hi;
        const float down = d.inverted ? d.hi : d.lo;
        float& v = out.*d.field;
        const float base = std::clamp(v, d.lo, d.hi);
        v = std::clamp(base + std::fabs(x) * ((x > 0.0f ? up : down) - base), d.lo, d.hi);
    }
    return out;
}

GeodeSoundTuning tuning(const GeodeSoundTuning& base, const MacroKnobs& k) {
    GeodeSoundTuning t = base;
    t.reactivity = std::clamp(base.reactivity * doubling(knob(k, Macro::Energy)), 0.0f, 3.0f);
    t.speed = std::clamp(base.speed * doubling(knob(k, Macro::Speed)), 0.0f, 4.0f);
    return t;
}

CameraRig::Moves camera(const MacroKnobs& k) {
    const float space = 1.0f + knob(k, Macro::Space);
    const float chaos = knob(k, Macro::Chaos);
    CameraRig::Moves m;
    m.orbit = space;
    m.lean = space;
    m.dolly = space;
    m.breath = space;
    m.shake = std::max(0.0f, 1.0f + 1.5f * chaos);
    m.reframe = 1.0f + chaos;
    m.cut = 1.0f + chaos;
    return m;
}

std::array<ReactRoute, ReactionEngine::kRoutes> routes(const MacroKnobs& k) {
    std::array<ReactRoute, ReactionEngine::kRoutes> out{};
    for (size_t i = 0; i < kSound.size(); ++i) {
        const SoundRoute& s = kSound[i];
        const float x = knob(k, s.macro);
        ReactRoute& r = out[i];
        r.enabled = x > 0.0f;
        r.source = s.source;
        r.target = s.target;
        r.mode = ReactMode::Follow;
        r.amount = s.amount * std::max(x, 0.0f);
        r.curve = ModCurve::Linear;
        r.attackSeconds = s.attack;
        r.releaseSeconds = s.release;
    }
    return out;
}

MacroKnobs surprise(const MacroKnobs& k, unsigned lockMask, uint64_t seed) {
    MacroKnobs out = k;
    uint64_t x = seed;
    for (int i = 0; i < kMacroCount; ++i) {
        // splitmix64, one draw per knob whether locked or not, so a lock never shifts the others.
        x += 0x9E3779B97F4A7C15ull;
        uint64_t z = x;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        if (lockMask & (1u << i)) continue;
        const float u = static_cast<float>(z >> 40) / 16777216.0f;  // 0..1
        out[static_cast<size_t>(i)] = (2.0f * u - 1.0f) * kSurpriseReach;
    }
    return out;
}

const std::array<Starter, kStarterCount>& starters() { return kStarters; }

}  // namespace geode::viz::macros
