#include "sound/offline/OfflineEmotion.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "sound/offline/OfflineMath.hpp"

namespace geode::sound::offline {

namespace {

// EmotionTable.inc, read the way live Emotion reads it (its tables are private to Emotion.cpp, which the
// offline files do not link).
const Emotion::Mode kModes[] = {
#define GEODE_EMOTION_MODES
#include "sound/EmotionTable.inc"
#undef GEODE_EMOTION_MODES
};
const Emotion::Key kKeys[] = {
#define GEODE_EMOTION_KEYS
#include "sound/EmotionTable.inc"
#undef GEODE_EMOTION_KEYS
};
constexpr int kModeCount = static_cast<int>(sizeof(kModes) / sizeof(kModes[0]));
static_assert(sizeof(kKeys) / sizeof(kKeys[0]) == 24, "12 major and 12 minor keys");

// Live Emotion's constants.
constexpr int kIonian = 0;
constexpr int kAeolian = 5;
constexpr float kScaleTrust = 0.2f;
constexpr float kKeyTrust = 0.2f;
constexpr int kGems = Emotion::kGems;

struct Quale {
    float valence;
    float tension;
};
// Huron's scale-degree qualia, as live Emotion weighs them.
constexpr std::array<Quale, 12> kQualia = {{
    {0.30f, 0.00f}, {-0.30f, 0.40f}, {0.10f, 0.10f}, {-0.30f, 0.10f}, {0.30f, 0.00f}, {0.00f, 0.20f},
    {0.05f, 0.40f}, {0.20f, 0.00f}, {-0.35f, 0.20f}, {0.20f, 0.00f}, {0.00f, 0.10f}, {0.00f, 0.45f},
}};

float clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
int pitchClass(float fraction) { return (static_cast<int>(std::lround(fraction * 12.0f)) % 12 + 12) % 12; }

void normalise(Emotion::GemsVec& g) {
    float sum = 0.0f;
    for (float& v : g) {
        v = std::max(v, 0.0f);
        sum += v;
    }
    for (float& v : g) v = sum > 0.0f ? v / sum : 1.0f / kGems;
}

Emotion::Fingerprint fingerprintOf(int scale, int root) {
    const Emotion::Mode& m = kModes[std::clamp(scale, 0, kModeCount - 1)];
    const Emotion::Key& k = kKeys[((root % 12 + 12) % 12) + (m.majorThird ? 0 : 12)];
    Emotion::Fingerprint out;
    out.valence = m.valence;
    out.energy = m.energy;
    out.tension = m.tension;
    out.sharpness = k.sharpness;
    for (int i = 0; i < kGems; ++i) out.gems[i] = Emotion::kModeWeight * m.gems[i] + (1.0f - Emotion::kModeWeight) * k.gems[i];
    normalise(out.gems);
    return out;
}

std::vector<double> smooth(const std::vector<double>& x, double seconds, double rate) {
    return math::centredMean(x, math::framesFor(seconds, rate));
}

}  // namespace

EmotionTrack analyseEmotion(const Track& track) {
    EmotionTrack out;
    const int frames = track.frames;
    if (frames <= 0) return out;
    const double rate = track.frameRate;
    out.frameRate = rate;
    const size_t nf = static_cast<size_t>(frames);

    // The live cues, frame by frame, read through the same schema the director reads.
    std::vector<int> scale(nf, -1), root(nf, 0), tonicOf(nf, 0), lead(nf, -1);
    std::vector<double> conf(nf, 0.0), pv(nf, 0.0), pe(nf, 0.0), pt(nf, 0.0), ps(nf, 0.0);
    std::vector<std::array<double, kGems>> pg(nf);
    std::vector<double> loud(nf), tempo(nf), bright(nf), rise(nf), harmonyTension(nf), buildUp(nf);
    std::vector<uint8_t> fingerprinted(nf, 0);
    GeodeSoundFrame f{};
    for (int t = 0; t < frames; ++t) {
        sampleTrack(track, t / rate, 1.0 / rate, f);
        int s = -1, r = 0;
        float c = 0.0f;
        if (f.scale >= 0.0f && f.scaleFit >= kScaleTrust) {
            s = std::clamp(static_cast<int>(std::lround(f.scale)), 0, kModeCount - 1);
            r = pitchClass(f.scaleRoot);
            c = clamp01((f.scaleFit - kScaleTrust) / (1.0f - kScaleTrust) * 1.5f);
        } else if (f.keyClarity >= kKeyTrust) {
            s = f.keyMajor >= 0.5f ? kIonian : kAeolian;
            r = pitchClass(f.keyRoot);
            c = 0.7f * clamp01((f.keyClarity - kKeyTrust) / (1.0f - kKeyTrust) * 1.5f);
        }
        scale[t] = s;
        root[t] = r;
        conf[t] = c;
        if (s >= 0) {
            const Emotion::Fingerprint p = fingerprintOf(s, r);
            pv[t] = p.valence, pe[t] = p.energy, pt[t] = p.tension, ps[t] = p.sharpness;
            for (int i = 0; i < kGems; ++i) pg[t][i] = p.gems[i];
            fingerprinted[t] = 1;
        }
        tonicOf[t] = pitchClass(f.keyRoot);
        int best = -1;
        float top = 0.0f, sum = 0.0f;
        for (int i = 0; i < GEODE_SOUND_CHROMA; ++i) {
            sum += f.chromaFast[i];
            if (f.chromaFast[i] > top) top = f.chromaFast[i], best = i;
        }
        lead[t] = best >= 0 && top > 0.0f && top >= 1.6f * sum / GEODE_SOUND_CHROMA && f.loudness > 0.2f ? best : -1;
        loud[t] = clamp01((f.loudness - 0.45f) / 0.45f);
        tempo[t] = f.beatConf > 0.25f && f.bpm > 0.0f ? clamp01((f.bpm - 70.0f) / 110.0f) : 0.35f;
        bright[t] = clamp01((f.centroid - 0.35f) / 0.35f);
        rise[t] = clamp01((f.energyTrend - 0.5f) * 2.0f);
        harmonyTension[t] = f.tension;
        buildUp[t] = f.buildUp;
    }

    // Where no key or scale is trusted, the fingerprint holds the nearest one that was (live holds the last).
    {
        int last = -1;
        for (int t = 0; t < frames; ++t) {
            if (fingerprinted[t]) last = t;
            else if (last >= 0) scale[t] = scale[last], root[t] = root[last], pv[t] = pv[last], pe[t] = pe[last],
                                pt[t] = pt[last], ps[t] = ps[last], pg[t] = pg[last];
        }
        const auto first = std::find(fingerprinted.begin(), fingerprinted.end(), uint8_t{1});
        if (first != fingerprinted.end()) {
            const int f0 = static_cast<int>(first - fingerprinted.begin());
            for (int t = 0; t < f0; ++t) scale[t] = scale[f0], root[t] = root[f0], pv[t] = pv[f0], pe[t] = pe[f0], pt[t] = pt[f0],
                                         ps[t] = ps[f0], pg[t] = pg[f0];
        }
    }
    const std::vector<double> sConf = smooth(conf, 3.0, rate), sv = smooth(pv, 3.0, rate), se = smooth(pe, 3.0, rate),
                              st = smooth(pt, 3.0, rate), ss = smooth(ps, 3.0, rate);
    std::array<std::vector<double>, kGems> sg;
    for (int i = 0; i < kGems; ++i) {
        std::vector<double> g(nf);
        for (int t = 0; t < frames; ++t) g[t] = pg[t][i];
        sg[i] = smooth(g, 3.0, rate);
    }

    // The leading degree against the tonic, held at least 0.12 s.
    std::vector<int> degree(nf, -1);
    for (int t = 0; t < frames; ++t) {
        const int tonic = scale[t] >= 0 ? root[t] : tonicOf[t];
        degree[t] = lead[t] >= 0 && sConf[t] > 0.1 ? (lead[t] - tonic + 12) % 12 : -1;
    }
    {
        const int hold = math::framesFor(0.12, rate);
        int held = -2;  // the degree in force; -2 before the first long enough run
        int t = 0;
        std::vector<int> settled(nf, -1);
        while (t < frames) {
            int e = t;
            while (e < frames && degree[e] == degree[t]) ++e;
            // A long run takes over (the short runs before the first one take its degree); a short run
            // keeps the degree in force.
            if (e - t >= hold) {
                if (held == -2) std::fill(settled.begin(), settled.begin() + t, degree[t]);
                if (degree[t] >= 0 && degree[t] != held) out.accents.push_back(t / rate);
                held = degree[t];
            }
            std::fill(settled.begin() + t, settled.begin() + e, held == -2 ? -1 : held);
            t = e;
        }
        degree = settled;
    }
    std::vector<double> dv(nf, 0.0), dtn(nf, 0.0);
    for (int t = 0; t < frames; ++t) {
        if (degree[t] < 0) continue;
        dv[t] = kQualia[static_cast<size_t>(degree[t])].valence * sConf[t];
        dtn[t] = kQualia[static_cast<size_t>(degree[t])].tension * sConf[t];
    }
    const std::vector<double> sdv = smooth(dv, 0.3, rate), sdt = smooth(dtn, 0.3, rate);

    // Onsets a second over the centred four seconds.
    std::vector<double> busy(nf);
    for (int t = 0; t < frames; ++t) {
        const double now = t / rate;
        const double count = static_cast<double>(math::countUpTo(track.features.onsets, now + 2.0) - math::countUpTo(track.features.onsets, now - 2.0));
        busy[t] = std::clamp(count / 4.0 / 6.0, 0.0, 1.0);
    }

    // The three axes, then their centred means.
    std::vector<double> energy(nf), valence(nf), tension(nf);
    for (int t = 0; t < frames; ++t) {
        const double c = sConf[t];
        const double build = buildUp[t];
        energy[t] = std::clamp(0.2 * se[t] * c + 0.3 * loud[t] + 0.2 * tempo[t] + 0.2 * busy[t] + 0.1 * bright[t] + 0.15 * build, 0.0, 1.0);
        valence[t] = std::clamp(sv[t] * c + 0.1 * (tempo[t] - 0.5) + 0.05 * (bright[t] - 0.5) + 0.3 * sdv[t], -1.0, 1.0);
        tension[t] = std::clamp(0.35 * st[t] * c + 0.3 * harmonyTension[t] + 0.2 * rise[t] + 0.3 * build + 0.2 * sdt[t], 0.0, 1.0);
    }
    const std::vector<double> sEnergy = smooth(energy, 1.5, rate), sValence = smooth(valence, 3.0, rate),
                              sTension = smooth(tension, 1.0, rate);

    // What kind of feeling: the fingerprint as far as it is trusted, plus what the axes add.
    std::array<std::vector<double>, kGems> gems;
    for (auto& g : gems) g.assign(nf, 0.0);
    for (int t = 0; t < frames; ++t) {
        const float c = static_cast<float>(sConf[t]);
        Emotion::GemsVec g{};
        for (int i = 0; i < kGems; ++i) g[i] = static_cast<float>(sg[i][t]) * c + (1.0f - c) / kGems;
        const float v = static_cast<float>(sValence[t]), e = static_cast<float>(sEnergy[t]), te = static_cast<float>(sTension[t]);
        g[Emotion::kPower] += 0.3f * e * te;
        g[Emotion::kJoy] += 0.3f * std::max(v, 0.0f) * e;
        g[Emotion::kPeace] += 0.3f * (1.0f - e) * (1.0f - te);
        g[Emotion::kTension] += 0.3f * te * te;
        g[Emotion::kSadness] += 0.3f * std::max(-v, 0.0f) * (1.0f - e);
        normalise(g);
        for (int i = 0; i < kGems; ++i) gems[i][t] = g[i];
    }
    for (auto& g : gems) g = smooth(g, 2.0, rate);

    out.frames.resize(nf);
    for (int t = 0; t < frames; ++t) {
        Emotion::State& s = out.frames[t];
        s.valence = static_cast<float>(sValence[t]);
        s.energy = static_cast<float>(sEnergy[t]);
        s.tension = static_cast<float>(sTension[t]);
        s.sharpness = static_cast<float>(ss[t] * sConf[t]);
        for (int i = 0; i < kGems; ++i) s.gems[i] = static_cast<float>(gems[i][t]);
        normalise(s.gems);
        s.scale = scale[t];
        s.root = root[t];
        s.confidence = static_cast<float>(sConf[t]);
        s.degree = degree[t];
        s.degreeValence = static_cast<float>(sdv[t]);
        s.degreeTension = static_cast<float>(sdt[t]);
        s.degreeAccent = 0.0f;
    }
    return out;
}

Emotion::State sampleEmotion(const EmotionTrack& emotion, double seconds, double dt) {
    if (emotion.frames.empty()) return Emotion::State{};
    Emotion::State s = emotion.frames[math::frameAt(seconds, emotion.frameRate, static_cast<int>(emotion.frames.size()))];
    s.degreeAccent = math::firstIn(emotion.accents, seconds, dt) >= 0 ? 1.0f : 0.0f;
    return s;
}

}  // namespace geode::sound::offline
