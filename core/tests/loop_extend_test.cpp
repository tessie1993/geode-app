// Host-side checks for what a loop extend decides while it copies encoded samples (core/output/LoopExtend), and
// its C ABI. Each case pins the Kotlin it replaces (export/LoopExtend.kt: LoopReader.copyInto, AudioReel.writeUpTo,
// the clip start sums, the progress bar and the sample buffer size). Built and run by CI
// (.github/workflows/native-host-tests.yml, "Native export planning tests").
#include <cstdint>
#include <cstdio>

#include "api/geode_api.h"
#include "output/LoopExtend.hpp"

using namespace geode::output::loopext;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// A loop of ten frames, one every 100 ms, a keyframe on the first and fifth.
const int64_t kTimes[10] = {0, 100'000, 200'000, 300'000, 400'000, 500'000, 600'000, 700'000, 800'000, 900'000};
const int kFlags[10] = {1, 0, 0, 0, 1, 0, 0, 0, 0, 0};

void flags() {
    check(muxerFlags(1) == kMuxerKeyFrameFlag, "a sync sample is a key frame");
    check(muxerFlags(0) == 0, "a plain sample has no flags");
    check(muxerFlags(1 | 4) == kMuxerKeyFrameFlag, "other extractor flags are dropped");
    check(muxerFlags(4) == 0, "no sync bit, no key frame");
}

void repeats() {
    int64_t pts[10];
    int out[10];
    int n = repeatSamples(kTimes, kFlags, 10, 0, 10'000'000, pts, out);
    check(n == 10 && pts[0] == 0 && pts[9] == 900'000, "the first repeat writes every sample unshifted");
    check(out[0] == kMuxerKeyFrameFlag && out[1] == 0 && out[4] == kMuxerKeyFrameFlag, "flags follow the samples");
    n = repeatSamples(kTimes, kFlags, 10, 5'000'000, 10'000'000, pts, out);
    check(n == 10 && pts[0] == 5'000'000 && pts[9] == 5'900'000, "a later repeat shifts by its offset");
    // A repeat that runs past the end stops at the first sample at or past the limit.
    n = repeatSamples(kTimes, kFlags, 10, 9'700'000, 10'000'000, pts, out);
    check(n == 3 && pts[2] == 9'900'000, "the limit cuts the last repeat");
    n = repeatSamples(kTimes, kFlags, 10, 9'700'000, 9'800'000, pts, out);
    check(n == 1, "a sample exactly at the limit is not written");
    check(repeatSamples(kTimes, kFlags, 10, 10'000'000, 10'000'000, pts, out) == 0, "an offset at the limit writes nothing");
    check(repeatSamples(kTimes, kFlags, 0, 0, 10'000'000, pts, out) == 0, "no samples, none written");
    check(repeatSamples(nullptr, kFlags, 10, 0, 10'000'000, pts, out) == 0, "NULL times");
    check(geode_loop_repeat_samples(kTimes, kFlags, 10, 5'000'000, 10'000'000, pts, out) == 10 && pts[4] == 5'400'000,
          "ABI: repeat samples");
}

void starts() {
    const int64_t durations[3] = {3'000'000, 2'000'000, 4'500'000};
    int64_t out[4] = {};
    clipStarts(durations, 3, out);
    check(out[0] == 0 && out[1] == 3'000'000 && out[2] == 5'000'000 && out[3] == 9'500'000,
          "clips start back to back, then the total");
    int64_t none[1] = {-1};
    clipStarts(nullptr, 0, none);
    check(none[0] == 0, "no clips total zero");
    int64_t abi[4] = {};
    geode_loop_clip_starts(durations, 3, abi);
    check(abi[3] == 9'500'000, "ABI: clip starts");
}

// Two clips: three samples a second apart, then two; the second starts at 3 s.
const int64_t kClipStarts[2] = {0, 3'000'000};
const int kCounts[2] = {3, 2};
const int64_t kPts[5] = {0, 1'000'000, 2'000'000, 0, 1'000'000};

void audio() {
    int clip[8];
    int sample[8];
    int64_t at[8];
    AudioCursor c;
    int n = audioDue(kClipStarts, kCounts, 2, kPts, c, 1'500'000, clip, sample, at, 8);
    check(n == 2 && clip[0] == 0 && sample[0] == 0 && clip[1] == 0 && sample[1] == 1, "samples before the horizon are due");
    check(c.clip == 0 && c.sample == 2, "the cursor stops at the first sample not yet due");
    n = audioDue(kClipStarts, kCounts, 2, kPts, c, 1'500'000, clip, sample, at, 8);
    check(n == 0, "nothing more is due at the same horizon");
    n = audioDue(kClipStarts, kCounts, 2, kPts, c, 3'500'000, clip, sample, at, 8);
    check(n == 2 && clip[0] == 0 && sample[0] == 2 && clip[1] == 1 && sample[1] == 0, "the feed crosses into the next clip");
    check(at[0] == 2000000 && at[1] == 3000000, "each sample time counts its clip start");
    check(c.clip == 1 && c.sample == 1, "and the cursor sits in it");
    n = audioDue(kClipStarts, kCounts, 2, kPts, c, INT64_MAX, clip, sample, at, 8);
    check(n == 1 && clip[0] == 1 && sample[0] == 1, "an open horizon drains the rest");
    n = audioDue(kClipStarts, kCounts, 2, kPts, c, INT64_MAX, clip, sample, at, 8);
    check(n == 0 && c.clip == 2, "after the last clip nothing is due");

    // A limited capacity returns what fits and resumes where it left off.
    AudioCursor d;
    n = audioDue(kClipStarts, kCounts, 2, kPts, d, INT64_MAX, clip, sample, at, 2);
    check(n == 2 && d.clip == 0 && d.sample == 2, "capacity bounds one call");
    n = audioDue(kClipStarts, kCounts, 2, kPts, d, INT64_MAX, clip, sample, at, 8);
    check(n == 3 && clip[0] == 0 && sample[0] == 2 && clip[2] == 1 && sample[2] == 1, "the next call resumes");

    // A cursor handed in mid-way finds its place in the flat times.
    AudioCursor e{1, 0};
    n = audioDue(kClipStarts, kCounts, 2, kPts, e, 3'500'000, clip, sample, at, 8);
    check(n == 1 && clip[0] == 1 && sample[0] == 0, "a cursor in a later clip reads that clip's times");

    // A clip with no samples is skipped.
    const int64_t starts3[3] = {0, 1'000'000, 1'000'000};
    const int counts3[3] = {1, 0, 1};
    const int64_t pts3[2] = {0, 0};
    AudioCursor f;
    n = audioDue(starts3, counts3, 3, pts3, f, INT64_MAX, clip, sample, at, 8);
    check(n == 2 && clip[1] == 2 && sample[1] == 0, "an empty clip is stepped over");

    int cursor[2] = {0, 0};
    check(geode_loop_audio_due(kClipStarts, kCounts, 2, kPts, cursor, 1'500'000, clip, sample, at, 8) == 2 && cursor[1] == 2,
          "ABI: audio due advances the cursor");
    check(geode_loop_audio_due(kClipStarts, kCounts, 2, kPts, nullptr, 1'500'000, clip, sample, at, 8) == 0, "ABI: NULL cursor");
}

bool near(float a, float b) { return a - b < 1e-5f && b - a < 1e-5f; }

void progress() {
    check(near(copyProgress(0, 4), kAudioProgressSpan), "the copy starts where the transcode ends");
    check(near(copyProgress(4, 4), 1.0f), "and finishes at one");
    check(near(copyProgress(2, 4), kAudioProgressSpan + (1.0f - kAudioProgressSpan) * 0.5f), "halfway through the repeats");
    check(near(copyProgress(1, 0), 1.0f), "no repeats is done");
    check(near(transcodeProgress(0, 0.0f, 2), 0.0f), "the transcode starts at zero");
    check(near(transcodeProgress(1, 0.5f, 2), kAudioProgressSpan * 0.75f), "half of the second of two clips");
    check(near(transcodeProgress(1, 1.0f, 2), kAudioProgressSpan), "the transcode ends at its span");
    check(near(transcodeProgress(0, 0.5f, 0), 0.0f), "no clips, no progress");
    check(near(geode_loop_copy_progress(2, 4), copyProgress(2, 4)), "ABI: copy progress");
    check(near(geode_loop_transcode_progress(1, 0.5f, 2), transcodeProgress(1, 0.5f, 2)), "ABI: transcode progress");
}

void buffer() {
    check(sampleBufferBytes(0, 640, 360) == 1024 * 1024, "a small format gets the floor");
    check(sampleBufferBytes(0, 1920, 1080) == 1920 * 1080, "a big format gets its pixel count");
    check(sampleBufferBytes(5'000'000, 1920, 1080) == 5'000'000, "a declared maximum above both wins");
    check(sampleBufferBytes(0, 100'000, 100'000) == INT32_MAX, "an absurd size clamps instead of overflowing");
    check(sampleBufferBytes(0, -1, 5) == 1024 * 1024, "negative sizes count as zero");
    check(geode_loop_sample_buffer_bytes(0, 1920, 1080) == 1920 * 1080, "ABI: sample buffer");
}

}  // namespace

int main() {
    flags();
    repeats();
    starts();
    audio();
    progress();
    buffer();
    if (failures) {
        std::printf("loop_extend_test: %d failed\n", failures);
        return 1;
    }
    std::printf("loop_extend_test: all passed\n");
    return 0;
}
