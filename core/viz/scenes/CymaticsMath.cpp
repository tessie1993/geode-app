#include "viz/scenes/CymaticsMath.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace geode::viz::cymatics {

namespace {
constexpr float kTwoPi = 6.2831853f;
}

const std::vector<Mode>& modes() {
    static const std::vector<Mode> kModes = [] {
        std::vector<Mode> out;
        for (int n = 1; n <= kMaxOrder; ++n) {
            for (int m = 0; m < n; ++m) out.push_back({n, m, std::sqrt(static_cast<float>(n * n + m * m))});
        }
        std::stable_sort(out.begin(), out.end(), [](const Mode& a, const Mode& b) { return a.wavenumber < b.wavenumber; });
        return out;
    }();
    return kModes;
}

float bandCenterHz(int band, int bandCount) {
    if (bandCount <= 0) return kMinBandHz;
    const double ratio = std::log(kMaxBandHz / kMinBandHz);
    const float low = kMinBandHz * static_cast<float>(std::exp(ratio * band / bandCount));
    const float high = kMinBandHz * static_cast<float>(std::exp(ratio * (band + 1) / bandCount));
    return std::sqrt(low * high);
}

float wavenumberFor(float hz, float fundamentalHz) {
    const float f0 = std::clamp(fundamentalHz, kMinFundamentalHz, kMaxFundamentalHz);
    return std::sqrt(std::max(hz / f0, 0.0f));
}

int modeIndexFor(float wavenumber) {
    int best = 0;
    float bestDelta = std::numeric_limits<float>::max();
    const auto& table = modes();
    for (size_t i = 0; i < table.size(); ++i) {
        const float delta = std::fabs(table[i].wavenumber - wavenumber);
        if (delta > bestDelta) break;
        best = static_cast<int>(i);
        bestDelta = delta;
    }
    return best;
}

std::vector<int> bandModeMap(int bandCount, float fundamentalHz) {
    std::vector<int> out(static_cast<size_t>(std::max(bandCount, 0)));
    for (int band = 0; band < bandCount; ++band) {
        out[static_cast<size_t>(band)] = modeIndexFor(wavenumberFor(bandCenterHz(band, bandCount), fundamentalHz));
    }
    return out;
}

float ringSeconds(float ring) { return kMinRingSeconds + (kMaxRingSeconds - kMinRingSeconds) * std::clamp(ring, 0.0f, 1.0f); }

float smoothing(float dt, float tau) { return tau <= 0.0f ? 1.0f : std::clamp(1.0f - std::exp(-dt / tau), 0.0f, 1.0f); }

float vibrationHz(float wavenumber) { return std::clamp(kVibrationHzPerOrder * wavenumber, kMinVibrationHz, kMaxVibrationHz); }

float safeDrive(float raw) { return std::isfinite(raw) ? std::clamp(raw, 0.0f, kMaxDrive) : 0.0f; }

float fieldLiveness(float totalAmplitude) { return std::isfinite(totalAmplitude) ? std::clamp(totalAmplitude / kLiveAmplitude, 0.0f, 1.0f) : 0.0f; }

float wrapPhase(float value, float period) {
    if (!std::isfinite(value) || period <= 0.0f) return 0.0f;
    const float r = std::fmod(value, period);
    return r < 0.0f ? r + period : r;
}

float approachHue(float current, float target, float alpha) {
    float d = std::fmod(target - current, 1.0f);
    if (d > 0.5f) d -= 1.0f;
    if (d < -0.5f) d += 1.0f;
    const float next = current + d * std::clamp(alpha, 0.0f, 1.0f);
    return next - std::floor(next);
}

Plate::Plate()
    : amplitudes_(modes().size(), 0.0f), taken_(modes().size(), false), excitation_(modes().size(), 0.0f), phases_(modes().size(), 0.0f) {}

void Plate::reset() {
    std::fill(amplitudes_.begin(), amplitudes_.end(), 0.0f);
    std::fill(excitation_.begin(), excitation_.end(), 0.0f);
    std::fill(phases_.begin(), phases_.end(), 0.0f);
    dominantWavenumber_ = 0.0f;
}

void Plate::excite(const float* bands, int bandCount, float dt, float fundamentalHz, float drive, float ringSecs, float focus) {
    if (bandCount <= 0 || dt <= 0.0f) return;
    ensureMap(bandCount, fundamentalHz);
    if (static_cast<int>(smoothed_.size()) != bandCount) smoothed_.assign(static_cast<size_t>(bandCount), 0.0f);
    const float f = std::clamp(focus, 0.0f, 1.0f);
    localMean(bands, bandCount);
    std::fill(excitation_.begin(), excitation_.end(), 0.0f);
    for (int b = 0; b < bandCount; ++b) {
        const float raw = std::max(bands[b], 0.0f);
        const float peak = std::max(raw - smoothed_[static_cast<size_t>(b)], 0.0f) * kWhitenGain;
        const float value = (raw * (1.0f - f) + peak * f) * drive;
        const size_t mode = static_cast<size_t>(map_[static_cast<size_t>(b)]);
        if (value > excitation_[mode]) excitation_[mode] = value;
    }
    const float attack = smoothing(dt, kAttackSeconds);
    const float release = smoothing(dt, ringSecs);
    float loudest = 0.0f;
    int loudestIndex = -1;
    for (size_t i = 0; i < amplitudes_.size(); ++i) {
        const float target = excitation_[i];
        const float a = amplitudes_[i];
        float next = a + (target - a) * (target > a ? attack : release);
        if (next < kSilence) next = 0.0f;
        amplitudes_[i] = next;
        if (next > loudest) {
            loudest = next;
            loudestIndex = static_cast<int>(i);
        }
    }
    dominantWavenumber_ = loudestIndex >= 0 ? modes()[static_cast<size_t>(loudestIndex)].wavenumber : 0.0f;
}

