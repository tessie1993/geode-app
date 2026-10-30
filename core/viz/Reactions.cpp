#include "viz/Reactions.hpp"

#include <algorithm>
#include <cmath>

#include "viz/LiveSignal.hpp"

namespace geode::viz {

namespace {

// 64 log bands split into seven perceptual regions; the boundaries are band
// indices, not Hz, because the analyser's band edges are what the frame carries.
struct BandRange {
    int from;
    int to;  // exclusive
};
constexpr std::array<BandRange, 7> kRegions = {{{0, 8}, {8, 16}, {16, 26}, {26, 36}, {36, 46}, {46, 55}, {55, GEODE_BAND_COUNT}}};

// Event decay times (seconds): how long a hit stays readable after the hop that flagged it.
constexpr float kOnsetDecay = 0.22f;
constexpr float kBeatDecay = 0.30f;
constexpr float kDownbeatDecay = 0.60f;
constexpr float kDropDecay = 2.50f;
constexpr float kSectionDecay = 1.50f;
constexpr float kPitchDecay = 0.50f;
constexpr float kTimbreDecay = 0.40f;

// A chroma argmax only counts as a pitch change when the detector is this sure.
constexpr float kPitchConfidence = 0.35f;
// A centroid this far from its 1 s average reads as a timbre jump.
constexpr float kTimbreJump = 0.12f;
constexpr float kTimbreSlowSeconds = 1.0f;

constexpr float kMinTempoBpm = 60.0f;
constexpr float kMaxTempoBpm = 180.0f;
constexpr float kTwoPi = 6.2831853f;

// A NaN or infinite frame time (a clock glitch) is a frame that did not
// happen; anything else is clamped to [0, 0.1] s so one long stall cannot
// fling an envelope or an accumulator.
float sanitizeDt(float dt) { return std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.1f) : 0.0f; }

float onePoleK(float dt, float tau) { return 1.0f - std::exp(-std::max(dt, 0.0f) / std::max(tau, 1e-4f)); }

float regionMean(const GeodeFeatureFrame& f, BandRange r) {
    float sum = 0.0f;
    for (int i = r.from; i < r.to; ++i) sum += f.bands[i];
    return sum / static_cast<float>(r.to - r.from);
}

}  // namespace

void ReactSignals::reset() {
    values_.fill(0.0f);
    peaks_.fill(0.0f);
    swellAvg_ = 0.0f;
    swellWarm_ = 0.0f;
    lastKey_ = -1;
    lastCentroid_ = 0.0f;
    centroidSlow_ = 0.0f;
    onsetArmed_ = 1.0f;
    onsetCount_ = 0.0f;
    sectionCount_ = 0.0f;
}

// x over a running peak that decays over seconds, so a quiet track still uses
// the whole 0..1 range and a loud one does not pin every route at 1.
float ReactSignals::normalised(int slot, float x, float dt) {
    const float v = std::max(x, 0.0f);
    float& peak = peaks_[static_cast<size_t>(slot)];
    peak = std::max(v, peak * std::exp(-std::max(dt, 0.0f) / kPeakDecaySeconds));
    return std::clamp(v / std::max(peak, kPeakFloor), 0.0f, 1.0f);
}

void ReactSignals::trigger(ReactSource s, float strength) {
    float& v = values_[static_cast<size_t>(s)];
    v = std::max(v, std::clamp(strength, 0.0f, 1.0f));
}

void ReactSignals::decay(ReactSource s, float dt, float seconds) {
    values_[static_cast<size_t>(s)] *= std::exp(-std::max(dt, 0.0f) / seconds);
}

