#include "sound/Emotion.hpp"

#include <algorithm>
#include <cmath>

namespace geode::sound {

namespace {

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
static_assert(sizeof(kKeys) / sizeof(kKeys[0]) == 24, "12 major and 12 minor keys");

// In scales.csv order: the fallbacks when only the key (no scale) is known.
constexpr int kIonian = 0;
constexpr int kAeolian = 5;

// The scale's own fit must clear this before its fingerprint is trusted at all; the key's clarity, this.
constexpr float kScaleTrust = 0.2f;
constexpr float kKeyTrust = 0.2f;
// A new key or scale must hold this long (on top of the detectors' own hysteresis) before the feeling moves.
constexpr float kHoldSeconds = 1.0f;
// The fingerprint glides over about this long, so a modulation is a change of light, not a cut.
constexpr float kPrintTau = 3.0f;
constexpr float kValenceTau = 3.0f;
constexpr float kEnergyTau = 1.5f;
constexpr float kTensionTau = 1.0f;
constexpr float kGemsTau = 2.0f;

// Huron's scale-degree qualia (Sweet Anticipation, ch. 9) as a pull on valence and tension, against the
// tonic: stable degrees (1, 3, 5, 6) lift, the lowered ones (b2, b3, b6) sink, the leaning ones (b2, #4, 7)
// strain. Maimon, Lamy & Eitan 2021: more stable degrees read more positive. Moderate evidence.
struct Quale {
    float valence;
    float tension;
};
constexpr std::array<Quale, 12> kQualia = {{
    {0.30f, 0.00f},   // 1  home, rest
    {-0.30f, 0.40f},  // b2 dark, falling
    {0.10f, 0.10f},   // 2  open, moving
    {-0.30f, 0.10f},  // b3 sad, inward
    {0.30f, 0.00f},   // 3  bright, warm
    {0.00f, 0.20f},   // 4  poised to fall
    {0.05f, 0.40f},   // #4 restless, lifting
    {0.20f, 0.00f},   // 5  strong, stable
    {-0.35f, 0.20f},  // b6 sorrow, sighing down
    {0.20f, 0.00f},   // 6  sweet, tender
    {0.00f, 0.10f},   // b7 relaxed, bluesy
    {0.00f, 0.45f},   // 7  longing, pointing home
}};

float follow(float current, float target, float dt, float tau) { return current + (target - current) * (1.0f - std::exp(-dt / tau)); }

float clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }

int pitchClass(float fraction) { return (static_cast<int>(std::lround(fraction * 12.0f)) % 12 + 12) % 12; }

void normalise(Emotion::GemsVec& g) {
    float sum = 0.0f;
    for (float& v : g) {
        v = std::max(v, 0.0f);
        sum += v;
    }
    for (float& v : g) v = sum > 0.0f ? v / sum : 1.0f / Emotion::kGems;
}

}  // namespace

int Emotion::modeCount() { return static_cast<int>(sizeof(kModes) / sizeof(kModes[0])); }

const Emotion::Mode& Emotion::mode(int scale) { return kModes[std::clamp(scale, 0, modeCount() - 1)]; }

const Emotion::Key& Emotion::key(int root, bool major) { return kKeys[((root % 12 + 12) % 12) + (major ? 0 : 12)]; }

Emotion::Fingerprint Emotion::fingerprint(int scale, int root) {
    const Mode& m = mode(scale);
    const Key& k = key(root, m.majorThird);
    Fingerprint out;
    out.valence = m.valence;  // the key never moves valence: its character is tradition, not measurement
    out.energy = m.energy;
    out.tension = m.tension;
    out.sharpness = k.sharpness;
    for (int i = 0; i < kGems; ++i) out.gems[i] = kModeWeight * m.gems[i] + (1.0f - kModeWeight) * k.gems[i];
    normalise(out.gems);
    return out;
}

void Emotion::stepFingerprint(const GeodeSoundFrame& f, float dt) {
    int scale = -1;
    int root = 0;
    float confidence = 0.0f;
    if (f.scale >= 0.0f && f.scaleFit >= kScaleTrust) {
        scale = std::clamp(static_cast<int>(std::lround(f.scale)), 0, modeCount() - 1);
        root = pitchClass(f.scaleRoot);
        confidence = clamp01((f.scaleFit - kScaleTrust) / (1.0f - kScaleTrust) * 1.5f);
    } else if (f.keyClarity >= kKeyTrust) {
        scale = f.keyMajor >= 0.5f ? kIonian : kAeolian;
        root = pitchClass(f.keyRoot);
        confidence = 0.7f * clamp01((f.keyClarity - kKeyTrust) / (1.0f - kKeyTrust) * 1.5f);
    }
    if (scale >= 0 && (scale != state_.scale || root != state_.root)) {
        if (scale == candidateScale_ && root == candidateRoot_) {
            candidateSeconds_ += dt;
        } else {
            candidateScale_ = scale;
            candidateRoot_ = root;
            candidateSeconds_ = 0.0f;
        }
        if (!primed_ || candidateSeconds_ >= kHoldSeconds) {
            state_.scale = scale;
            state_.root = root;
            if (!primed_) print_ = fingerprint(scale, root);
            primed_ = true;
        }
    }
    state_.confidence = follow(state_.confidence, scale >= 0 ? confidence : 0.0f, dt, kPrintTau);
    if (state_.scale < 0) return;
    const Fingerprint target = fingerprint(state_.scale, state_.root);
    print_.valence = follow(print_.valence, target.valence, dt, kPrintTau);
    print_.energy = follow(print_.energy, target.energy, dt, kPrintTau);
    print_.tension = follow(print_.tension, target.tension, dt, kPrintTau);
    print_.sharpness = follow(print_.sharpness, target.sharpness, dt, kPrintTau);
    for (int i = 0; i < kGems; ++i) print_.gems[i] = follow(print_.gems[i], target.gems[i], dt, kPrintTau);
    normalise(print_.gems);
}

