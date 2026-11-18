#include "viz/Births.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace geode::viz::births {

namespace {

constexpr float kTau = 6.28318531f;

// Lives per clock wrap: the scene clocks wrap at 7100 s, which is exactly this many periods.
constexpr int64_t kLivesPerWrap = 800;

// A well-mixed 32-bit integer from a birth's slot and life, so each life gets a spawn point of its own
// and the same clock always gives the same picture (export matches live).
uint32_t mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// The k-th seeded value of a life, 0..1.
float seeded(uint32_t seed, uint32_t k) {
    return static_cast<float>(mix(seed + 0x9e3779b9U * (k + 1U)) >> 8) / 16777216.0f;
}

float smooth(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

}  // namespace

std::array<float, kFloats> compute(float clock, float life) {
    std::array<float, kFloats> out{};
    // At Life 0 nothing is born: every value exactly 0, and the shaders draw as before.
    if (!(life > 0.0f)) return out;
    life = std::min(life, 1.0f);
    for (int b = 0; b < kBirths; ++b) {
        // Life brings the births in one at a time, each fading in over a third of the dial, so none pops.
        float presence = std::clamp(3.0f * life - static_cast<float>(b), 0.0f, 1.0f);
        if (!(presence > 0.0f)) continue;
        // Staggered a third of a life apart, so one is always being born while another dies.
        double t = static_cast<double>(clock) + static_cast<double>(b) * kPeriod / 3.0;
        double lives = std::floor(t / kPeriod);
        float u = static_cast<float>(t / kPeriod - lives);
        int64_t n = ((static_cast<int64_t>(lives) % kLivesPerWrap) + kLivesPerWrap) % kLivesPerWrap;
        uint32_t seed = mix(static_cast<uint32_t>(n) * 3U + static_cast<uint32_t>(b) + 1U);

        // Where it is born: off the centre, all round it, a little in front or behind.
        float at = kTau * seeded(seed, 0);
        float radius = 0.8f + 0.6f * seeded(seed, 1);
        float depth = 0.6f * (seeded(seed, 2) - 0.5f);
        // Where it drifts: outward, give or take, by up to half a unit as Life rises. A translation only.
        float way = at + 1.6f * (seeded(seed, 3) - 0.5f);
        float drift = 0.5f * life * smooth(0.1f, 0.75f, u);
        // The side it folds back or caves in from, fixed for its whole life.
        float side = kTau * seeded(seed, 4);

        // Born from nothing over the first quarter, collapsed by the end.
        float grown = smooth(0.0f, 0.25f, u);
        float collapse = smooth(0.7f, 1.0f, u);
        float* v = out.data() + 8 * b;
        v[0] = radius * std::cos(at) + drift * std::cos(way);
        v[1] = radius * std::sin(at) + drift * std::sin(way);
        v[2] = depth;
        v[3] = kMaxScale * grown * presence;
        // The kinds take turns: every slot moves on to the next each life, and the three slots never share one.
        v[4] = static_cast<float>((n + b) % 3);
        v[5] = collapse;
        v[6] = std::cos(side);
        v[7] = std::sin(side);
    }
    return out;
}

}  // namespace geode::viz::births
