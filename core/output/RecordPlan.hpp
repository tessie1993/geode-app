#pragma once
#include <cstdint>

namespace geode::output {

// A recorded take's limits (Settings › Export › Record): how long a take may run, and what its MP4 will
// weigh. Kotlin stores the user's three choices (resolution, frame rate, maximum length) and draws the rows;
// which lengths are offered, when a take has reached its limit and the size a length means are decided here.
namespace record {

// "No limit": the take runs until the user stops it (or its event store is full).
constexpr int kNoLimitSec = 0;

// The maximum lengths offered, in seconds, shortest first after kNoLimitSec. They follow where takes go:
// 15, 30 and 60 s are the short-video limits, 3 min a long short, 5 to 30 min a song or a set.
constexpr int kMaxLengthSec[] = {kNoLimitSec, 15, 30, 60, 180, 300, 600, 1800};
constexpr int kMaxLengthCount = static_cast<int>(sizeof(kMaxLengthSec) / sizeof(kMaxLengthSec[0]));

// The i-th offered maximum length, or kNoLimitSec outside the list.
int maxLengthAt(int index);

// A stored maximum length made one of the offered ones: the nearest (the shorter on a tie), and
// kNoLimitSec for zero or less, so a value from an older build or a hand-edited backup is never trusted.
int snapMaxLength(int requestedSec);

struct Estimate {
    int64_t bytes = 0;      // video plus AAC
    bool lowered = false;   // the video bit rate is lowered so the file stays under the muxer's target
};

// What lengthSec of video at videoBps weighs with its AAC soundtrack. Past the muxer's target the render
// lowers the bit rate instead of failing (encode::cappedVideoBps), and so does this. Zero for no length.
Estimate estimate(int videoBps, int lengthSec);

}  // namespace record
}  // namespace geode::output
