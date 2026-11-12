#include "sound/Tuning.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace geode::sound::tuning {

namespace {

// The ranges are where the engine stays sensible, not what sounds best; the defaults are the tuned
// values (the drums' differ per drum because a hat's punch is smaller and dies faster than a kick's).
constexpr Slot kGain{1.0f, 0.0f, 4.0f};
constexpr Slot kThreshold{0.0f, 0.0f, 1.0f};
constexpr Slot kOffset{0.0f, -1.0f, 1.0f};
constexpr Slot kAttack{0.03f, 0.0f, 1.0f};
constexpr Slot kRelease{0.15f, 0.0f, 2.0f};
constexpr float kDrumLo = 0.1f;
constexpr float kDrumHi = 3.0f;
constexpr float kDecayLo = 0.02f;
constexpr float kDecayHi = 1.0f;

constexpr std::array<Slot, kSlots> kTable{{
    kGain, kGain, kGain,
    kThreshold, kThreshold, kThreshold,
    kOffset, kOffset, kOffset,
    kAttack, kAttack, kAttack,
    kRelease, kRelease, kRelease,
    {0.6f, kDrumLo, kDrumHi}, {0.5f, kDrumLo, kDrumHi}, {0.5f, kDrumLo, kDrumHi},
    {0.18f, kDecayLo, kDecayHi}, {0.15f, kDecayLo, kDecayHi}, {0.08f, kDecayLo, kDecayHi},
    {1.0f, 0.0f, 3.0f},        // reactivity
    {1.0f, 0.0f, 4.0f},        // speed
    {2.0f, 1.0f, 4.0f},        // onsetThreshold
    {2.5f, 1.5f, 4.0f},        // hardCutLevel
    {60.0f, 5.0f, 180.0f},     // hardCutHalfLife, seconds
    {0.35f, 0.1f, 0.9f},       // pitchThreshold
    {0.08f, 0.02f, 0.5f},      // noteHold, seconds
    {-60.0f, -90.0f, -30.0f},  // silenceDb
    {0.0f, -24.0f, 24.0f},     // inputTrimDb
}};

}  // namespace

const std::array<Slot, kSlots>& slots() { return kTable; }

void clamp(float* values, int count) {
    if (!values) return;
    const int n = std::min(count, kSlots);
    for (int i = 0; i < n; ++i) {
        const Slot& s = kTable[static_cast<size_t>(i)];
        values[i] = std::isfinite(values[i]) ? std::clamp(values[i], s.lo, s.hi) : s.def;
    }
}

// The input trim is the last slot, the one saves from before it lack.
static_assert(offsetof(GeodeSoundTuning, inputTrimDb) == sizeof(float) * (kSlots - 1), "the input trim is the last slot");

bool fromSaved(const float* saved, int count, float* out) {
    if (!out) return false;
    const bool everySlot = count == kSlots;
    const bool beforeTrim = count == kSlots - 1;
    if (!saved || !(everySlot || beforeTrim)) {
        for (int i = 0; i < kSlots; ++i) out[i] = kTable[static_cast<size_t>(i)].def;
        return false;
    }
    std::copy(saved, saved + count, out);
    // A save from before the trim was heard untrimmed: 0 dB, whatever the trim's default becomes.
    if (beforeTrim) out[kSlots - 1] = 0.0f;
    clamp(out, kSlots);
    return true;
}

GeodeSoundTuning defaults() {
    float v[kSlots];
    for (int i = 0; i < kSlots; ++i) v[i] = kTable[static_cast<size_t>(i)].def;
    GeodeSoundTuning t;
    std::memcpy(&t, v, sizeof t);
    return t;
}

GeodeSoundTuning clamp(const GeodeSoundTuning& t) {
    float v[kSlots];
    std::memcpy(v, &t, sizeof v);
    clamp(v, kSlots);
    GeodeSoundTuning out;
    std::memcpy(&out, v, sizeof out);
    return out;
}

}  // namespace geode::sound::tuning
