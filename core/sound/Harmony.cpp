#include "sound/Harmony.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace geode::sound {

namespace {

// Krumhansl & Kessler's probe-tone ratings (1982), tonic first.
constexpr std::array<float, 12> kMajorProfile = {6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f};
constexpr std::array<float, 12> kMinorProfile = {6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f};

// How rough each interval class sounds when its two notes ring together: minor second / major seventh
// worst, then the tritone, the major second / minor seventh; thirds, fourths and fifths nearly smooth.
// The ranking of Plomp & Levelt's curves and Huron's interval-class consonance, scaled 0..1.
constexpr std::array<float, 7> kIntervalRoughness = {0.0f, 1.0f, 0.55f, 0.15f, 0.1f, 0.05f, 0.65f};

constexpr float kLowHz = 250.0f;
constexpr float kHighHz = 2000.0f;

// Pearson correlation of `c` with `profile` rotated to `tonic`.
float correlate(const std::array<float, 12>& c, const std::array<float, 12>& profile, int tonic) {
    float mc = 0.0f;
    float mp = 0.0f;
    for (int i = 0; i < 12; i++) {
        mc += c[i];
        mp += profile[i];
    }
    mc /= 12.0f;
    mp /= 12.0f;
    float num = 0.0f;
    float dc = 0.0f;
    float dp = 0.0f;
    for (int i = 0; i < 12; i++) {
        const float a = c[(i + tonic) % 12] - mc;
        const float b = profile[i] - mp;
        num += a * b;
        dc += a * a;
        dp += b * b;
    }
    return dc > 1e-12f ? num / std::sqrt(dc * dp) : 0.0f;
}

void normalise(const std::array<float, 12>& in, float* out) {
    float top = 0.0f;
    for (float v : in) top = std::max(top, v);
    for (int i = 0; i < 12; i++) out[i] = top > 1e-9f ? in[i] / top : 0.0f;
}

// 1 - the cosine of two chroma vectors after each loses its mean (so a shared floor does not read as
// agreement), halved: 0 the same chord, ~0.5 unrelated, 1 opposite.
float distance(const std::array<float, 12>& a, const std::array<float, 12>& b) {
    float ma = 0.0f;
    float mb = 0.0f;
    for (int i = 0; i < 12; i++) {
        ma += a[i];
        mb += b[i];
    }
    ma /= 12.0f;
    mb /= 12.0f;
    float num = 0.0f;
    float da = 0.0f;
    float db = 0.0f;
    for (int i = 0; i < 12; i++) {
        num += (a[i] - ma) * (b[i] - mb);
        da += (a[i] - ma) * (a[i] - ma);
        db += (b[i] - mb) * (b[i] - mb);
    }
    if (da < 1e-12f || db < 1e-12f) return 0.0f;
    return 0.5f * (1.0f - num / std::sqrt(da * db));
}

float hueOf(int pitchClass) { return static_cast<float>((pitchClass * 7) % 12) / 12.0f; }

constexpr ScaleInfo kScales[] = {
#include "sound/ScaleTable.inc"
};
constexpr int kScaleCount = static_cast<int>(sizeof(kScales) / sizeof(kScales[0]));

}  // namespace

int scaleCount() { return kScaleCount; }
const ScaleInfo& scaleInfo(int index) { return kScales[std::clamp(index, 0, kScaleCount - 1)]; }

