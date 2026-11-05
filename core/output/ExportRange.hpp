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

// The free tier's export cap (ported from the Kotlin PlayerViewModel.capRange). Whether a user is capped and
// for how long stay Kotlin's (pro/FreeTier, through FeatureGate.exportCapMs); this only cuts the range.
// range null is the whole track; capMs <= 0 is no cap. With no cap the range comes back as it was; with one,
// the whole track becomes its first capMs and a longer range keeps its start and is cut to capMs. A range no
// longer than the cap is untouched. The track's length is not read: the audio transcode stops at the range's
// end, so a track shorter than the cap is unaffected. False (out untouched) only for the whole track uncapped.
bool capExportRange(const ExportRange* range, int64_t capMs, ExportRange* out);

}  // namespace geode::output
