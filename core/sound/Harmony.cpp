#include "sound/Harmony.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "sound/Stages.hpp"

namespace geode::sound {

namespace {

constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
// Peaks between these count. Below 65 Hz (C2) the long window cannot split semitones and the kick's
// thump lands on a pitch class it does not have; above 5 kHz the partials are high harmonics that only
// smear the profile.
constexpr float kLowHz = 65.0f;
constexpr float kHighHz = 5000.0f;
constexpr float kPeakFloor = 0.01f;  // -40 dB under the frame's strongest peak
constexpr float kFastTau = 0.15f;
constexpr float kMidTau = 0.6f;
constexpr float kSlowTau = 10.0f;
constexpr float kKeyEaseTau = 1.5f;  // the tonal centre glides round the wheel, never jumps
constexpr float kModeTau = 2.0f;
constexpr float kClarityTau = 1.0f;
constexpr float kWarmUp = 4.0f;      // seconds of pitched sound before the key is trusted fully
constexpr float kKeySharpness = 10.0f;  // softmax over the 24 correlations
constexpr float kChangeAt = 0.25f;   // cosine distance between the sound now and the chord that fires a change
constexpr float kChangeGap = 0.25f;
constexpr float kMotionTau = 8.0f;

// Position of pitch class pc on the circle of fifths, in twelfths: C 0, G 1, D 2 … F 11.
int fifthsOf(int pc) { return (pc * 7) % 12; }

float pearson(const std::array<float, 12>& a, const std::array<float, 12>& b, int rotate) {
    float ma = 0.0f;
    float mb = 0.0f;
    for (int i = 0; i < 12; i++) {
        ma += a[i];
        mb += b[i];
    }
    ma /= 12.0f;
    mb /= 12.0f;
    float sab = 0.0f;
    float saa = 0.0f;
    float sbb = 0.0f;
    for (int pc = 0; pc < 12; pc++) {
        const float x = a[pc] - ma;
        const float y = b[(pc - rotate + 12) % 12] - mb;
        sab += x * y;
        saa += x * x;
        sbb += y * y;
    }
    const float d = std::sqrt(saa * sbb);
    return d > 1e-12f ? sab / d : 0.0f;
}

std::array<float, 12> unitMax(const std::array<float, 12>& v) {
    const float m = *std::max_element(v.begin(), v.end());
    std::array<float, 12> out{};
    if (m <= 0.0f) return out;
    for (int i = 0; i < 12; i++) out[i] = v[i] / m;
    return out;
}

// An angle in turns, 0..1, from a vector.
float turns(float x, float y) {
    const float a = std::atan2(y, x) / kTwoPi;
    return a < 0.0f ? a + 1.0f : a;
}

}  // namespace

const std::array<float, 12>& Harmony::majorProfile() {
    static const std::array<float, 12> k = {6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f};
    return k;
}

const std::array<float, 12>& Harmony::minorProfile() {
    static const std::array<float, 12> k = {6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f};
    return k;
}

void Harmony::step(const float* mag, int count, float binHz, float flatness, bool silent, float dt) {
    out_.change = 0.0f;
    sinceChange_ += dt;
    changeRate_ *= std::exp(-dt / kMotionTau);
    out_.motion = clamp01(changeRate_ / (2.0f * kMotionTau));
    if (silent || mag == nullptr || count < 4 || binHz <= 0.0f) {
        out_.tonal = follow(out_.tonal, 0.0f, 0.3f, dt);
        return;
    }

    // Spectral peaks folded into pitch classes. Each peak's frequency is refined by a parabola through
    // its neighbours, then shared between the two nearest classes by distance, so a slightly sharp
    // recording leans its notes rather than splitting them in half. sqrt compresses loud partials, and
    // a gentle roll-off above 2 kHz keeps the upper harmonics of one note from outvoting the chord.
    const int lo = std::max(1, static_cast<int>(std::ceil(kLowHz / binHz)));
    const int hi = std::min(count - 2, static_cast<int>(kHighHz / binHz));
    float strongest = 0.0f;
    for (int k = lo; k <= hi; k++) strongest = std::max(strongest, mag[k]);
    raw_.fill(0.0f);
    float total = 0.0f;
    if (strongest > 0.0f) {
        const float floor = strongest * kPeakFloor;
        for (int k = lo; k <= hi; k++) {
            const float b = mag[k];
            if (b < floor || b < mag[k - 1] || b <= mag[k + 1]) continue;
            const float a = mag[k - 1];
            const float c = mag[k + 1];
            const float curve = a - 2.0f * b + c;
            const float delta = curve < 0.0f ? std::clamp(0.5f * (a - c) / curve, -0.5f, 0.5f) : 0.0f;
            const float hz = (static_cast<float>(k) + delta) * binHz;
            const float midi = 69.0f + 12.0f * std::log2(hz / 440.0f);
            float pc = std::fmod(midi, 12.0f);
            if (pc < 0.0f) pc += 12.0f;
            const int below = static_cast<int>(pc) % 12;
            const float frac = pc - std::floor(pc);
            const float w = std::sqrt(b) / (1.0f + hz / 2000.0f);
            raw_[below] += w * (1.0f - frac);
            raw_[(below + 1) % 12] += w * frac;
            total += w;
        }
    }
    // How pitched the sound is: a flat (noisy) spectrum, a drum break or a wash of cymbals holds the
    // last harmony instead of painting a key from noise. Everything below advances by dt × tonal.
    const float tonalNow = total > 0.0f ? clamp01(1.0f - flatness) : 0.0f;
    out_.tonal = follow(out_.tonal, tonalNow, 0.3f, dt);
    const float dtTonal = dt * tonalNow;
    if (dtTonal <= 0.0f) return;
    heardSeconds_ += dtTonal;
    for (int i = 0; i < 12; i++) {
        const float p = raw_[i] / total;
        fast_[i] = follow(fast_[i], p, kFastTau, dtTonal);
        mid_[i] = follow(mid_[i], p, kMidTau, dtTonal);
        slow_[i] = follow(slow_[i], p, kSlowTau, dtTonal);
    }
    out_.chroma = unitMax(fast_);
    out_.home = unitMax(slow_);
    scoreKeys(dt);
    chord(dt);
}

void Harmony::scoreKeys(float dt) {
    // All 24 keys scored by correlation with the song's profile (Krumhansl-Schmuckler), then blended by a
    // softmax: a clear key dominates, an ambiguous passage averages its neighbours on the wheel.
    std::array<float, 24> score{};
    float best = -1.0f;
    for (int t = 0; t < 12; t++) {
        score[t] = pearson(slow_, majorProfile(), t);
        score[12 + t] = pearson(slow_, minorProfile(), t);
    }
    for (float s : score) best = std::max(best, s);
    float fx = 0.0f;
    float fy = 0.0f;
    float tx = 0.0f;
    float ty = 0.0f;
    float major = 0.0f;
    float sum = 0.0f;
    homeProfile_.fill(0.0f);
    for (int k = 0; k < 24; k++) {
        const float w = std::exp(kKeySharpness * (score[k] - best));
        const bool isMajor = k < 12;
        const int tonic = k % 12;
        // A minor key shares its signature with the major a minor third above: A minor sits with C.
        const int signature = isMajor ? tonic : (tonic + 3) % 12;
        const float fa = kTwoPi * static_cast<float>(fifthsOf(signature)) / 12.0f;
        const float ta = kTwoPi * static_cast<float>(tonic) / 12.0f;
        fx += w * std::cos(fa);
        fy += w * std::sin(fa);
        tx += w * std::cos(ta);
        ty += w * std::sin(ta);
        if (isMajor) major += w;
        sum += w;
        const auto& profile = isMajor ? majorProfile() : minorProfile();
        for (int pc = 0; pc < 12; pc++) homeProfile_[pc] += w * profile[(pc - tonic + 12) % 12];
    }
    const float spread = std::hypot(fx, fy) / sum;  // 1 = every likely key shares one signature
    const float warm = clamp01(heardSeconds_ / kWarmUp);
    const float clarity = clamp01((best - 0.3f) / 0.5f) * spread * warm;
    out_.clarity = follow(out_.clarity, clarity, kClarityTau, dt);
    out_.mode = follow(out_.mode, major / sum, kModeTau, dt);
    // Ease the centre as a vector, so it takes the short way round and never wraps through the middle.
    const float fl = std::hypot(fx, fy);
    if (fl > 1e-6f) {
        fifthsX_ = follow(fifthsX_, fx / fl, kKeyEaseTau, dt);
        fifthsY_ = follow(fifthsY_, fy / fl, kKeyEaseTau, dt);
        out_.fifths = turns(fifthsX_, fifthsY_);
    }
    const float tl = std::hypot(tx, ty);
    if (tl > 1e-6f) {
        tonicX_ = follow(tonicX_, tx / tl, kKeyEaseTau, dt);
        tonicY_ = follow(tonicY_, ty / tl, kKeyEaseTau, dt);
        out_.tonic = turns(tonicX_, tonicY_);
    }
}

void Harmony::chord(float dt) {
    // The chord on the circle of fifths: a triad's three notes sit within four fifths of each other, so
    // their vector is long and points at its centre; a chromatic cluster cancels out.
    float cx = 0.0f;
    float cy = 0.0f;
    float sum = 0.0f;
    for (int pc = 0; pc < 12; pc++) {
        const float a = kTwoPi * static_cast<float>(fifthsOf(pc)) / 12.0f;
        cx += mid_[pc] * std::cos(a);
        cy += mid_[pc] * std::sin(a);
        sum += mid_[pc];
    }
    if (sum > 1e-9f) {
        out_.chordFifths = turns(cx, cy);
        out_.chordStrength = clamp01(1.3f * std::hypot(cx, cy) / sum);
    }
    // Roughness: semitone neighbours clash hardest, the tritone next (Plomp-Levelt, folded into one
    // octave). Measured on the sound now, over its own energy, so a single note or a triad reads 0 and
    // two notes a semitone apart read 1.
    float self = 0.0f;
    float clash = 0.0f;
    for (int i = 0; i < 12; i++) {
        self += fast_[i] * fast_[i];
        clash += 2.0f * fast_[i] * fast_[(i + 1) % 12];
        if (i < 6) clash += fast_[i] * fast_[i + 6];
    }
    const float rough = self > 1e-12f ? clamp01(clash / self) : 0.0f;
    out_.roughness = follow(out_.roughness, rough, 0.2f, dt);
    // Tension: the chord against the home key's blended profile. Diatonic chords correlate ~0.6-0.8 with
    // their key; borrowed and chromatic harmony falls toward 0 and below.
    const float fit = pearson(mid_, homeProfile_, 0);
    const float away = clamp01((0.75f - fit) / 0.85f);
    const float target = clamp01(0.65f * away + 0.35f * rough) * (0.5f + 0.5f * out_.tonal);
    out_.tension = lag(out_.tension, target, 0.25f, 0.8f, dt);
    // A chord change: the sound now leaves the chord it was in. Fires on the way up through the
    // threshold, at most every kChangeGap seconds.
    float dot = 0.0f;
    float na = 0.0f;
    float nb = 0.0f;
    for (int i = 0; i < 12; i++) {
        dot += fast_[i] * mid_[i];
        na += fast_[i] * fast_[i];
        nb += mid_[i] * mid_[i];
    }
    const float distance = na > 0.0f && nb > 0.0f ? 1.0f - dot / std::sqrt(na * nb) : 0.0f;
    if (distance > kChangeAt && lastDistance_ <= kChangeAt && sinceChange_ > kChangeGap && out_.tonal > 0.3f) {
        out_.change = clamp01(0.3f + (distance - kChangeAt) / 0.4f);
        sinceChange_ = 0.0f;
        changeRate_ += 1.0f;
        out_.motion = clamp01(changeRate_ / (2.0f * kMotionTau));
    }
    lastDistance_ = distance;
}

}  // namespace geode::sound
