#include "output/RecordPlan.hpp"

#include "output/EncodePlan.hpp"

#include <cstdlib>

namespace geode::output::record {

namespace {
constexpr int64_t kUsPerSecond = 1'000'000;
constexpr int64_t kBitsPerByte = 8;
}  // namespace

int maxLengthAt(int index) { return index >= 0 && index < kMaxLengthCount ? kMaxLengthSec[index] : kNoLimitSec; }

int snapMaxLength(int requestedSec) {
    if (requestedSec <= 0) return kNoLimitSec;
    int best = kMaxLengthSec[1];
    for (int i = 2; i < kMaxLengthCount; ++i) {
        if (std::abs(kMaxLengthSec[i] - requestedSec) < std::abs(best - requestedSec)) best = kMaxLengthSec[i];
    }
    return best;
}

Estimate estimate(int videoBps, int lengthSec) {
    if (videoBps <= 0 || lengthSec <= 0) return {};
    const int64_t durationUs = lengthSec * kUsPerSecond;
    const int64_t audio = encode::audioBytes(durationUs);
    const int bps = encode::cappedVideoBps(videoBps, durationUs, audio);
    return {bps * durationUs / (kBitsPerByte * kUsPerSecond) + audio, bps < videoBps};
}

}  // namespace geode::output::record
