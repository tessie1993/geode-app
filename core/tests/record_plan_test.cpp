// Host-side checks for a recorded take's limits (core/output/RecordPlan) and their C ABI: the maximum
// lengths Settings › Export › Record offers, snapping a stored value onto them, when a take stops, the
// length it is saved at, and the file size a length means, with the muxer's 3.8 GB target applied. Built
// and run by CI (.github/workflows/native-host-tests.yml, "Native export planning tests"); no device.
#include <cstdint>
#include <cstdio>
#include <initializer_list>

#include "api/geode_api.h"
#include "output/EncodePlan.hpp"
#include "output/RecordPlan.hpp"

using namespace geode::output;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void lengths() {
    using namespace record;
    check(kMaxLengthSec[0] == kNoLimitSec, "no limit is the first choice, the default");
    for (int i = 2; i < kMaxLengthCount; ++i) check(kMaxLengthSec[i] > kMaxLengthSec[i - 1], "lengths rise");
    check(maxLengthAt(-1) == kNoLimitSec && maxLengthAt(kMaxLengthCount) == kNoLimitSec, "outside the list is no limit");
    check(geode_record_max_length_count() == kMaxLengthCount, "ABI count");
    for (int i = 0; i < kMaxLengthCount; ++i) {
        check(geode_record_max_length_at(i) == kMaxLengthSec[i], "ABI lengths");
        check(snapMaxLength(kMaxLengthSec[i]) == kMaxLengthSec[i], "an offered length snaps to itself");
    }
    check(snapMaxLength(0) == kNoLimitSec && snapMaxLength(-5) == kNoLimitSec, "zero or less is no limit");
    check(snapMaxLength(1) == 15, "a positive length is never rounded down to no limit");
    check(snapMaxLength(50) == 60 && snapMaxLength(40) == 30, "the nearest offered length");
    check(snapMaxLength(45) == 30, "a tie goes to the shorter");
    check(snapMaxLength(1'000'000) == 1800, "past the longest is the longest");
    check(geode_record_snap_max_length(170) == 180, "ABI snap");
}

void size() {
    using namespace record;
    // 1080p's 12 Mbit/s plus 192 kbit/s AAC: 1.524 MB a second.
    const Estimate minute = estimate(12'000'000, 60);
    check(minute.bytes == 91'440'000 && !minute.lowered, "a minute of 1080p");
    const Estimate fourK = estimate(40'000'000, 300);
    check(fourK.bytes == 1'507'200'000 && !fourK.lowered, "five minutes of 4K fit as asked");
    // Half an hour of 4K at 40 Mbit/s would be 9 GB: the render lowers the rate, and so does the estimate.
    const Estimate longFourK = estimate(40'000'000, 1800);
    check(longFourK.lowered, "half an hour of 4K is lowered");
    check(longFourK.bytes <= encode::kTargetBytes && longFourK.bytes > encode::kTargetBytes - 1'000,
          "a lowered render lands on the muxer's target");
    check(!estimate(12'000'000, 1800).lowered, "half an hour of 1080p fits");
    check(estimate(12'000'000, 0).bytes == 0 && estimate(0, 60).bytes == 0, "no length or no rate is no estimate");
    // Every offered length at every quality stays under the hard limit MediaMuxer can index.
    for (const int bps : {6'000'000, 12'000'000, 40'000'000}) {
        for (int i = 1; i < kMaxLengthCount; ++i) {
            check(estimate(bps, kMaxLengthSec[i]).bytes < encode::kMuxerHardLimitBytes, "every choice is writable");
        }
    }
    int lowered = -1;
    check(geode_record_estimate_bytes(12'000'000, 60, &lowered) == 91'440'000 && lowered == 0, "ABI estimate");
    check(geode_record_estimate_bytes(40'000'000, 1800, &lowered) == longFourK.bytes && lowered == 1, "ABI lowered");
    check(geode_record_estimate_bytes(12'000'000, 60, nullptr) == 91'440'000, "ABI without the flag");
}

}  // namespace

int main() {
    lengths();
    size();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("record_plan_test: all passed\n");
    return 0;
}
