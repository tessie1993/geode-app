#include "viz/WaveRandomizer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "viz/ModTargets.hpp"

namespace geode::viz::waves {

namespace {

using T = LfoTarget;

constexpr int kColourKind = 2;
constexpr int kEngineKind = 4;

const std::array<std::vector<T>, kKinds>& kinds() {
    static const std::array<std::vector<T>, kKinds> kTable = {{
        {T::Speed, T::Zoom, T::Rotation, T::Sway, T::DriftX, T::DriftY, T::Twist},
        {T::Warp, T::Ripple, T::Morph, T::Turbulence, T::Fisheye, T::ChromaAb},
        {T::ColorShift, T::PaletteMix, T::Saturation, T::Temperature, T::Bloom, T::Vignette},
        {T::CamFly, T::CamOrbit, T::CamPush, T::CamYaw, T::CamPitch, T::CamRoll, T::CamFov},
        {T::ParticleSize, T::TrailLength, T::FluidCurl, T::FluidRadius, T::FluidForce, T::FluidGlow, T::FlowStrength, T::FlowCurl,
         T::FlowForce, T::EndlessZoomSpeed},
    }};
    return kTable;
}

bool isLiquid(T target) { return target == T::FlowStrength || target == T::FlowCurl || target == T::FlowForce; }

// splitmix64: small, and the same on every compiler.
struct Rng {
    std::uint64_t state;
    std::uint64_t next() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    float unit() { return static_cast<float>(next() >> 40) / 16777216.0f; }
    int below(int n) { return static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
    float between(float lo, float hi) { return lo + (hi - lo) * unit(); }
    float logBetween(float lo, float hi) { return lo * std::pow(hi / lo, unit()); }
    template <typename V, size_t N>
    V weighted(const std::array<std::pair<V, int>, N>& choices) {
        int total = 0;
        for (const auto& c : choices) total += c.second;
        int at = below(total);
        for (const auto& c : choices) {
            if (at < c.second) return c.first;
            at -= c.second;
        }
        return choices[0].first;
    }
};

ModPolarity polarityFor(T target, const SceneParams& params) {
    const int slot = paramSlotOf(target);
    if (slot < 0) return ModPolarity::Bipolar;
    const ParamSpec& spec = SceneParams::spec(static_cast<ParamSlot>(slot));
    if (spec.kind == ParamKind::Circular || !(spec.max > spec.min)) return ModPolarity::Bipolar;
    const float at = (params.getSlot(slot) - spec.min) / (spec.max - spec.min);
    if (at < 0.2f) return ModPolarity::Positive;
    if (at > 0.8f) return ModPolarity::Negative;
    return ModPolarity::Bipolar;
}

LfoConfig rollOne(T target, const SceneParams& params, Rng& rng) {
    const bool colour = kindOf(target) == kColourKind;
    const bool calm = colour || kindOf(target) == kCameraKind;
    LfoConfig c;
    c.enabled = true;
    c.source = ModSource::Lfo;
    c.target = target;
    using W = std::pair<LfoWave, int>;
    c.wave = calm ? rng.weighted(std::array<W, 3>{{{LfoWave::Sine, 45}, {LfoWave::Triangle, 25}, {LfoWave::RandomGlide, 30}}})
                  : rng.weighted(std::array<W, 6>{{{LfoWave::Sine, 30}, {LfoWave::Triangle, 20}, {LfoWave::RandomGlide, 25},
                                                   {LfoWave::Random, 10}, {LfoWave::Saw, 10}, {LfoWave::Square, 5}}});
    const float period = calm ? rng.logBetween(4.0f, 24.0f) : rng.logBetween(1.0f, 12.0f);
    c.rateSeconds = std::clamp(period, LfoConfig::kMinRateSeconds, LfoConfig::kMaxRateSeconds);
    c.beats = 0.0f;
    if (rng.below(5) < 2) {
        constexpr float kBeats[] = {2.0f, 4.0f, 8.0f, 16.0f};
        c.beats = calm ? kBeats[1 + rng.below(3)] : kBeats[rng.below(4)];
    }
    c.depth = colour ? rng.between(0.1f, 0.3f) : rng.between(0.15f, 0.45f);
    c.polarity = polarityFor(target, params);
    c.curve = rng.below(4) == 0 ? ModCurve::Smooth : ModCurve::Linear;
    return c;
}

template <typename E>
E enumAt(float value, E last) {
    const int i = static_cast<int>(value);
    return i >= 0 && i <= static_cast<int>(last) ? static_cast<E>(i) : E{};
}

}  // namespace

int kindOf(LfoTarget target) {
    for (int k = 0; k < kKinds; ++k) {
        const auto& list = kinds()[static_cast<size_t>(k)];
        if (std::find(list.begin(), list.end(), target) != list.end()) return k;
    }
    return -1;
}

Set randomize(const Set& current, const SceneParams& params, std::uint64_t seed, const std::string& styleId, Roll roll) {
    Set out = current;
    Rng rng{seed};
    std::array<bool, LfoEngine::kSlots> rolls{};
    std::array<bool, kKinds> kindTaken{};
    std::vector<T> taken;
    for (int i = 0; i < LfoEngine::kSlots; ++i) {
        const LfoConfig& c = current[static_cast<size_t>(i)];
        const bool running = c.enabled && c.source == ModSource::Lfo && c.target != T::None;
        rolls[static_cast<size_t>(i)] = roll == Roll::All || running;
        if (rolls[static_cast<size_t>(i)] || !c.enabled) continue;
        // A wave that stays: the rolled ones keep off its target and its kind of control.
        taken.push_back(c.target);
        if (const int kind = kindOf(c.target); kind >= 0) kindTaken[static_cast<size_t>(kind)] = true;
    }
    std::array<int, kKinds> order{0, 1, 2, 3, kEngineKind};
    for (int i = kKinds - 1; i > 0; --i) std::swap(order[static_cast<size_t>(i)], order[static_cast<size_t>(rng.below(i + 1))]);

    for (int i = 0; i < LfoEngine::kSlots; ++i) {
        if (!rolls[static_cast<size_t>(i)]) continue;
        // The first kind in this roll's order that still has a target for the slot; a kind already used
        // only when no fresh one has any.
        T picked = T::None;
        for (const bool fresh : {true, false}) {
            for (const int kind : order) {
                if (fresh == kindTaken[static_cast<size_t>(kind)]) continue;
                std::vector<T> open;
                for (const T t : kinds()[static_cast<size_t>(kind)]) {
                    if (std::find(taken.begin(), taken.end(), t) != taken.end()) continue;
                    if (isLiquid(t) && !params.flowEnabled) continue;
                    if (modtargets::offeredTo(t, styleId, i)) open.push_back(t);
                }
                if (open.empty()) continue;
                picked = open[static_cast<size_t>(rng.below(static_cast<int>(open.size())))];
                kindTaken[static_cast<size_t>(kind)] = true;
                break;
            }
            if (picked != T::None) break;
        }
        if (picked == T::None) continue;
        taken.push_back(picked);
        out[static_cast<size_t>(i)] = rollOne(picked, params, rng);
    }
    return out;
}

Set fromWire(const float* values) {
    Set set{};
    for (int i = 0; i < LfoEngine::kSlots; ++i) {
        const float* c = values + i * GEODE_LFO_CONFIG_FLOATS;
        LfoConfig& o = set[static_cast<size_t>(i)];
        o.enabled = c[0] > 0.5f;
        o.source = enumAt(c[1], ModSource::StereoPan);
        o.target = enumAt(c[2], LfoTarget::DiveAimY);
        o.wave = enumAt(c[3], LfoWave::RandomGlide);
        o.rateSeconds = c[4];
        o.depth = c[5];
        o.polarity = enumAt(c[6], ModPolarity::Negative);
        o.curve = enumAt(c[7], ModCurve::Smooth);
        o.beats = c[8];
    }
    return set;
}

void toWire(const Set& set, float* out) {
    for (int i = 0; i < LfoEngine::kSlots; ++i) {
        float* c = out + i * GEODE_LFO_CONFIG_FLOATS;
        const LfoConfig& o = set[static_cast<size_t>(i)];
        c[0] = o.enabled ? 1.0f : 0.0f;
        c[1] = static_cast<float>(o.source);
        c[2] = static_cast<float>(o.target);
        c[3] = static_cast<float>(o.wave);
        c[4] = o.rateSeconds;
        c[5] = o.depth;
        c[6] = static_cast<float>(o.polarity);
        c[7] = static_cast<float>(o.curve);
        c[8] = o.beats;
    }
}

}  // namespace geode::viz::waves