void Emotion::stepDegree(const GeodeSoundFrame& f, float dt) {
    state_.degreeAccent = 0.0f;
    const int tonic = state_.scale >= 0 ? state_.root : pitchClass(f.keyRoot);
    int best = -1;
    float top = 0.0f;
    float sum = 0.0f;
    for (int i = 0; i < GEODE_SOUND_CHROMA; ++i) {
        sum += f.chromaFast[i];
        if (f.chromaFast[i] > top) {
            top = f.chromaFast[i];
            best = i;
        }
    }
    const float mean = sum / GEODE_SOUND_CHROMA;
    // One note must stand out (a melody or a held bass), and there must be music to hear it in.
    const bool leads = best >= 0 && top > 0.0f && top >= 1.6f * mean && f.loudness > 0.2f && state_.confidence > 0.1f;
    const int degree = leads ? (best - tonic + 12) % 12 : -1;
    if (degree != degreeCandidate_) {
        degreeCandidate_ = degree;
        degreeSeconds_ = 0.0f;
    } else {
        degreeSeconds_ += dt;
    }
    // A passing note is not a degree: it must last an eighth of a second.
    if (degreeCandidate_ != state_.degree && degreeSeconds_ >= 0.12f) {
        state_.degree = degreeCandidate_;
        if (state_.degree >= 0) state_.degreeAccent = 1.0f;
    }
    const Quale q = state_.degree >= 0 ? kQualia[static_cast<size_t>(state_.degree)] : Quale{0.0f, 0.0f};
    const float tau = state_.degree >= 0 ? 0.3f : 1.0f;
    state_.degreeValence = follow(state_.degreeValence, q.valence * state_.confidence, dt, tau);
    state_.degreeTension = follow(state_.degreeTension, q.tension * state_.confidence, dt, tau);
}

void Emotion::step(const GeodeSoundFrame& f, float dt) {
    if (!(dt > 0.0f)) return;
    stepFingerprint(f, dt);
    stepDegree(f, dt);

    // Onsets a second over about four seconds: the articulation and busyness cue.
    const bool onset = f.onset > 0.5f && lastOnset_ <= 0.5f;
    lastOnset_ = f.onset;
    onsetRate_ = onsetRate_ * std::exp(-dt / 4.0f) + (onset ? 0.25f : 0.0f);

    const float conf = state_.confidence;
    const float loud = clamp01((f.loudness - 0.45f) / 0.45f);  // about -33 dBFS .. -6 dBFS
    const float tempo = f.beatConf > 0.25f && f.bpm > 0.0f ? clamp01((f.bpm - 70.0f) / 110.0f) : 0.35f;
    const float busy = clamp01(onsetRate_ / 6.0f);
    const float bright = clamp01((f.centroid - 0.35f) / 0.35f);
    const float rise = clamp01((f.energyTrend - 0.5f) * 2.0f);

    // Energy (arousal): loudness, tempo, onset rate and brightness carry it (Eerola 2013, Juslin 2010).
    const float energy = clamp01(0.2f * print_.energy * conf + 0.3f * loud + 0.2f * tempo + 0.2f * busy + 0.1f * bright +
                                 0.15f * f.buildUp);
    // Valence: the mode carries about half (sr² .48 happy, .54 sad); tempo and register add a little.
    valence_ = std::clamp(print_.valence * conf + 0.1f * (tempo - 0.5f) + 0.05f * (bright - 0.5f) + 0.3f * state_.degreeValence,
                          -1.0f, 1.0f);
    // Tension: the scale's own, the chord's roughness, a rise in loudness (Farbood 2012: the trend, not the
    // level) and the build-up.
    const float tension = clamp01(0.35f * print_.tension * conf + 0.3f * f.tension + 0.2f * rise + 0.3f * f.buildUp +
                                  0.2f * state_.degreeTension);
    state_.valence = follow(state_.valence, valence_, dt, kValenceTau);
    state_.energy = follow(state_.energy, energy, dt, kEnergyTau);
    state_.tension = follow(state_.tension, tension, dt, kTensionTau);
    state_.sharpness = print_.sharpness * conf;

    // What kind of feeling: the fingerprint, trusted as far as the key is, and what the live axes add.
    GemsVec g{};
    for (int i = 0; i < kGems; ++i) g[i] = print_.gems[i] * conf + (1.0f - conf) / kGems;
    const float v = state_.valence;
    const float e = state_.energy;
    const float t = state_.tension;
    g[kPower] += 0.3f * e * t;
    g[kJoy] += 0.3f * std::max(v, 0.0f) * e;
    g[kPeace] += 0.3f * (1.0f - e) * (1.0f - t);
    g[kTension] += 0.3f * t * t;
    g[kSadness] += 0.3f * std::max(-v, 0.0f) * (1.0f - e);
    normalise(g);
    for (int i = 0; i < kGems; ++i) state_.gems[i] = follow(state_.gems[i], g[i], dt, kGemsTau);
    normalise(state_.gems);
}

}  // namespace geode::sound
