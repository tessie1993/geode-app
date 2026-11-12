#pragma once
#include <cstdint>

namespace geode::output {

// The part of a track a render covers. durationMs 0 is the whole track.
struct ExportRange {
    int64_t startMs = 0;
    int64_t durationMs = 0;
};

// The shortest part a render is offered: under a second there is nothing to see.
inline constexpr int64_t kMinRangeMs = 1000;

// The part between two slider positions, start and end as fractions of trackMs. False, and the render covers the
// whole track, when the length is unknown, the part is under kMinRangeMs, or it is the whole track anyway.
bool rangeFromFractions(float start, float end, int64_t trackMs, ExportRange* out);

// The free tier's cap: a whole-track render becomes the track's first capMs, a longer part keeps its start and
// is cut to capMs. capMs <= 0 is no cap.
ExportRange cappedRange(ExportRange range, int64_t capMs);

}  // namespace geode::output
