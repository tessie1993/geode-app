// Host-side checks for the player tap's flush judge (core/audio/ingest/FlushJudge, #358, #487). Built and run
// by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no device.
//
// What is pinned: a flush Media3 makes to re-apply speed, pitch or skip-silence, with the media position where
// the written samples put the stream, keeps the epoch, so a reader's cursor and its sound engine keep what they
// learned, even across a format change; a seek (a sink reset), another playlist item or a real position jump
// begins a new epoch and restarts them. The judge's cases run against a small stand-in ring and reader (a
// sound engine's learned seconds); then the same story runs through the real thing: geode_ingest_flush on a
// core/audio/ingest Ring, read by a native Cursor, whose restarted flag is what resets a sound engine.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "api/geode_api.h"
#include "audio/ingest/Cursor.hpp"
#include "audio/ingest/FlushJudge.hpp"

using geode::audio::ingest::Cursor;
using geode::audio::ingest::FlushFacts;
using geode::audio::ingest::FlushJudge;
using geode::audio::ingest::FlushVerdict;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// The writer side as the tap drives it: a sample count, an epoch, a format.
struct Ring {
    long long written = 0;
    int epoch = 0;
    int rate = 0;
    int channels = 0;
    FlushJudge judge;

    void write(long long frames) { written += frames; }

    FlushVerdict flush(const FlushFacts& f) {
        const FlushVerdict v = judge.judge(f, written);
        if (v == FlushVerdict::BeginEpoch) ++epoch;
        rate = f.sampleRate;
        channels = f.channels;
        return v;
    }
};

// A reader: its cursor, and the state its sound engine has learned (seconds heard). A new epoch restarts both,
// as the cursor's restart flag resets the engine (SoundEngine::reset).
struct Reader {
    int epoch = 0;
    long long cursor = 0;
    double learnedSeconds = 0.0;
    int restarts = 0;

    void read(const Ring& ring) {
        if (ring.epoch != epoch) {
            epoch = ring.epoch;
            learnedSeconds = 0.0;
            ++restarts;
        }
        if (ring.rate > 0) learnedSeconds += static_cast<double>(ring.written - cursor) / ring.rate;
        cursor = ring.written;
    }
};

constexpr int64_t kSong = 11;
constexpr int64_t kOther = 22;

FlushFacts reset(int rate, int64_t period) {
    FlushFacts f;
    f.sampleRate = rate;
    f.channels = 2;
    f.reapplied = false;
    f.positionUs = 0;   // Media3 reports no position for a sink reset
    f.periodId = period;
    return f;
}

FlushFacts reapply(int rate, int64_t positionUs, int64_t period, int channels = 2) {
    FlushFacts f;
    f.sampleRate = rate;
    f.channels = channels;
    f.reapplied = true;
    f.positionUs = positionUs;
    f.periodId = period;
    return f;
}

// Start a song at 0: the reset Media3 makes when the item starts, then its re-apply on the first buffer.
void startSong(Ring& ring, Reader& reader, int rate, int64_t period) {
    ring.flush(reset(rate, period));
    ring.flush(reapply(rate, 0, period));
    reader.read(ring);
}