void Harmony::detectScale() {
    // Every scale on every root against the slow chroma: how well it correlates with the scale, plus a pull toward a strong root and fifth, which is what tells the seven modes of
    // one note set apart (C Ionian and A Aeolian share every note; the music's home note does not).
    // Chromatic has no outside: its in-out term is zero, so it wins only when no scale fits at all.
    // Capped at twice the mean first, so a held drone or one loud bass note cannot decide the scale on its
    // own: what matters is which classes sound at all, not how loud the loudest is.
    std::array<float, 12> c{};
    normalise(slow_, c.data());
    float mean = 0.0f;
    for (float v : c) mean += v / 12.0f;
    for (float& v : c) v = std::min(1.0f, v / std::max(2.0f * mean, 1e-6f));
    mean = 0.0f;
    for (float v : c) mean += v / 12.0f;
    int best = -1;
    float bestScore = -1e9f;
    float bestFit = 0.0f;
    float current = -1e9f;
    for (int s = 0; s < kScaleCount; s++) {
        const unsigned mask = kScales[s].mask;
        for (int root = 0; root < 12; root++) {
            // Pearson correlation of the chroma with the scale's mask: notes inside high and notes outside
            // low, whatever the overall spread. A difference of means needed the outside doubled to stop
            // pentatonics swallowing modes, and then a real piano's sustain and spill sank every scale
            // below chromatic. Correlation is size-fair and tolerates spill. Chromatic has no variance:
            // it scores 0, so it wins only when every scale fits worse than chance.
            int n = 0;
            for (int k = 0; k < 12; k++) n += (mask >> k) & 1u;
            float fit = 0.0f;
            if (n < 12) {
                const float mm = static_cast<float>(n) / 12.0f;
                float num = 0.0f;
                float dc = 0.0f;
                for (int k = 0; k < 12; k++) {
                    const float v = c[(root + k) % 12] - mean;
                    num += v * ((((mask >> k) & 1u) ? 1.0f : 0.0f) - mm);
                    dc += v * v;
                }
                fit = dc > 1e-9f ? num / std::sqrt(dc * mm * (1.0f - mm) * 12.0f) : 0.0f;
            }
            const float home = 0.5f * (c[root] - mean) + ((mask & (1u << 7)) ? 0.25f * (c[(root + 7) % 12] - mean) : 0.0f);
            const float score = fit + home;
            if (score > bestScore) {
                bestScore = score;
                best = s * 12 + root;
                bestFit = fit;
            }
            if (s == scale_ && root == scaleRoot_) current = score;
        }
    }
    const float dt = 0.25f;
    if (scale_ < 0) {
        scale_ = best / 12;
        scaleRoot_ = best % 12;
    } else if (best != scale_ * 12 + scaleRoot_ && bestScore - current > 0.03f) {
        scaleHold_ = best == scaleRival_ ? scaleHold_ + dt : dt;
        scaleRival_ = best;
        if (scaleHold_ >= 2.0f) {  // two seconds of a better fit before the mood changes
            scale_ = best / 12;
            scaleRoot_ = best % 12;
            scaleHold_ = 0.0f;
            scaleRival_ = -1;
        }
    } else {
        scaleHold_ = 0.0f;
        scaleRival_ = -1;
    }
    scaleFit_ = follow(scaleFit_, std::clamp((bestFit - 0.3f) / 0.5f, 0.0f, 1.0f), 1.0f, dt);
}

void Harmony::configure(float binHz, int bins) {
    binHz_ = binHz;
    from_ = std::max(2, static_cast<int>(std::ceil(kLowHz / binHz)));
    to_ = std::min(bins - 2, static_cast<int>(kHighHz / binHz) + 1);
    reset();
}

void Harmony::reset() {
    const float binHz = binHz_;
    const int from = from_;
    const int to = to_;
    *this = Harmony();
    binHz_ = binHz;
    from_ = from;
    to_ = to;
}

