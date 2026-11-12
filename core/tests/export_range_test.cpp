// Host-side checks for the segment an export covers (core/output/ExportRange): a pick is held
// inside the track, and there is no segment without a track length, under a second, or over the whole track.
// Built and run by CI (.github/workflows/native-host-tests.yml, "Native export planning tests"); no device.
#include <cstdint>
#include <cstdio>
#include <limits>

#include "output/ExportRange.hpp"

using namespace geode::output;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool rangeIs(int64_t startMs, int64_t endMs, int64_t trackMs, int64_t wantStart, int64_t wantDuration) {
    ExportRange r;
    return exportRangeOf(startMs, endMs, trackMs, &r) && r.startMs == wantStart && r.durationMs == wantDuration;
}

bool none(int64_t startMs, int64_t endMs, int64_t trackMs) {
    ExportRange r{7, 9};
    return !exportRangeOf(startMs, endMs, trackMs, &r) && r.startMs == 7 && r.durationMs == 9;
}

void rules() {
    static_assert(kMinRangeMs == 1'000);
    static_assert(exportRangeOf(10'000, 20'000, 60'000, nullptr));

    check(rangeIs(10'000, 20'000, 60'000, 10'000, 10'000), "a pick inside the track is itself");
    check(rangeIs(-5'000, 20'000, 60'000, 0, 20'000), "a start before the track is its start");
    check(rangeIs(50'000, 90'000, 60'000, 50'000, 10'000), "an end past the track is its end");
    check(rangeIs(1, 60'000, 60'000, 1, 59'999), "all but the first millisecond is a segment");
    check(rangeIs(0, 59'999, 60'000, 0, 59'999), "all but the last millisecond is a segment");
    check(rangeIs(5'000, 6'000, 60'000, 5'000, 1'000), "exactly the shortest");

    check(none(0, 10'000, 0) && none(0, 10'000, -1), "no track length, no segment");
    check(none(5'000, 5'999, 60'000), "under the shortest");
    check(none(20'000, 10'000, 60'000), "an end before the start");
    check(none(70'000, 90'000, 60'000), "a pick wholly past the track");
    check(none(-9'000, -1'000, 60'000), "a pick wholly before the track");
    check(none(59'500, 90'000, 60'000), "what is left after the clamp is too short");
    check(none(0, 60'000, 60'000) && none(-1, 60'001, 60'000), "the whole track is not a segment");
    check(none(0, 500, 500), "a track under the shortest has none");

    const int64_t most = std::numeric_limits<int64_t>::max();
    const int64_t least = std::numeric_limits<int64_t>::min();
    check(none(least, most, most), "the widest pick over the longest track is the whole track");
    check(none(least, most, 5'000), "and over a short one");
    check(none(most - 1, most, most), "the far end, too short");
    check(rangeIs(1, most, most, 1, most - 1), "the far end");
}

}  // namespace

int main() {
    rules();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("export_range_test: all passed\n");
    return 0;
}