void Plate::advancePhases(float dt, float speed) {
    if (dt <= 0.0f) return;
    const float rate = std::clamp(speed, 0.05f, 4.0f) * kTwoPi * dt;
    for (size_t i = 0; i < phases_.size(); ++i) {
        if (amplitudes_[i] <= kSilence) continue;
        phases_[i] = std::fmod(phases_[i] + vibrationHz(modes()[i].wavenumber) * rate, kTwoPi);
    }
}

int Plate::snapshot(int limit, float* out, int outFloats) {
    const int want = std::min(std::clamp(limit, 1, kMaxRenderedModes), outFloats / 4);
    int written = 0;
    float total = 0.0f;
    std::fill(taken_.begin(), taken_.end(), false);
    for (int k = 0; k < want; ++k) {
        int bestIndex = -1;
        float best = 0.0f;
        for (size_t i = 0; i < amplitudes_.size(); ++i) {
            if (taken_[i]) continue;
            if (amplitudes_[i] > best) {
                best = amplitudes_[i];
                bestIndex = static_cast<int>(i);
            }
        }
        if (bestIndex < 0 || best <= kSilence) continue;
        taken_[static_cast<size_t>(bestIndex)] = true;
        const Mode& mode = modes()[static_cast<size_t>(bestIndex)];
        float* o = out + written * 4;
        o[0] = static_cast<float>(mode.n);
        o[1] = static_cast<float>(mode.m);
        o[2] = best;
        o[3] = phases_[static_cast<size_t>(bestIndex)];
        total += best;
        written++;
    }
    if (written == 0) return 0;
    const float norm = 1.0f / std::max(1.0f, total);
    for (int i = 0; i < written; ++i) out[i * 4 + 2] *= norm;
    return written;
}

void Plate::ensureMap(int bandCount, float fundamentalHz) {
    const float f0 = std::clamp(fundamentalHz, kMinFundamentalHz, kMaxFundamentalHz);
    if (bandCount == mapBandCount_ && f0 == mapFundamental_) return;
    map_ = bandModeMap(bandCount, f0);
    mapBandCount_ = bandCount;
    mapFundamental_ = f0;
}

void Plate::localMean(const float* bands, int bandCount) {
    for (int b = 0; b < bandCount; ++b) {
        float sum = 0.0f;
        int n = 0;
        for (int k = b - kWhitenRadius; k <= b + kWhitenRadius; ++k) {
            if (k < 0 || k >= bandCount) continue;
            sum += std::max(bands[k], 0.0f);
            n++;
        }
        smoothed_[static_cast<size_t>(b)] = n > 0 ? sum / static_cast<float>(n) : 0.0f;
    }
}

void Drops::update(float dt, float hit) {
    if (dt <= 0.0f) return;
    cooldown_ = std::max(cooldown_ - dt, 0.0f);
    const float fade = std::exp(-dt / kDecaySeconds);
    for (int i = 0; i < kSlots; ++i) {
        const size_t base = static_cast<size_t>(i) * 4;
        if (packed_[base + 3] <= 0.0f) continue;
        packed_[base + 2] = wrapPhase(packed_[base + 2] + kOmega * dt, kTwoPi);
        packed_[base + 3] *= fade;
        if (packed_[base + 3] < kDropSilence) packed_[base + 3] = 0.0f;
    }
    if (hit > kSpawnThreshold && cooldown_ <= 0.0f) {
        spawn(hit);
        cooldown_ = kCooldownSeconds;
    }
}

void Drops::reset() {
    packed_.fill(0.0f);
    cooldown_ = 0.0f;
}

void Drops::spawn(float strength) {
    const size_t base = static_cast<size_t>(next_) * 4;
    next_ = (next_ + 1) % kSlots;
    seed_++;
    packed_[base] = (hash(seed_) - 0.5f) * 2.0f * kSpread;
    packed_[base + 1] = (hash(seed_ * 7 + 3) - 0.5f) * 2.0f * kSpread;
    packed_[base + 2] = 0.0f;
    packed_[base + 3] = 0.22f + 0.33f * std::clamp(strength, 0.0f, 1.5f);
}

float Drops::hash(int n) {
    const float x = std::sin(static_cast<float>(n) * 12.9898f) * 43758.547f;
    return x - std::floor(x);
}

}  // namespace geode::viz::cymatics
