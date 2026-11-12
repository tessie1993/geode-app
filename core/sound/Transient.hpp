#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// Transients: the sharp start of any sound (a pluck, a click, a consonant, a struck chord, a drum), heard
// per sample in a few milliseconds. The frame's `onset` is a spectral-flux peak on the 10 ms hop and lands
// ~70 ms late; the drums listen to their own bands only. This listens to every sound, at the speed of its
// attack.
//
// Per band (the filter bank's low, middle and high, and the whole mix, each as the bank filters it), two
// one-pole followers of the sample's power: a fast one of a few milliseconds and a slow one of tens. A steady
// or slowly changing sound keeps them together (their ratio sits near 0 dB); an attack lifts the fast one
// over the slow one within a millisecond. That ratio, in dB, is the detection function. A band whose sound
// jumps fires even under a loud other band: a hi-hat under a held bass shows in the high band, not the mix.
//
// Both followers weigh the newest sample most, the fast one by a fixed multiple of the slow one, so the ratio
// has a ceiling: no attack, however loud, reads past that multiple (about the ratio of their time constants,
// 10 to 40 here). An instant step from quiet reaches it; how close an attack came to it is its sharpness. The slow follower is short (20–30 ms), so a swell, a fade-in or a filter sweep, which only
// rises a fraction of a dB per millisecond, lags it by a few dB at most and never fires.
//
// The threshold adapts to the music: from the last second of the detection function (each 10 ms hop's
// peak, per band) its median plus a margin plus a multiple of its median absolute deviation, and never under
// a fixed jump. Median and MAD, not mean and deviation, so the transients themselves do not lift the bar;
// the history is a fixed ring and the statistics run on a stack copy, so nothing allocates. A steady tone's
// ripple (a low note's waveform shows in the fast follower) raises its own band's bar above it.
//
// Then TouchDesigner's trigger rules, per sample: fire past the threshold, re-arm only once the band that
// fired is back under half of it (in dB), and not again within the refractory time, so one attack seen in
// several bands is one transient. The attack is measured for a few milliseconds after the crossing (its
// peak ratio), and the hop that holds the end of that window reports it: the strength (the peak over twice
// the threshold, as Trigger does) and the sharpness.
//
// No tempo anywhere: nothing here expects a beat or an interval between attacks. Time constants are in
// seconds and turned into samples at configure(), so 44.1 and 48 kHz hear alike; the statistics count
// hops, which are 10 ms at every rate. Whether a transient is heard at all (the silence gate, the hit
// floor) and what it is worth (the drive) is the engine's call, as for every hit (SoundEngine::transients).
namespace geode::sound {

class Transient {
public:
    // Low (< 250 Hz), middle (250 Hz–4 kHz), high (> 4 kHz) and the whole mix.
    static constexpr int kBands = 4;
    // The fast followers: long enough that a low note's waveform does not ripple the low band and the mix by
    // more than a couple of dB, short enough to see a click.
    static constexpr std::array<float, kBands> kFastSeconds = {0.003f, 0.001f, 0.0005f, 0.002f};
    // The slow followers. Equal-ish across bands, so a swell is rejected alike in each.
    static constexpr std::array<float, kBands> kSlowSeconds = {0.03f, 0.02f, 0.02f, 0.02f};
    // Added to both followers (about -74 dBFS, the engine's band floor squared): a band holding only rounding
    // noise has no attacks, and a sound rising out of silence is measured from here, not from zero.
    static constexpr float kFloorPower = 4e-8f;
    // The smallest jump that is an attack: the fast follower four times the slow one.
    static constexpr float kMinJumpDb = 6.0f;
    // The threshold sits this far over the last second's median hop peak, plus kSpread of its MAD.
    static constexpr float kMarginDb = 3.0f;
    static constexpr float kSpread = 3.0f;
    // One second of hop peaks (Rhythm's 10 ms hop at every rate).
    static constexpr int kHistory = 100;
    // A hat roll's spacing, the drums' shortest.
    static constexpr float kRefractorySeconds = 0.03f;
    // How long after the crossing the attack is measured before it is reported.
    static constexpr float kAttackSeconds = 0.005f;

    // One hop's report: strength 0 when nothing fired; else 0.05..1, and the attack's sharpness, 0..1.
    struct Hit {
        float strength = 0.0f;
        float sharpness = 0.0f;
    };

    void configure(int sampleRateHz) {
        const float fs = static_cast<float>(std::max(8000, sampleRateHz));
        for (int b = 0; b < kBands; b++) {
            fastK_[b] = 1.0f - std::exp(-1.0f / (kFastSeconds[b] * fs));
            slowK_[b] = 1.0f - std::exp(-1.0f / (kSlowSeconds[b] * fs));
            ceilingDb_[b] = 10.0f * std::log10(fastK_[b] / slowK_[b]);  // the newest sample's weights
        }
        refractorySamples_ = std::max(1, static_cast<int>(std::lround(kRefractorySeconds * fs)));
        attackSamples_ = std::max(1, static_cast<int>(std::lround(kAttackSeconds * fs)));
        reset();
    }

