#include "output/LoopExtend.hpp"

#include <algorithm>
#include <climits>

namespace geode::output::loopext {

namespace {
// Big enough for the loop's opening IDR, which is by far its largest sample.
constexpr int64_t kMinSampleBufferBytes = 1024 * 1024;
}  // namespace

int repeatSamples(const int64_t* sampleTimesUs, const int* extractorFlags, int count, int64_t offsetUs, int64_t limitUs,
                  int64_t* outPtsUs, int* outFlags) {
    if (!sampleTimesUs || !extractorFlags || !outPtsUs || !outFlags) return 0;
    int written = 0;
    while (written < count && offsetUs + sampleTimesUs[written] < limitUs) {
        outPtsUs[written] = offsetUs + sampleTimesUs[written];
        outFlags[written] = muxerFlags(extractorFlags[written]);
        ++written;
    }
    return written;
}

void clipStarts(const int64_t* durationsUs, int clipCount, int64_t* out) {
    if (!out) return;
    int64_t start = 0;
    for (int i = 0; i < clipCount; ++i) {
        out[i] = start;
        start += durationsUs ? durationsUs[i] : 0;
    }
    out[std::max(clipCount, 0)] = start;
}

int audioDue(const int64_t* clipStartsUs, const int* sampleCounts, int clipCount, const int64_t* samplePtsUs,
             AudioCursor& cursor, int64_t untilUs, int* outClip, int* outSample, int64_t* outPtsUs, int capacity) {
    if (!clipStartsUs || !sampleCounts || !samplePtsUs || !outClip || !outSample || !outPtsUs) return 0;
    int64_t base = 0;  // index in samplePtsUs of the cursor's clip's first sample
    for (int c = 0; c < cursor.clip && c < clipCount; ++c) base += sampleCounts[c];
    int emitted = 0;
    while (cursor.clip < clipCount && emitted < capacity) {
        if (cursor.sample >= sampleCounts[cursor.clip]) {
            base += sampleCounts[cursor.clip];
            ++cursor.clip;
            cursor.sample = 0;
            continue;
        }
        const int64_t at = clipStartsUs[cursor.clip] + samplePtsUs[base + cursor.sample];
        if (at >= untilUs) break;
        outClip[emitted] = cursor.clip;
        outSample[emitted] = cursor.sample;
        outPtsUs[emitted] = at;
        ++emitted;
        ++cursor.sample;
    }
    return emitted;
}

float copyProgress(int done, int repeats) {
    if (repeats <= 0) return 1.0f;
    return kAudioProgressSpan + (1.0f - kAudioProgressSpan) * static_cast<float>(done) / static_cast<float>(repeats);
}

float transcodeProgress(int index, float clipProgress, int clipCount) {
    if (clipCount <= 0) return 0.0f;
    return kAudioProgressSpan * (static_cast<float>(index) + clipProgress) / static_cast<float>(clipCount);
}

int sampleBufferBytes(int declaredBytes, int width, int height) {
    const int64_t pixels = static_cast<int64_t>(std::max(width, 0)) * std::max(height, 0);
    const int64_t bytes = std::max({static_cast<int64_t>(declaredBytes), pixels, kMinSampleBufferBytes});
    return static_cast<int>(std::min<int64_t>(bytes, INT_MAX));
}

}  // namespace geode::output::loopext