void ReactSignals::step(const GeodeFeatureFrame& f, float dt) {
    dt = sanitizeDt(dt);
    auto set = [&](ReactSource s, float v) { values_[static_cast<size_t>(s)] = v; };

    for (size_t i = 0; i < kRegions.size(); ++i) {
        const int slot = static_cast<int>(ReactSource::SubBass) + static_cast<int>(i);
        values_[static_cast<size_t>(slot)] = normalised(slot, regionMean(f, kRegions[i]), dt);
    }
    set(ReactSource::Loudness, normalised(static_cast<int>(ReactSource::Loudness), live::level(f), dt));
    swellWarm_ += dt;
    swellAvg_ += (live::level(f) - swellAvg_) * onePoleK(dt, std::min(kSwellSeconds, std::max(swellWarm_, dt)));
    set(ReactSource::Swell, std::clamp(0.5f * live::level(f) / std::max(swellAvg_, 0.02f), 0.0f, 1.0f));
    set(ReactSource::Brightness, live::brightness(f));
    set(ReactSource::Noisiness, std::clamp(1.0f - f.harmonicity, 0.0f, 1.0f));
    set(ReactSource::StereoWidth, live::width(f));
    set(ReactSource::StereoPan, live::pan(f));
    const float lock = std::clamp(f.pulseConfidence, 0.0f, 1.0f);
    set(ReactSource::BeatOsc, 0.5f + 0.5f * std::cos(kTwoPi * f.beatPhase) * lock);
    set(ReactSource::BarOsc, 0.5f + 0.5f * std::cos(kTwoPi * f.barPhase) * lock);
    set(ReactSource::Tempo, f.bpm > 0.0f ? std::clamp((f.bpm - kMinTempoBpm) / (kMaxTempoBpm - kMinTempoBpm), 0.0f, 1.0f) : 0.0f);
    set(ReactSource::Buildup, std::clamp(f.buildup, 0.0f, 1.0f));
    set(ReactSource::Flux, std::clamp(f.onset, 0.0f, 1.0f));
    set(ReactSource::Kick, normalised(static_cast<int>(ReactSource::Kick), f.kick, dt));
    set(ReactSource::Snare, normalised(static_cast<int>(ReactSource::Snare), f.snare, dt));
    set(ReactSource::Hat, normalised(static_cast<int>(ReactSource::Hat), f.hat, dt));

    decay(ReactSource::Onset, dt, kOnsetDecay);
    decay(ReactSource::Beat, dt, kBeatDecay);
    decay(ReactSource::Downbeat, dt, kDownbeatDecay);
    decay(ReactSource::Drop, dt, kDropDecay);
    decay(ReactSource::Section, dt, kSectionDecay);
    decay(ReactSource::PitchChange, dt, kPitchDecay);
    decay(ReactSource::TimbreJump, dt, kTimbreDecay);

    // An onset is the rising edge of the transient, not every hop it stays high.
    const bool hot = f.transient >= live::kHitFloor;
    if (hot && onsetArmed_ > 0.5f) {
        trigger(ReactSource::Onset, 0.4f + 0.6f * f.transient);
        onsetCount_ = std::fmod(onsetCount_ + 1.0f, kOnsetCountWrap);
    }
    onsetArmed_ = hot ? 0.0f : 1.0f;
    if (f.beat > 0.0f) trigger(ReactSource::Beat, live::beatImpulse(f));
    if (f.downbeat > 0.0f) trigger(ReactSource::Downbeat, 1.0f);
    if (f.drop > 0.0f) trigger(ReactSource::Drop, 1.0f);
    if (f.sectionBoundary > 0.0f) {
        trigger(ReactSource::Section, 1.0f);
        sectionCount_ = std::fmod(sectionCount_ + 1.0f, kOnsetCountWrap);
    }

    int key = 0;
    for (int i = 1; i < GEODE_CHROMA_BINS; ++i) {
        if (f.chroma[i] > f.chroma[key]) key = i;
    }
    if (f.chromaConfidence >= kPitchConfidence) {
        if (lastKey_ >= 0 && key != lastKey_) trigger(ReactSource::PitchChange, f.chromaConfidence);
        lastKey_ = key;
    }

    const float centroid = live::brightness(f);
    centroidSlow_ += (centroid - centroidSlow_) * onePoleK(dt, kTimbreSlowSeconds);
    const float jump = std::fabs(centroid - centroidSlow_);
    if (jump >= kTimbreJump && std::fabs(lastCentroid_ - centroidSlow_) < kTimbreJump) trigger(ReactSource::TimbreJump, jump * 4.0f);
    lastCentroid_ = centroid;
}

void ReactionEngine::reset() {
    signals_.reset();
    acc_.fill(0.0f);
    out_.fill(0.0f);
    high_.fill(false);
}

// Triangle wave of period 4 through -1..1: continuous, bounded, slope 1 at 0.
float ReactionEngine::fold(float accumulator) {
    const float t = std::fmod(accumulator + 1.0f, 4.0f);
    const float u = t < 0.0f ? t + 4.0f : t;
    return u < 2.0f ? u - 1.0f : 3.0f - u;
}

void ReactionEngine::tick(const GeodeFeatureFrame& f, float dt) {
    dt = sanitizeDt(dt);
    signals_.step(f, dt);
    for (int i = 0; i < kRoutes; ++i) {
        const ReactRoute& r = routes[static_cast<size_t>(i)];
        const size_t k = static_cast<size_t>(i);
        if (!r.enabled || r.target == LfoTarget::None) {
            out_[k] = 0.0f;
            continue;
        }
        const float src = signals_.value(r.source);
        float target = 0.0f;
        switch (r.mode) {
            case ReactMode::Follow:
                target = shape(src, r.curve) * r.amount;
                break;
            case ReactMode::Step: {
                const bool high = src >= 0.5f;
                if (high && !high_[k]) acc_[k] = std::fmod(acc_[k] + r.amount, 4.0f);
                high_[k] = src >= 0.3f && (high || high_[k]);  // hysteresis: re-arm below 0.3
                target = fold(acc_[k]);
                break;
            }
            case ReactMode::Spin:
                acc_[k] = std::fmod(acc_[k] + shape(src, r.curve) * r.amount * dt, 4.0f);
                target = fold(acc_[k]);
                break;
        }
        float attack = std::max(r.attackSeconds, 0.0f);
        float release = std::max(r.releaseSeconds, 0.0f);
        if (isLuminanceTarget(r.target)) {
            attack = std::max(attack, kLuminanceMinAttack);
            release = std::max(release, kLuminanceMinRelease);
        }
        const float tau = std::fabs(target) > std::fabs(out_[k]) ? attack : release;
        out_[k] += (target - out_[k]) * (tau <= 1e-4f ? 1.0f : onePoleK(dt, tau));
    }
}

SceneParams ReactionEngine::apply(const SceneParams& p) const {
    const float scale = std::clamp(p.reactAmount, 0.0f, 2.0f);
    SceneParams o = p;
    if (scale <= 0.0f) return o;
    for (int i = 0; i < kRoutes; ++i) {
        const ReactRoute& r = routes[static_cast<size_t>(i)];
        ModChain chain{};
        if (!r.enabled || r.target == LfoTarget::None || chainOf(r.target, chain)) continue;
        o = applyLfoTarget(o, r.target, out_[static_cast<size_t>(i)] * scale);
    }
    return o;
}

}  // namespace geode::viz