    void reset() {
        fast_ = {};
        slow_ = {};
        hopPeak_ = {};
        history_ = {};
        head_ = 0;
        filled_ = 0;
        for (int b = 0; b < kBands; b++) setThreshold(b, kMinJumpDb);
        armed_ = true;
        since_ = refractorySamples_;
        attackLeft_ = 0;
        band_ = 0;
        peak_ = 1.0f;
        pending_ = Hit{};
    }

    // One sample of each band, as the filter bank filtered it.
    void process(const std::array<float, kBands>& x) {
        if (since_ < refractorySamples_) since_++;
        int best = -1;      // the band furthest over its threshold, if any is
        float bestOver = 1.0f;
        std::array<float, kBands> ratio{};
        for (int b = 0; b < kBands; b++) {
            const float p = x[b] * x[b];
            fast_[b] += fastK_[b] * (p - fast_[b]);
            slow_[b] += slowK_[b] * (p - slow_[b]);
            if (fast_[b] < 1e-20f) fast_[b] = 0.0f;  // silence must not decay into denormals
            if (slow_[b] < 1e-20f) slow_[b] = 0.0f;
            const float r = (fast_[b] + kFloorPower) / (slow_[b] + kFloorPower);
            ratio[b] = r;
            hopPeak_[b] = std::max(hopPeak_[b], r);
            const float over = r / threshold_[b];
            if (over > bestOver) {
                bestOver = over;
                best = b;
            }
        }
        if (attackLeft_ > 0) {
            peak_ = std::max(peak_, ratio[band_]);
            if (--attackLeft_ == 0) report();
        }
        if (!armed_ && ratio[band_] < rearm_[band_]) armed_ = true;
        if (armed_ && best >= 0 && since_ >= refractorySamples_) {
            armed_ = false;
            since_ = 0;
            band_ = best;
            peak_ = ratio[best];
            fireThresholdDb_ = thresholdDb_[best];
            attackLeft_ = attackSamples_;
        }
    }

    // Closes a hop: its peaks join the last second, every band's threshold is read again from it, and the
    // attack whose measurement ended in this hop (if one did) is handed over.
    Hit hop() {
        for (int b = 0; b < kBands; b++) {
            history_[static_cast<size_t>(b)][static_cast<size_t>(head_)] = 10.0f * std::log10(std::max(hopPeak_[b], 1e-6f));
            hopPeak_[b] = 0.0f;
        }
        head_ = (head_ + 1) % kHistory;
        filled_ = std::min(filled_ + 1, kHistory);
        for (int b = 0; b < kBands; b++) {
            std::array<float, kHistory> v = history_[static_cast<size_t>(b)];
            const auto mid = v.begin() + filled_ / 2;
            std::nth_element(v.begin(), mid, v.begin() + filled_);
            const float median = *mid;
            for (int i = 0; i < filled_; i++) v[static_cast<size_t>(i)] = std::fabs(v[static_cast<size_t>(i)] - median);
            std::nth_element(v.begin(), mid, v.begin() + filled_);
            const float mad = *mid;
            setThreshold(b, std::max(kMinJumpDb, median + kMarginDb + kSpread * mad));
        }
        const Hit out = pending_;
        pending_ = Hit{};
        return out;
    }

private:
    void setThreshold(int b, float db) {
        thresholdDb_[b] = db;
        threshold_[b] = std::pow(10.0f, db / 10.0f);
        rearm_[b] = std::pow(10.0f, db / 20.0f);  // half the threshold, in dB
    }

    // The attack's measurement is done: its strength as Trigger reads one (the peak over twice the threshold,
    // in dB) and its sharpness, how far past the threshold it went of the way to the band's ceiling.
    void report() {
        const float peakDb = 10.0f * std::log10(peak_);
        const float thr = fireThresholdDb_;
        Hit h;
        h.strength = std::clamp(peakDb / (2.0f * thr), 0.05f, 1.0f);
        const float room = ceilingDb_[band_] - thr;
        h.sharpness = room > 0.0f ? std::clamp((peakDb - thr) / room, 0.0f, 1.0f) : 1.0f;
        if (h.strength > pending_.strength) pending_ = h;
    }

    std::array<float, kBands> fastK_{};
    std::array<float, kBands> slowK_{};
    std::array<float, kBands> ceilingDb_{};
    int refractorySamples_ = 1;
    int attackSamples_ = 1;

    std::array<float, kBands> fast_{};
    std::array<float, kBands> slow_{};
    std::array<float, kBands> hopPeak_{};
    std::array<std::array<float, kHistory>, kBands> history_{};
    int head_ = 0;
    int filled_ = 0;
    std::array<float, kBands> thresholdDb_{};
    std::array<float, kBands> threshold_{};
    std::array<float, kBands> rearm_{};
    bool armed_ = true;
    int since_ = 1;
    int attackLeft_ = 0;
    int band_ = 0;
    float peak_ = 1.0f;
    float fireThresholdDb_ = kMinJumpDb;
    Hit pending_{};
};

}  // namespace geode::sound
