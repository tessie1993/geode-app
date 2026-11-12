#pragma once
#include <cstdint>

namespace geode::output {

// The offline render's frame clock (ADR 0006, core/output): which instant frame n shows, the timestamp
// the encoder stamps on it, and how many source samples the sound engine has heard by its end.
//
// Every value is computed from n directly, never accumulated, so a long render does not drift: a 24 fps
// frame lasts 41 666 666.67 ns, and stamping n * 41 666 666 (the truncated step the Kotlin exporter
// multiplied by) loses 2/3 ns a frame, 6 ms an hour. Here pts(n) = n * 1e9 / fps in one division.
//
// Pure integer maths, no state beyond the rate pair: any thread, host-testable (core/tests/output_test).
class FrameClock {
public:
    FrameClock(int fps, int sampleRateHz);

    int fps() const { return fps_; }
    int sampleRateHz() const { return rate_; }

    // Frames a render of durationUs draws: floor(durationUs * fps / 1e6), at least 1. The Kotlin
    // VideoExporter's `totalFrames`, unchanged.
    int64_t framesFor(int64_t durationUs) const;

    // The encoder timestamp of frame n, floor(n * 1e9 / fps) nanoseconds.
    int64_t ptsNs(int64_t n) const;

    // Programme time frame n shows, floor(n * 1e6 / fps) microseconds.
    int64_t frameStartUs(int64_t n) const;

    // The same instant in whole milliseconds, floor(n * 1000 / fps): what the export's watermark and its
    // audio interleave read (VideoExporter); the picture draws at ptsNs (output/ExportFrame).
    int64_t frameTimeMs(int64_t n) const;

    // Source samples delivered once frame n is done: llround((n + 1) * rate / fps). Feeding the engine up
    // to here before drawing frame n makes an export hear exactly what the screen would (core/output README).
    int64_t samplesThrough(int64_t n) const;

    // The fixed step every frame advances the engine by, 1 / fps seconds.
    double dtSeconds() const { return 1.0 / fps_; }

private:
    int fps_;
    int rate_;
};

}  // namespace geode::output