void speedChangeKeepsTheCursorAndEngine() {
    Ring ring;
    Reader reader;
    startSong(ring, reader, 48000, kSong);
    check(reader.restarts == 1, "a song's start is one epoch, not one per flush");
    ring.write(48000 * 20);   // 20 s of source samples (the tap is first: source time, whatever the speed)
    reader.read(ring);
    const long long cursor = reader.cursor;

    // Speed 1.00 -> 1.05: Media3 drains, re-applies, flushes at the next buffer's position.
    check(ring.flush(reapply(48000, 20'000'000, kSong)) == FlushVerdict::SetFormat, "a speed change keeps the epoch");
    reader.read(ring);
    check(reader.restarts == 1, "the reader does not restart on a speed change");
    check(reader.cursor == cursor, "its cursor stays where it was");
    check(reader.learnedSeconds > 19.9, "its engine keeps the 20 s it heard");

    // Dragging the slider: a re-apply per step, each at the position the samples reached.
    for (int step = 1; step <= 10; ++step) {
        ring.write(4800);   // 100 ms between steps
        check(ring.flush(reapply(48000, 20'000'000 + step * 100'000, kSong)) == FlushVerdict::SetFormat,
              "each slider step keeps the epoch");
        reader.read(ring);
    }
    check(reader.restarts == 1, "a dragged slider never restarts the engine");
    check(reader.learnedSeconds > 20.9, "and the engine keeps learning through it");
}

void formatChangeWithContinuousPositionKeeps() {
    Ring ring;
    Reader reader;
    startSong(ring, reader, 44100, kSong);
    ring.write(44100 * 5);
    reader.read(ring);
    // The same item, a new format at the position the samples reached: set_format, not begin_epoch.
    check(ring.flush(reapply(48000, 5'000'000, kSong, 1)) == FlushVerdict::SetFormat,
          "a format change at a continuous position keeps the epoch");
    check(ring.rate == 48000 && ring.channels == 1, "with the new format");
    ring.write(48000 * 3);
    check(ring.flush(reapply(48000, 8'000'000, kSong, 1)) == FlushVerdict::SetFormat,
          "and the position is measured at the new rate after it");
    reader.read(ring);
    check(reader.restarts == 1, "the reader never restarted");
}

void skipSilenceAndResyncKeep() {
    Ring ring;
    Reader reader;
    startSong(ring, reader, 48000, kSong);
    ring.write(48000 * 10);
    // Skip silence on: the samples still reach the tap (SilenceSkipping runs after it), so the position holds.
    check(ring.flush(reapply(48000, 10'000'000, kSong)) == FlushVerdict::SetFormat, "skip-silence on keeps the epoch");
    ring.write(48000 * 10);
    // Media3's > 200 ms timestamp resync re-applies over contiguous samples: kept.
    check(ring.flush(reapply(48000, 20'300'000, kSong)) == FlushVerdict::SetFormat, "a 300 ms resync keeps the epoch");
    reader.read(ring);
    check(reader.restarts == 1, "neither restarts the engine");
}

void seekRestarts() {
    Ring ring;
    Reader reader;
    startSong(ring, reader, 48000, kSong);
    ring.write(48000 * 30);
    reader.read(ring);
    check(ring.flush(reset(48000, kSong)) == FlushVerdict::BeginEpoch, "a seek (sink reset) begins an epoch");
    // Media3 re-applies on the first buffer after the reset, at the seek target: not a second restart.
    check(ring.flush(reapply(48000, 90'000'000, kSong)) == FlushVerdict::SetFormat,
          "the re-apply right after a seek keeps the new epoch");
    reader.read(ring);
    check(reader.restarts == 2, "the reader restarted once for the seek");
    check(reader.learnedSeconds == 0.0, "its engine starts fresh on the new audio");
    // A speed change after the seek is measured from the seek target, so it keeps the epoch.
    ring.write(48000 * 4);
    check(ring.flush(reapply(48000, 94'000'000, kSong)) == FlushVerdict::SetFormat,
          "a speed change after a seek keeps the epoch");
}

void trackChangeAndJumpsRestart() {
    Ring ring;
    Reader reader;
    startSong(ring, reader, 48000, kSong);
    ring.write(48000 * 200);
    // Gapless: the next item behind a reused AudioTrack, re-applied in another period.
    check(ring.flush(reapply(48000, 200'000'000, kOther)) == FlushVerdict::BeginEpoch,
          "another playlist item begins an epoch");
    reader.read(ring);
    ring.write(48000 * 5);
    // A re-apply whose position is far from where the samples put the stream is a real jump.
    check(ring.flush(reapply(48000, 60'000'000, kOther)) == FlushVerdict::BeginEpoch,
          "a position jump past the tolerance begins an epoch");
    reader.read(ring);
    ring.write(48000);
    // An unknown period (0) never counts as another item.
    check(ring.flush(reapply(48000, 61'000'000, 0)) == FlushVerdict::SetFormat, "an unknown period is not a change");
    check(ring.flush(reapply(48000, 61'000'000, kOther)) == FlushVerdict::SetFormat,
          "and the last known period still holds after it");
    reader.read(ring);
    check(reader.restarts == 3, "the reader restarted once per real discontinuity");
}

void resetForgets() {
    FlushJudge judge;
    check(judge.judge(reapply(48000, 0, kSong), 0) == FlushVerdict::BeginEpoch, "the first flush ever begins an epoch");
    judge.reset();
    check(judge.judge(reapply(48000, 0, kSong), 0) == FlushVerdict::BeginEpoch, "and so does the first after reset()");
}

// The real ring: writes s16 stereo frames, and a reader that drains its cursor and counts restarts.
void writeFrames(geode_ingest* ing, int frames) {
    static const std::vector<int16_t> zeros(2 * 4096, 0);
    while (frames > 0) {
        const int n = frames < 4096 ? frames : 4096;
        geode_ingest_write(ing, zeros.data(), static_cast<size_t>(n), GEODE_PCM_S16);
        frames -= n;
    }
}

struct Drain {
    int restarts = 0;
    int64_t dropped = 0;
    int lastRate = 0;
    void from(Cursor& cursor) {
        std::vector<float> out(2 * 4096);
        for (Cursor::Block b = cursor.read(out.data(), 4096); b.frames > 0; b = cursor.read(out.data(), 4096)) {
            if (b.restarted) ++restarts;
            dropped += b.dropped;
            lastRate = b.sampleRate;
        }
    }
};

void realRingKeepsTheCursorAcrossSpeedAndFormat() {
    geode_ingest* ing = geode_ingest_create(1 << 18, 2);
    check(ing != nullptr, "ring created");
    Cursor cursor(ing->ring, Cursor::Start::Oldest);
    Drain reader;

    check(geode_ingest_flush(ing, 48000, 2, 0, 0, 0, kSong) == 1, "the song's first flush begins an epoch");
    check(geode_ingest_flush(ing, 48000, 2, 0, 1, 0, kSong) == 0, "its first-buffer re-apply does not");
    writeFrames(ing, 48000 * 2);
    reader.from(cursor);
    check(reader.restarts == 1, "the reader starts once");

    // Speed 1.0 -> 1.25 at 2 s, then a new format for the same item at 2.1 s: both continuous.
    check(geode_ingest_flush(ing, 48000, 2, 0, 1, 2'000'000, kSong) == 0, "a speed change keeps the epoch");
    writeFrames(ing, 4800);
    check(geode_ingest_flush(ing, 44100, 2, 0, 1, 2'100'000, kSong) == 0, "a format change at a continuous position too");
    writeFrames(ing, 44100);
    reader.from(cursor);
    check(reader.restarts == 1, "the cursor never restarts, so the sound engine keeps its state");
    check(reader.dropped == 0, "and loses no sample");
    check(cursor.position() == geode_ingest_written(ing), "it is level with the writer");
    check(reader.lastRate == 44100, "each block carries the rate it was written at");

    // A seek, then the next item behind a reused AudioTrack: each restarts the reader once.
    check(geode_ingest_flush(ing, 44100, 2, 0, 0, 0, kSong) == 1, "a seek begins an epoch");
    check(geode_ingest_flush(ing, 44100, 2, 0, 1, 60'000'000, kSong) == 0, "its first-buffer re-apply does not");
    writeFrames(ing, 4410);
    reader.from(cursor);
    check(reader.restarts == 2, "the seek restarts the reader");
    check(geode_ingest_flush(ing, 44100, 2, 0, 1, 60'100'000, kOther) == 1, "another item begins an epoch");
    writeFrames(ing, 4410);
    reader.from(cursor);
    check(reader.restarts == 3, "the track change restarts the reader");
    check(geode_ingest_flush(nullptr, 44100, 2, 0, 0, 0, 0) == 0, "a null ring is ignored");
    geode_ingest_destroy(ing);
}

}  // namespace

int main() {
    speedChangeKeepsTheCursorAndEngine();
    formatChangeWithContinuousPositionKeeps();
    skipSilenceAndResyncKeep();
    seekRestarts();
    trackChangeAndJumpsRestart();
    resetForgets();
    realRingKeepsTheCursorAcrossSpeedAndFormat();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ingest_flush_test: ok\n");
    return 0;
}
