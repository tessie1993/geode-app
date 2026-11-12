#include "output/ExportRange.hpp"

#include <algorithm>
#include <cmath>

namespace geode::output {

namespace {

// A slider fraction of the track in milliseconds: the product in float, cut toward zero, as the slider has always
// been read.
int64_t fractionMs(float fraction, int64_t trackMs) {
    const float ms = fraction * static_cast<float>(trackMs);
    if (std::isnan(ms) || ms <= 0.0f) return 0;
    // Past what an int64 holds the cast is undefined; past the track the clamp gives trackMs anyway.
    if (ms >= 9.0e18f) return trackMs;
    return std::min(static_cast<int64_t>(ms), trackMs);
}

}  // namespace

bool rangeFromFractions(float start, float end, int64_t trackMs, ExportRange* out) {
    if (!out || trackMs <= 0) return false;
    const int64_t from = fractionMs(start, trackMs);
    const int64_t to = fractionMs(end, trackMs);
    if (to - from < kMinRangeMs) return false;
    if (from == 0 && to >= trackMs) return false;
    *out = {from, to - from};
    return true;
}

ExportRange cappedRange(ExportRange range, int64_t capMs) {
    if (capMs <= 0) return range;
    if (range.durationMs <= 0) return {0, capMs};
    if (range.durationMs > capMs) return {range.startMs, capMs};
    return range;
}

}  // namespace geode::output
