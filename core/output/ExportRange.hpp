#pragma once
#include <cstdint>

namespace geode::output {

// The part of a track a render covers. durationMs 0 is the whole track.
struct ExportRange {
    int64_t startMs = 0;
    int64_t durationMs = 0;

    constexpr int64_t endMs() const { return startMs + durationMs; }
};

// The shortest part a render is offered: under a second there is nothing to see.
inline constexpr int64_t kMinRangeMs = 1'000;

// A picked start and end held inside the track. False, leaving *out alone, when there is no part to render:
// the track's length is unknown (<= 0), what is left is shorter than kMinRangeMs, or the pick is the whole
// track, which is rendered as a whole track and not as a range.
constexpr bool exportRangeOf(int64_t startMs, int64_t endMs, int64_t trackMs, ExportRange* out) {
    if (trackMs <= 0) return false;
    const int64_t start = startMs < 0 ? 0 : (startMs > trackMs ? trackMs : startMs);
    const int64_t end = endMs < 0 ? 0 : (endMs > trackMs ? trackMs : endMs);
    if (end - start < kMinRangeMs) return false;
    if (start == 0 && end >= trackMs) return false;
    if (out) *out = ExportRange{start, end - start};
    return true;
}

// The part between two slider positions, start and end as fractions of trackMs, by exportRangeOf's rules.
bool rangeFromFractions(float start, float end, int64_t trackMs, ExportRange* out);

// The free tier's cap: a whole-track render becomes the track's first capMs, a longer part keeps its start and
// is cut to capMs. capMs <= 0 is no cap.
ExportRange cappedRange(ExportRange range, int64_t capMs);

}  // namespace geode::output
