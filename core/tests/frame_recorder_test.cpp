// Host-side checks for core/stage/FrameRecorder: the stage's per-frame ring for on-device measurement.
//
// What is pinned: off by default and records nothing while off, the ring keeps the newest kCapacity records oldest
// first with their frame numbers, a GPU time told at the next frame's swap lands on the frame before the newest (never
// on a frame the ring no longer holds), clear keeps the numbering so flushed chunks join, and the CSV it serialises
// to, header and rows exactly.
#include <cstdint>
#include <cstdio>
#include <string>

#include "stage/FrameRecorder.hpp"

using geode::stage::FrameRecord;
using geode::stage::FrameRecorder;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

// Red against: a recorder that records (or allocates its ring) before a debug flag turns it on.
void offByDefault() {
    FrameRecorder r;
    check(!r.enabled(), "a recorder starts off");
    r.drawn(2'000, 16'666'667);
    r.drawn(2'000, 16'666'667);
    r.gpuOfPrevious(9'000);
    check(r.size() == 0, "and records nothing while off");
    check(r.csv() == std::string(FrameRecorder::kCsvHeader), "its CSV is the header alone");
}

// Red against: a GPU time put on the newest frame (the pacer measures the one before it), a time that is not one
// (Swappy's -1 for a fence not yet signalled) recorded, and a first frame given a time with nothing before it.
void aGpuTimeLandsOnTheFrameBeforeTheNewest() {
    FrameRecorder r;
    r.setEnabled(true);
    r.drawn(4'000'000, 0);
    r.gpuOfPrevious(7'000'000);
    check(r.at(0).gpuNs == 0, "the first frame has no frame before it to time");
    r.drawn(5'000'000, 16'666'667);
    r.gpuOfPrevious(7'000'000);
    check(r.at(0).gpuNs == 7'000'000 && r.at(1).gpuNs == 0, "a time told at frame 1's swap is frame 0's");
    r.drawn(5'000'000, 16'666'667);
    r.gpuOfPrevious(-1);
    r.gpuOfPrevious(0);
    check(r.at(1).gpuNs == 0, "a time that is not one records nothing");
}

// Red against: a ring that grows without bound, one that drops the newest instead of the oldest, frame numbers that
// restart at a flush.
void theRingKeepsTheNewest() {
    FrameRecorder r;
    r.setEnabled(true);
    const int n = static_cast<int>(FrameRecorder::kCapacity) + 3;
    for (int i = 0; i < n; ++i) r.drawn(i, 0);
    check(r.size() == FrameRecorder::kCapacity && r.full(), "the ring holds kCapacity records");
    check(r.firstFrame() == 3 && r.at(0).cpuDrawNs == 3, "the oldest went first");
    check(r.at(FrameRecorder::kCapacity - 1).cpuDrawNs == n - 1, "the newest is last");
    r.gpuOfPrevious(11);
    check(r.at(FrameRecorder::kCapacity - 2).gpuNs == 11, "a wrapped ring still times the frame before the newest");

    r.clear();
    check(r.size() == 0 && !r.full(), "a flush empties it");
    r.drawn(7, 0);
    r.gpuOfPrevious(5);
    check(r.at(0).gpuNs == 0, "a flushed frame takes no time");
    check(r.firstFrame() == static_cast<uint64_t>(n), "the numbering goes on across a flush");

    r.setEnabled(false);
    check(r.size() == 0 && !r.enabled(), "turned off, it lets the ring go");
}

// Red against: a format that drifts from its header, or rows out of order.
void itSerialisesToCsv() {
    FrameRecorder r;
    r.setEnabled(true);
    r.drawn(5'230'000, 0);
    r.drawn(4'100'000, 16'666'667);
    r.gpuOfPrevious(9'200'000);
    r.drawn(3'900'000, 33'333'334);
    const std::string expected = std::string(FrameRecorder::kCsvHeader) +
                                 "0,5230000,9200000,0\n"
                                 "1,4100000,0,16666667\n"
                                 "2,3900000,0,33333334\n";
    check(r.csv() == expected, "header, then one row per frame, oldest first");
    if (r.csv() != expected) std::fprintf(stderr, "got:\n%s", r.csv().c_str());
}

}  // namespace

int main() {
    offByDefault();
    aGpuTimeLandsOnTheFrameBeforeTheNewest();
    theRingKeepsTheNewest();
    itSerialisesToCsv();
    if (failures) {
        std::fprintf(stderr, "%d frame recorder check(s) failed\n", failures);
        return 1;
    }
    std::printf("frame recorder: all checks passed\n");
    return 0;
}
