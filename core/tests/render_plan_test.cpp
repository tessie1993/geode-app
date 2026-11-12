// Host checks for a render's plan (core/output: ExportRange) and its C ABI (core/api/geode_render_plan_api).
// Built and run by CI (.github/workflows/native-host-tests.yml, "Render plan host test"); no device.
#include <cstdint>
#include <cstdio>

#include "api/geode_render_plan_api.h"
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

bool same(const ExportRange& r, int64_t start, int64_t duration) { return r.startMs == start && r.durationMs == duration; }

void ranges() {
    ExportRange r;
    check(!rangeFromFractions(0.0f, 1.0f, 180'000, &r), "the whole track is no range");
    check(rangeFromFractions(0.25f, 0.75f, 200'000, &r) && same(r, 50'000, 100'000), "the middle half");
    check(!rangeFromFractions(0.25f, 0.75f, 0, &r), "an unknown length is the whole track");
    check(!rangeFromFractions(0.25f, 0.75f, -5, &r), "a negative length is the whole track");
    check(!rangeFromFractions(0.5f, 0.5004f, 2'000, &r), "under a second is no range");
    check(rangeFromFractions(0.0f, 0.5f, 2'000, &r) && same(r, 0, 1'000), "exactly a second is a range");
    check(rangeFromFractions(-0.1f, 0.5f, 10'000, &r) && same(r, 0, 5'000), "a start before the track clamps to 0");
    check(rangeFromFractions(0.5f, 1.5f, 10'000, &r) && same(r, 5'000, 5'000), "an end past the track clamps to it");
    check(!rangeFromFractions(0.7f, 0.2f, 10'000, &r), "an end before the start is no range");
    // The product is a float and is cut toward zero: 0.1f * 33 333 is 3 333.3.
    check(rangeFromFractions(0.1f, 0.9f, 33'333, &r) && r.startMs == 3'333, "the start is cut toward zero");
    check(!rangeFromFractions(0.0f, 1.0f, 180'000, nullptr), "no out is no range");
}

void caps() {
    check(same(cappedRange({0, 0}, 60'000), 0, 60'000), "a capped whole track is its first minute");
    check(same(cappedRange({30'000, 90'000}, 60'000), 30'000, 60'000), "a longer part keeps its start");
    check(same(cappedRange({30'000, 20'000}, 60'000), 30'000, 20'000), "a shorter part is untouched");
    check(same(cappedRange({30'000, 90'000}, 0), 30'000, 90'000), "no cap");
    check(same(cappedRange({0, 0}, -1), 0, 0), "no cap keeps the whole track");
}

void abi() {
    int64_t start = -1;
    int64_t duration = -1;
    check(geode_export_range_of(0.25f, 0.75f, 200'000, &start, &duration) == 1 && start == 50'000 &&
              duration == 100'000,
          "ABI range");
    check(geode_export_range_of(0.0f, 1.0f, 200'000, &start, &duration) == 0, "ABI whole track");
    geode_export_range_capped(0, 0, 60'000, &start, &duration);
    check(start == 0 && duration == 60'000, "ABI cap");
    check(geode_export_range_min_ms() == 1'000, "ABI shortest range");
}

}  // namespace

int main() {
    ranges();
    caps();
    abi();
    if (failures == 0) std::printf("render_plan_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
