// Host-side checks for core/stage/FrameRecorder: the stage's per-frame ring for on-device measurement.
//
// What is pinned: off by default and records nothing while off, the ring keeps the newest kCapacity records oldest
// first with their frame numbers, a fence seen signalled settles the last drawn frame once (never a skipped vsync,
// never a frame the ring no longer holds), clear keeps the numbering so flushed chunks join, and the CSV it serialises
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
    r.drawn(1'000, 2'000, 16'666'667);
    r.skipped();
    r.fenceSignalled(9'000);
    check(r.size() == 0 && !r.awaitingFence(), "and records nothing while off");
    check(r.csv() == std::string(FrameRecorder::kCsvHeader), "its CSV is the header alone");
}

// Red against: a frame's GPU time measured from the swap instead of the CPU start, a fence settling a skipped vsync,
// and a fence settling the same frame twice (the second poll would stretch it).
void aFenceSettlesTheLastDrawnFrameOnce() {
    FrameRecorder r;
    r.setEnabled(true);
    r.drawn(1'000'000, 4'000'000, 0);
    check(r.awaitingFence(), "a drawn frame waits for its fence");
    r.skipped();
    r.fenceSignalled(13'000'000);
    check(!r.awaitingFence(), "and is settled by the first poll that finds it signalled");
    r.fenceSignalled(30'000'000);
    check(r.size() == 2, "two records");
    const FrameRecord& drawn = r.at(0);
    check(drawn.cpuDrawNs == 4'000'000 && drawn.gpuDoneNs == 12'000'000 && !drawn.skipped,
          "the GPU's done time is from the frame's CPU start, set once");
    const FrameRecord& skip = r.at(1);
    check(skip.skipped && skip.gpuDoneNs == 0 && skip.cpuDrawNs == 0, "a skipped vsync takes no fence");
}

// Red against: a ring that grows without bound, one that drops the newest instead of the oldest, frame numbers that
// restart at a flush, and a fence that settles a frame the ring has already let go.
void theRingKeepsTheNewest() {
    FrameRecorder r;
    r.setEnabled(true);
    const int n = static_cast<int>(FrameRecorder::kCapacity) + 3;
    for (int i = 0; i < n; ++i) r.drawn(i * 10, i, 0);
    check(r.size() == FrameRecorder::kCapacity && r.full(), "the ring holds kCapacity records");
    check(r.firstFrame() == 3 && r.at(0).cpuDrawNs == 3, "the oldest went first");
    check(r.at(FrameRecorder::kCapacity - 1).cpuDrawNs == n - 1, "the newest is last");

    r.clear();
    check(r.size() == 0 && !r.full() && !r.awaitingFence(), "a flush empties it and forgets the fence");
    r.drawn(0, 7, 0);
    check(r.firstFrame() == static_cast<uint64_t>(n), "the numbering goes on across a flush");

    // Settled only while the ring still holds the frame.
    FrameRecorder small;
    small.setEnabled(true);
    small.drawn(0, 1, 0);
    for (size_t i = 0; i < FrameRecorder::kCapacity; ++i) small.skipped();
    small.fenceSignalled(5);
    bool anySettled = false;
    for (size_t i = 0; i < small.size(); ++i) anySettled = anySettled || small.at(i).gpuDoneNs != 0;
    check(!anySettled, "a fence for a frame the ring let go settles nothing");

    small.setEnabled(false);
    check(small.size() == 0 && !small.enabled(), "turned off, it lets the ring go");
}

// Red against: a format that drifts from its header, rows out of order, or a skipped flag that is not 0/1.
void itSerialisesToCsv() {
    FrameRecorder r;
    r.setEnabled(true);
    r.drawn(100, 5'230'000, 0);
    r.fenceSignalled(9'200'100);
    r.skipped();
    r.drawn(20'000'000, 4'100'000, 16'666'667);
    const std::string expected = std::string(FrameRecorder::kCsvHeader) +
                                 "0,5230000,9200000,0,0\n"
                                 "1,0,0,0,1\n"
                                 "2,4100000,0,16666667,0\n";
    check(r.csv() == expected, "header, then one row per frame, oldest first");
    if (r.csv() != expected) std::fprintf(stderr, "got:\n%s", r.csv().c_str());
}

}  // namespace

int main() {
    offByDefault();
    aFenceSettlesTheLastDrawnFrameOnce();
    theRingKeepsTheNewest();
    itSerialisesToCsv();
    if (failures) {
        std::fprintf(stderr, "%d frame recorder check(s) failed\n", failures);
        return 1;
    }
    std::printf("frame recorder: all checks passed\n");
    return 0;
}
