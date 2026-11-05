#pragma once
#include <cstdint>

namespace geode::output {

// The part of a track a render covers (#422; ported from the Kotlin ExportRange.of). Studio › Render's
// Length picks a part as two handles; this turns them into a range the exporter can trust, or says the
// whole track. Pure, any thread, host-tested by core/tests/output_test.

// A part shorter than this is not a video anyone meant to make.
inline constexpr int64_t kMinExportRangeMs = 1'000;

struct ExportRange {
    int64_t startMs = 0;
    int64_t durationMs = 0;
};

// Both ends clamped into the track. False (the whole track, out untouched) when the track has no length,
// the part is under kMinExportRangeMs, or it covers the whole track anyway.
bool clampExportRange(int64_t startMs, int64_t endMs, int64_t trackDurationMs, ExportRange* out);

}  // namespace geode::output
