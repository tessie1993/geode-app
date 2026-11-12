#pragma once
#include <cstdint>

namespace geode::output {

// The part of a track an export covers when the user picked a segment (core/output; ADR 0003).
struct ExportRange {
    int64_t startMs = 0;
    int64_t durationMs = 0;
};

// Shorter than this is not a segment worth rendering; the segment hint names it in seconds.
constexpr int64_t kMinRangeMs = 1'000;

// A picked start and end held inside the track. False, leaving *out alone, when there is no segment to
// render: the track's length is unknown (<= 0), what is left is shorter than kMinRangeMs, or the pick is
// the whole track, which is rendered as a whole track and not as a range.
constexpr bool exportRangeOf(int64_t startMs, int64_t endMs, int64_t trackMs, ExportRange* out) {
    if (trackMs <= 0) return false;
    const int64_t start = startMs < 0 ? 0 : (startMs > trackMs ? trackMs : startMs);
    const int64_t end = endMs < 0 ? 0 : (endMs > trackMs ? trackMs : endMs);
    if (end - start < kMinRangeMs) return false;
    if (start == 0 && end >= trackMs) return false;
    if (out) *out = ExportRange{start, end - start};
    return true;
}

}  // namespace geode::output
