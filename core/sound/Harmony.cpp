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

}  // namespace

void Harmony::configure(float binHz, int bins) {
    map_.clear();
    for (int k = 1; k < bins; k++) {
        const float hz = static_cast<float>(k) * binHz;
        if (hz < kLowHz || hz > kHighHz) continue;
        const float midi = 69.0f + 12.0f * std::log2(hz / 440.0f);
        const float lo = std::floor(midi);
        const int pc = (static_cast<int>(lo) % 12 + 12) % 12;
        map_.push_back(BinClass{k, pc, 1.0f - (midi - lo)});
    }
    reset();
}

void Harmony::reset() {
    std::vector<BinClass> map = std::move(map_);
    *this = Harmony();
    map_ = std::move(map);
}

void Harmony::step(const float* mag, float dt, bool silent, GeodeSoundFrame& out) {
    if (!silent && !map_.empty()) {
        float top = 0.0f;
        for (const BinClass& b : map_) top = std::max(top, mag[b.bin]);
        if (top > 1e-9f) {
            // Log-compressed over the frame's own peak (Müller's CLP chroma): loudness divides out and a
            // quiet third still counts beside a loud root.
            std::array<float, 12> raw{};
            for (const BinClass& b : map_) {
                const float v = std::log1p(100.0f * mag[b.bin] / top);
                raw[b.pc] += b.weight * v;
                raw[(b.pc + 1) % 12] += (1.0f - b.weight) * v;
            }
            float rawTop = 0.0f;
            for (float v : raw) rawTop = std::max(rawTop, v);
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
        out.harmonicChange = trigger_.step(change_, 0.25f, 0.5f, dt);
    }

    const bool minor = key_ >= 12;
    const int tonic = key_ % 12;
    out.keyRoot = static_cast<float>(tonic) / 12.0f;
    out.keyHue = hueOf(minor ? (tonic + 3) % 12 : tonic);
    out.keyMajor = major_;
    out.keyClarity = clarity_;
    out.chordHue = chordHue_;
    out.tension = tension_;
}

}  // namespace geode::sound