void Harmony::step(const float* mag, float dt, bool silent, GeodeSoundFrame& out) {
    if (!silent && to_ > from_) {
        float top = 0.0f;
        for (int k = from_; k < to_; k++) top = std::max(top, mag[k]);
        if (top > 1e-9f) {
            // Only spectral peaks count, each on its nearest pitch class (the peak's position refined
            // between bins): summing every bin, a window's leakage and the noise floor lifted all twelve
            // classes together and no scale stood out. Log-compressed over the frame's own peak (Müller's
            // CLP chroma), so loudness divides out and a quiet third still counts beside a loud root.
            std::array<float, 12> raw{};
            for (int k = from_; k < to_; k++) {
                const float m = mag[k];
                if (m < 0.03f * top || m <= mag[k - 1] || m < mag[k + 1]) continue;
                const float a = mag[k - 1];
                const float c = mag[k + 1];
                const float curve = a - 2.0f * m + c;
                const float delta = curve < 0.0f ? std::clamp(0.5f * (a - c) / curve, -0.5f, 0.5f) : 0.0f;
                const float midi = 69.0f + 12.0f * std::log2((static_cast<float>(k) + delta) * binHz_ / 440.0f);
                const int pc = (static_cast<int>(std::lround(midi)) % 12 + 12) % 12;
                // An overtone is not a note. A peak with a strong partner at a third, fifth, sixth or
                // seventh of its frequency is most likely that note's 3rd, 5th, 6th or 7th harmonic, and
                // would add a fifth, a major third or a minor seventh nobody played (a piano's D lights up
                // F# and A). Octave harmonics (2nd, 4th) land on the same class and need no discount.
                // A peak that is also the octave of a sounding note in its own class stays a note: in a
                // chord, one note's octave often sits exactly on another note's 3rd harmonic.
                float weight = 1.0f;
                const int octave = static_cast<int>(std::lround((static_cast<float>(k) + delta) * 0.5f));
                const bool ownOctave = octave >= 2 && std::max({mag[octave - 1], mag[octave], mag[octave + 1]}) >= 0.5f * m;
                for (int h : {3, 5, 6, 7}) {
                    if (ownOctave) break;
                    const int base = static_cast<int>(std::lround((static_cast<float>(k) + delta) / static_cast<float>(h)));
                    if (base < 2) continue;
                    const float below = std::max({mag[base - 1], mag[base], mag[base + 1]});
                    if (below >= 0.5f * m) {
                        weight = 0.25f;
                        break;
                    }
                }
                raw[pc] += weight * std::log1p(20.0f * m / top);
            }
            float rawTop = 0.0f;
            for (float v : raw) rawTop = std::max(rawTop, v);
            if (rawTop <= 0.0f) rawTop = 1.0f;
            for (float& v : raw) v /= rawTop;
            if (!heard_) {
                fast_ = medium_ = slow_ = raw;
                heard_ = true;
            }
            for (int i = 0; i < 12; i++) {
                fast_[i] = follow(fast_[i], raw[i], 0.1f, dt);
                medium_[i] = follow(medium_[i], raw[i], 0.5f, dt);
                slow_[i] = follow(slow_[i], raw[i], 4.0f, dt);
            }
        }
    }
    normalise(fast_, out.chromaFast);
    normalise(medium_, out.chroma);
    normalise(slow_, out.chromaSlow);
    if (!heard_) {
        out.keyMajor = 0.5f;
        out.scale = -1.0f;
        out.scaleBrightness = 0.5f;
        return;
    }

    if (!silent) {
        // Key: the best of the 24 rotated profiles on the slow chroma. Another key must fit better by a
        // margin for two seconds before it takes over, so a borrowed chord does not modulate.
        std::array<float, 24> r{};
        int best = 0;
        float bestMajor = -1.0f;
        float bestMinor = -1.0f;
        for (int t = 0; t < 12; t++) {
            r[t] = correlate(slow_, kMajorProfile, t);
            r[12 + t] = correlate(slow_, kMinorProfile, t);
            bestMajor = std::max(bestMajor, r[t]);
            bestMinor = std::max(bestMinor, r[12 + t]);
        }
        for (int i = 1; i < 24; i++) {
            if (r[i] > r[best]) best = i;
        }
        if (key_ < 0) {
            key_ = best;
        } else if (best != key_ && r[best] - r[key_] > 0.03f) {
            keyHold_ = best == keyRival_ ? keyHold_ + dt : dt;
            keyRival_ = best;
            if (keyHold_ >= 2.0f) {
                key_ = best;
                keyHold_ = 0.0f;
                keyRival_ = -1;
            }
        } else {
            keyHold_ = 0.0f;
            keyRival_ = -1;
        }
        clarity_ = follow(clarity_, std::clamp(r[key_], 0.0f, 1.0f), 1.0f, dt);
        major_ = follow(major_, std::clamp(0.5f + 2.0f * (bestMajor - bestMinor), 0.0f, 1.0f), 2.0f, dt);

        // The chord's colour: the medium chroma's centre of mass on the circle of fifths, each class
        // above the mean pulling toward its own fifths position (Harte's tonal centroid, one circle).
        float mean = 0.0f;
        for (float v : medium_) mean += v;
        mean /= 12.0f;
        float x = 0.0f;
        float y = 0.0f;
        std::array<float, 12> above{};
        for (int i = 0; i < 12; i++) {
            above[i] = std::max(0.0f, medium_[i] - mean);
            const float a = 2.0f * std::numbers::pi_v<float> * hueOf(i);
            x += above[i] * std::cos(a);
            y += above[i] * std::sin(a);
        }
        if (x * x + y * y > 1e-6f) {
            const float hue = std::atan2(y, x) / (2.0f * std::numbers::pi_v<float>);
            chordHue_ = hue - std::floor(hue);
        }
        // Tension: how rough the classes sounding together are, each pair weighted by both its levels.
        float rough = 0.0f;
        float weight = 0.0f;
        for (int i = 0; i < 12; i++) {
            for (int j = i + 1; j < 12; j++) {
                const int d = j - i;
                const float w = above[i] * above[j];
                rough += w * kIntervalRoughness[std::min(d, 12 - d)];
                weight += w;
            }
        }
        const float t = weight > 1e-9f ? rough / weight : 0.0f;
        tension_ = follow(tension_, std::clamp((t - 0.1f) / 0.4f, 0.0f, 1.0f), 0.3f, dt);

        // Harmonic change: every 0.1 s a snapshot of the fast chroma; the last five against the twenty
        // before them. Half a second of evidence keeps it near the change instead of two seconds after.
        sinceSnapshot_ += dt;
        if (sinceSnapshot_ >= kSnapshotEvery) {
            sinceSnapshot_ -= kSnapshotEvery * std::floor(sinceSnapshot_ / kSnapshotEvery);
            snapshots_[snapshotHead_] = fast_;
            snapshotHead_ = (snapshotHead_ + 1) % kSnapshots;
            snapshotCount_ = std::min(snapshotCount_ + 1, kSnapshots);
            if (snapshotCount_ == kSnapshots) {
                std::array<float, 12> recent{};
                std::array<float, 12> before{};
                for (int i = 0; i < kSnapshots; i++) {
                    const auto& s = snapshots_[(snapshotHead_ - 1 - i + kSnapshots) % kSnapshots];
                    auto& into = i < 5 ? recent : before;
                    for (int c = 0; c < 12; c++) into[c] += s[c];
                }
                change_ = distance(recent, before);
            }
        }
        out.harmonicChange = trigger_.step(change_, 0.15f, 0.5f, dt);

        sinceScale_ += dt;
        if (sinceScale_ >= 0.25f) {
            sinceScale_ -= 0.25f * std::floor(sinceScale_ / 0.25f);
            detectScale();
        }
        if (scale_ >= 0) {
            // The mood glides (about a second), so a change of mode is a turn of the light, not a cut.
            scaleBright_ = follow(scaleBright_, 0.5f + 0.5f * kScales[scale_].brightness, 1.0f, dt);
            scaleTension_ = follow(scaleTension_, kScales[scale_].tension, 1.0f, dt);
        }
    }

    const bool minor = key_ >= 12;
    const int tonic = key_ % 12;
    out.keyRoot = static_cast<float>(tonic) / 12.0f;
    out.keyHue = hueOf(minor ? (tonic + 3) % 12 : tonic);
    out.keyMajor = major_;
    out.keyClarity = clarity_;
    out.chordHue = chordHue_;
    out.tension = tension_;
    out.scale = static_cast<float>(scale_);
    out.scaleRoot = static_cast<float>(scaleRoot_) / 12.0f;
    out.scaleBrightness = scaleBright_;
    out.scaleTension = scaleTension_;
    out.scaleFit = scaleFit_;
}

}  // namespace geode::sound
