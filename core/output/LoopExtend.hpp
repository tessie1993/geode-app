#pragma once
#include <cstdint>

namespace geode::output::loopext {

// What a long-form loop extend decides while it copies already-encoded samples into one file (the repeat plan
// itself is output/LoopPlan). The shell keeps only the MediaExtractor reads and MediaMuxer writes; every
// decision, timestamp and progress figure here is host-tested (core/tests/loop_extend_test.cpp).

// The two sample flags that mean "a keyframe": MediaExtractor.SAMPLE_FLAG_SYNC in, MediaCodec.BUFFER_FLAG_KEY_FRAME out.
constexpr int kExtractorSyncFlag = 1;
constexpr int kMuxerKeyFrameFlag = 1;

// The share of the progress bar the soundtrack's transcode takes; the copy takes the rest.
constexpr float kAudioProgressSpan = 0.35f;

// The flags a sample is written with: key when the extractor called it a sync sample, else none.
constexpr int muxerFlags(int extractorFlags) { return (extractorFlags & kExtractorSyncFlag) != 0 ? kMuxerKeyFrameFlag : 0; }

// One repeat of one loop. The loop's samples (their times in the loop, and their extractor flags) are written
// in order, each shifted by offsetUs, up to the first one that would land at or past limitUs. Fills outPtsUs and
// outFlags for those samples and returns how many; the loop was encoded with a keyframe first and no B-frames,
// so a constant shift is all a copy needs.
int repeatSamples(const int64_t* sampleTimesUs, const int* extractorFlags, int count, int64_t offsetUs, int64_t limitUs,
                  int64_t* outPtsUs, int* outFlags);

// Where the soundtrack's clips begin: back to back, so each starts where the one before ends. Writes
// durationsUs.size() starts and, after them, the total, so out holds clipCount + 1 values.
void clipStarts(const int64_t* durationsUs, int clipCount, int64_t* out);

// Where the audio feed stands: the clip it is in and the next sample of that clip.
struct AudioCursor {
    int clip = 0;
    int sample = 0;
};

// The soundtrack samples due before untilUs, in order, from the cursor, which it advances. clipStartsUs and
// sampleCounts have one entry per clip; samplePtsUs holds every clip's sample times (relative to its clip)
// one clip after the other. Writes (clip, sample) and the time in the whole soundtrack (the clip's start plus the
// sample's) of up to capacity due samples and returns how many; fewer
// than capacity means the feed has caught up to untilUs (or run out of clips).
int audioDue(const int64_t* clipStartsUs, const int* sampleCounts, int clipCount, const int64_t* samplePtsUs,
             AudioCursor& cursor, int64_t untilUs, int* outClip, int* outSample, int64_t* outPtsUs, int capacity);

// How far a long file has got: the copy of repeat `done` of `repeats`, after the transcode's share; and the
// transcode of clip `index` (progress 0..1 within it) of `clipCount`.
float copyProgress(int done, int repeats);
float transcodeProgress(int index, float clipProgress, int clipCount);

// The bytes one sample read can need: the largest of the format's declared maximum, its pixel count and a floor.
int sampleBufferBytes(int declaredBytes, int width, int height);

}  // namespace geode::output::loopext
