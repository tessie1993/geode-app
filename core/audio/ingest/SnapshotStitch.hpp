#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "audio/ingest/PcmRing.hpp"

// A capture that hands over snapshots, not a stream (the wallpaper's output mix: android.media.audiofx.
// Visualizer gives the newest ~1024 samples about 20 times a second, so ~21 ms heard out of every 50 ms).
// Butted together in the ring, those snapshots read as a stream running at ~0.43x real time with a seam
// every snapshot: the engine's sample clock, so tempo, bars, sections and the LUFS windows, ran 2.3x fast,
// and each seam was a broadband click the onset, hat and flux detectors heard.
//
// This writes each snapshot as the stretch of real time since the last one, measured on the capture clock
// (when the snapshot arrived), so the ring gains as many frames as the wall clock moved and the sample clock
// stays real time. The stretch is filled from the snapshot alone: overlap-added grains half its length at
// random offsets inside it, under a sine window at 50 % overlap (equal power, so the level holds), so no two
// snapshots are ever joined sample to sample and no stretch repeats one period (a fixed loop would read as
// a 47 Hz buzz). Each snapshot's sound is held until the next, which takes over through one grain's
// crossfade: a change between snapshots lands as one onset at its own time, and a steady sound reads steady.
//
// A gap longer than kMaxGapSeconds, or than the ring's runway (what a reader takes at once), is a break (a
// stall, the capture stopped and started again) and is not filled: the clock starts over from the snapshot,
// and the engine's own starvation rule has already heard the silence.
//
// Writer thread only, like the ring. No allocation after construction.
namespace geode::audio::ingest {

class SnapshotStitch {
public:
    // The longest snapshot kept (the newest frames of a longer one) and the most channels it carries.
    static constexpr int kMaxFrames = 4096;
    static constexpr int kMaxChannels = 2;
    // The shortest snapshot it fills from; a shorter one is written as it is.
    static constexpr int kMinFrames = 16;
    // Longer than this since the last snapshot is a break, not a gap to fill.
    static constexpr double kMaxGapSeconds = 0.25;

    SnapshotStitch();

    // One snapshot of whole frames of `encoding` (PcmRing's Encoding) in `bytes` bytes, at sampleRateHz,
    // arrived at nowNs (a monotonic clock). Writes the span since the last snapshot into `ring`; the peak is
    // the snapshot's own.
    PcmRing::Written write(PcmRing& ring, const void* data, size_t bytes, int encoding, int sourceChannels,
                           int sampleRateHz, int64_t nowNs);

    // Forget the clock and the grains: the next snapshot starts a new run.
    void reset();

private:
    struct Grain {
        int length = 0;
        int pos = 0;  // == length when done
    };

    void startGrain();
    int64_t spanFrames(int frames, int rate, int64_t nowNs, int64_t maxSpan);

    // The newest snapshot, interleaved at channels_.
    std::vector<float> snap_;
    int frames_ = 0;
    int channels_ = 0;
    int rate_ = 0;
    // Two grains overlap at any frame: each holds its own windowed copy, so a new snapshot never changes a
    // grain already sounding.
    std::array<std::vector<float>, 2> grainData_;
    std::array<Grain, 2> grains_{};
    int nextSlot_ = 0;
    int untilGrain_ = 0;  // frames until the next grain starts
    uint32_t random_ = 0x9E3779B9u;
    // The capture clock: frames written since the run's first snapshot (at anchorNs_), so a span is the wall
    // time since then, less what is already written.
    bool anchored_ = false;
    int64_t anchorNs_ = 0;
    int64_t lastNs_ = 0;
    int64_t sinceAnchor_ = 0;
    std::vector<float> out_;  // one chunk on its way to the ring
};

}  // namespace geode::audio::ingest
