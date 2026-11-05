#include "sound/offline/OfflineHarmony.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

#include "sound/offline/OfflineMath.hpp"

namespace geode::sound::offline {

namespace {

// ScaleTable.inc's rows, read the way live Harmony reads them. A copy of the struct rather than
// Harmony.hpp's, so the offline files build without the live engine's sources.
struct ScaleRow {
    const char* id;
    unsigned mask;     // pitch classes above the root, bit 0 = the root
    float brightness;  // -1 dark … 1 bright
    float tension;     // 0 calm … 1 tense
};
constexpr ScaleRow kScales[] = {
#include "sound/ScaleTable.inc"
};
constexpr int kScaleCount = static_cast<int>(sizeof(kScales) / sizeof(kScales[0]));
constexpr int kStates = kScaleCount * 12;

// Live Harmony's interval-class roughness: unison, minor second … tritone.
constexpr std::array<float, 7> kIntervalRoughness = {0.0f, 1.0f, 0.55f, 0.15f, 0.1f, 0.05f, 0.65f};

float hueOf(int pitchClass) { return static_cast<float>((pitchClass * 7) % 12) / 12.0f; }

// Live Harmony's chroma distance: 1 - the cosine of the two after each loses its mean, halved.
float distance(const std::array<double, 12>& a, const std::array<double, 12>& b) {
    double ma = 0.0, mb = 0.0;
    for (int i = 0; i < 12; ++i) ma += a[i], mb += b[i];
    ma /= 12.0;
    mb /= 12.0;
    double num = 0.0, da = 0.0, db = 0.0;
    for (int i = 0; i < 12; ++i) {
        num += (a[i] - ma) * (b[i] - mb);
        da += (a[i] - ma) * (a[i] - ma);
        db += (b[i] - mb) * (b[i] - mb);
    }
    if (da < 1e-12 || db < 1e-12) return 0.0f;
    return static_cast<float>(0.5 * (1.0 - num / std::sqrt(da * db)));
}

// Live Harmony::detectScale's score for every state (scale × 12 + root) on one slow chroma, and each
// state's fit (the correlation part alone). All zero for a chroma with nothing in it.
void scoreScales(const std::array<float, 12>& slow, std::vector<float>& score, std::vector<float>& fit) {
    score.assign(kStates, 0.0f);
    fit.assign(kStates, 0.0f);
    std::array<float, 12> c = slow;
    float top = 0.0f;
    for (float v : c) top = std::max(top, v);
    if (top <= 1e-9f) return;
    for (float& v : c) v /= top;
    float mean = 0.0f;
    for (float v : c) mean += v / 12.0f;
    for (float& v : c) v = std::min(1.0f, v / std::max(2.0f * mean, 1e-6f));
    mean = 0.0f;
    for (float v : c) mean += v / 12.0f;
    for (int s = 0; s < kScaleCount; ++s) {
        const unsigned mask = kScales[s].mask;
        int n = 0;
        for (int k = 0; k < 12; ++k) n += static_cast<int>((mask >> k) & 1u);
        for (int root = 0; root < 12; ++root) {
            float f = 0.0f;
            if (n < 12) {
                const float mm = static_cast<float>(n) / 12.0f;
                float num = 0.0f, dc = 0.0f;
                for (int k = 0; k < 12; ++k) {
                    const float v = c[(root + k) % 12] - mean;
                    num += v * ((((mask >> k) & 1u) ? 1.0f : 0.0f) - mm);
                    dc += v * v;
                }
                f = dc > 1e-9f ? num / std::sqrt(dc * mm * (1.0f - mm) * 12.0f) : 0.0f;
            }
            const float home = 0.5f * (c[root] - mean) + ((mask & (1u << 7)) ? 0.25f * (c[(root + 7) % 12] - mean) : 0.0f);
            score[s * 12 + root] = f + home;
            fit[s * 12 + root] = f;
        }
    }
}

}  // namespace

int harmonyScaleCount() { return kScaleCount; }
float harmonyScaleBrightness(int scale) { return kScales[std::clamp(scale, 0, kScaleCount - 1)].brightness; }
unsigned harmonyScaleMask(int scale) { return kScales[std::clamp(scale, 0, kScaleCount - 1)].mask; }

HarmonyTrack analyseHarmony(const Features& features) {
    HarmonyTrack out;
    const int frames = features.frames;
    if (frames <= 0 || features.hop <= 0) return out;
    const double frameRate = static_cast<double>(features.sampleRateHz) / features.hop;
    out.frameRate = frameRate;
    const size_t nf = static_cast<size_t>(frames);

    // The chord's colour and tension on the medium chroma, as live Harmony reads them.
    out.chordHue.assign(nf, 0.0f);
    std::vector<double> rawTension(nf, 0.0);
    std::vector<uint8_t> hasHue(nf, 0);
    for (int t = 0; t < frames; ++t) {
        const auto& m = features.chromaMedium[t];
        float mean = 0.0f;
        for (float v : m) mean += v / 12.0f;
        std::array<float, 12> above{};
        float x = 0.0f, y = 0.0f;
        for (int i = 0; i < 12; ++i) {
            above[i] = std::max(0.0f, m[i] - mean);
            const float a = 2.0f * std::numbers::pi_v<float> * hueOf(i);
            x += above[i] * std::cos(a);
            y += above[i] * std::sin(a);
        }
        if (x * x + y * y > 1e-6f) {
            const float hue = std::atan2(y, x) / (2.0f * std::numbers::pi_v<float>);
            out.chordHue[t] = hue - std::floor(hue);
            hasHue[t] = 1;
        }
        float rough = 0.0f, weight = 0.0f;
        for (int i = 0; i < 12; ++i)
            for (int j = i + 1; j < 12; ++j) {
                const int d = j - i;
                const float w = above[i] * above[j];
                rough += w * kIntervalRoughness[std::min(d, 12 - d)];
                weight += w;
            }
        rawTension[t] = weight > 1e-9f ? std::clamp((rough / weight - 0.1f) / 0.4f, 0.0f, 1.0f) : 0.0;
    }
    // Where no chord sounds, the colour holds the nearest one heard: the last before, else the first after.
    {
        int last = -1;
        for (int t = 0; t < frames; ++t) {
            if (hasHue[t]) last = t;
            else if (last >= 0) out.chordHue[t] = out.chordHue[last];
        }
        const auto first = std::find(hasHue.begin(), hasHue.end(), uint8_t{1});
        if (first != hasHue.end()) {
            const int f = static_cast<int>(first - hasHue.begin());
            for (int t = 0; t < f; ++t) out.chordHue[t] = out.chordHue[f];
        }
    }
    {
        const std::vector<double> smooth = math::centredMean(rawTension, math::framesFor(0.15, frameRate));
        out.tension.assign(smooth.begin(), smooth.end());
    }

    // Harmonic change: the second after against the second before, on the per-frame chroma.
    {
        const int w = math::framesFor(1.0, frameRate);
        std::vector<std::array<double, 12>> prefix(nf + 1, std::array<double, 12>{});
        for (int t = 0; t < frames; ++t)
            for (int p = 0; p < 12; ++p) prefix[t + 1][p] = prefix[t][p] + features.chroma[t][p];
        auto meanOver = [&](int a, int b) {
            std::array<double, 12> m{};
            a = std::clamp(a, 0, frames);
            b = std::clamp(b, 0, frames);
            if (b <= a) return m;
            for (int p = 0; p < 12; ++p) m[p] = (prefix[b][p] - prefix[a][p]) / (b - a);
            return m;
        };
        std::vector<float> novelty(nf, 0.0f);
        for (int t = 0; t < frames; ++t) novelty[t] = distance(meanOver(t - w, t), meanOver(t, t + w));
        constexpr float kOn = 0.15f;  // live's trigger level
        const int reach = math::framesFor(0.25, frameRate), refractory = math::framesFor(0.5, frameRate);
        int lastPick = -refractory - 1;
        for (int t = 0; t < frames; ++t) {
            if (novelty[t] <= kOn || t - lastPick < refractory) continue;
            bool peak = true;
            for (int d = std::max(0, t - reach); d <= std::min(frames - 1, t + reach) && peak; ++d)
                peak = d == t || novelty[d] < novelty[t] || (novelty[d] == novelty[t] && d > t);
            if (!peak) continue;
            out.changes.push_back(t / frameRate);
            out.changeStrength.push_back(std::clamp(novelty[t] / (2.0f * kOn), 0.05f, 1.0f));
            lastPick = t;
        }
    }

    // The scale: one Viterbi path over every scale on every root, scored per frame as live scores them.
    out.scale.assign(nf, -1);
    out.scaleRoot.assign(nf, 0);
    std::vector<double> fitPath(nf, 0.0);
    {
        // Live switches when another state scores 0.03 better for two seconds: that much evidence is the cost.
        const float cost = static_cast<float>(0.03 * 2.0 * frameRate);
        std::vector<float> score, fit, acc(kStates, 0.0f), next(kStates, 0.0f);
        std::vector<int> bestBefore(nf, 0);
        std::vector<bool> switched(nf * kStates, false);
        std::vector<uint8_t> heard(nf, 0);
        for (int t = 0; t < frames; ++t) {
            scoreScales(features.chromaSlow[t], score, fit);
            heard[t] = *std::max_element(features.chromaSlow[t].begin(), features.chromaSlow[t].end()) > 1e-9f;
            const int best = static_cast<int>(std::max_element(acc.begin(), acc.end()) - acc.begin());
            bestBefore[t] = best;
            for (int s = 0; s < kStates; ++s) {
                const float jump = acc[best] - cost;
                const bool sw = t > 0 && jump > acc[s];
                switched[static_cast<size_t>(t) * kStates + s] = sw;
                next[s] = (sw ? jump : acc[s]) + score[s];
            }
            // Keep the sums near zero: only differences matter, and a long track would lose float precision.
            const float top = *std::max_element(next.begin(), next.end());
            for (int s = 0; s < kStates; ++s) acc[s] = next[s] - top;
        }
        int s = static_cast<int>(std::max_element(acc.begin(), acc.end()) - acc.begin());
        for (int t = frames - 1; t >= 0; --t) {
            out.scale[t] = s / 12;
            out.scaleRoot[t] = s % 12;
            if (switched[static_cast<size_t>(t) * kStates + s]) s = bestBefore[t];
        }
        for (int t = 0; t < frames; ++t) {
            if (!heard[t]) {
                out.scale[t] = -1;
                out.scaleRoot[t] = 0;
                continue;
            }
            scoreScales(features.chromaSlow[t], score, fit);
            fitPath[t] = fit[out.scale[t] * 12 + out.scaleRoot[t]];
        }
    }
    {
        const int glide = math::framesFor(0.5, frameRate);
        std::vector<double> bright(nf, 0.5), tension(nf, 0.0), fitShaped(nf, 0.0);
        for (int t = 0; t < frames; ++t) {
            if (out.scale[t] < 0) continue;
            bright[t] = 0.5 + 0.5 * kScales[out.scale[t]].brightness;
            tension[t] = kScales[out.scale[t]].tension;
            fitShaped[t] = std::clamp((fitPath[t] - 0.3) / 0.5, 0.0, 1.0);
        }
        const std::vector<double> b = math::centredMean(bright, glide), te = math::centredMean(tension, glide),
                                  f = math::centredMean(fitShaped, glide);
        out.scaleBrightness.assign(b.begin(), b.end());
        out.scaleTension.assign(te.begin(), te.end());
        out.scaleFit.assign(f.begin(), f.end());
    }
    return out;
}

void sampleHarmony(const HarmonyTrack& h, double seconds, double dt, GeodeSoundFrame& out) {
    const int frames = static_cast<int>(h.scale.size());
    if (frames <= 0) return;
    const int t = math::frameAt(seconds, h.frameRate, frames);
    out.chordHue = h.chordHue[t];
    out.tension = h.tension[t];
    const int change = math::firstIn(h.changes, seconds, dt);
    out.harmonicChange = change >= 0 ? h.changeStrength[change] : 0.0f;
    out.scale = static_cast<float>(h.scale[t]);
    out.scaleRoot = static_cast<float>(h.scaleRoot[t]) / 12.0f;
    out.scaleBrightness = h.scaleBrightness[t];
    out.scaleTension = h.scaleTension[t];
    out.scaleFit = h.scaleFit[t];
}

}  // namespace geode::sound::offline
