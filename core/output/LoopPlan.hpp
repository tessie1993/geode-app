#pragma once
#include <cstdint>
#include <vector>

namespace geode::output {

// A long-form render's loop (ADR 0006, core/output; fosfora's headless/loop_spec.rs): how long one loop
// runs, how long its seam dissolves, at what rate it is drawn, and how the encoded loop is repeated to
// fill the running time. Ported from the Kotlin LoopSpec (export/LoopRender.kt) and LoopExtendPlan
// (export/LoopExtend.kt), which keep only the MediaCodec and MediaMuxer work once they call this.
struct LoopSpec {
    static constexpr int64_t kMinLoopMs = 30'000;
    static constexpr int64_t kMaxLoopMs = 300'000;
    static constexpr int64_t kMinCrossfadeMs = 200;
    static constexpr int64_t kMaxCrossfadeMs = 5'000;
    static constexpr int64_t kDefaultCrossfadeMs = 1'000;
    static constexpr int kMinFps = 24;
    static constexpr int kMaxFps = 60;
    static constexpr int64_t kSeamPreviewLeadMs = 2'000;
    // The seam stash keeps one full-resolution RGBA frame per crossfade frame (LoopRender.SeamStash).
    static constexpr int64_t kSeamStashBudgetBytes = 192LL * 1024 * 1024;
    static constexpr int64_t kBytesPerPixel = 4;

    int64_t loopMs = 0;
    int64_t crossfadeMs = 0;
    int fps = 0;

    // Loose UI numbers clamped into a spec that is always valid: 30 s..5 min, a dissolve of 200 ms..5 s and
    // never more than half the loop, 24..60 fps.
    static LoopSpec of(int64_t loopMs, int64_t crossfadeMs = kDefaultCrossfadeMs, int fps = kMaxFps);

    // The Kotlin constructor's checks: positive lengths, a dissolve at most half the loop, fps in range.
    bool valid() const;

    // Frames the viewer sees once per repeat, at least 2.
    int loopFrames() const;
    // Frames the dissolve spans: at least 1, at most half the loop.
    int crossfadeFrames() const;
    // Frames drawn: the loop plus the tail its opening dissolves out of.
    int sourceFrames() const { return loopFrames() + crossfadeFrames(); }
    int64_t loopDurationUs() const;
    // Where a preview starts to land on the seam: a lead before the dissolve, never before 0.
    int64_t seamPreviewStartMs(int64_t leadMs = kSeamPreviewLeadMs) const;

    // The dissolve shortened to what the seam stash can hold at width x height. The stash affords
    // `budget / (w * h * 4)` frames; the shortened dissolve is the shortest whole-ms length whose
    // crossfadeFrames() is exactly that many. The Kotlin version truncated ms then frames, losing one frame
    // (7 affordable frames at 60 fps gave 116 ms, which is 6 frames). It may fall under kMinCrossfadeMs:
    // the stash cannot hold more, and a shorter dissolve beats a render that runs out of memory.
    LoopSpec budgeted(int width, int height) const;
};

// The weight of the stashed opening frame blended over closing frame `tail` of a seam `seamFrames` long:
// exactly 0 on the first closing frame and 1 on the last, so the frames either side of the wrap are
// untouched renders.
float seamWeight(int tail, int seamFrames);

// One run of repeats of one rendered loop (a time-of-day drift stop), starting at startUs.
struct LoopSegment {
    int loopIndex = 0;
    int64_t startUs = 0;
    int repeats = 0;
};

// One copy of a loop into the long file: which loop, shifted by how much.
struct LoopWrite {
    int loopIndex = 0;
    int64_t offsetUs = 0;
};

// How encoded loops fill the running time: ceil(total / loop) repeats, spread over `loops` stops as evenly
// as whole repeats allow, earlier stops taking the remainder. A stop that would get no repeats is dropped.
class LoopExtendPlan {
public:
    static constexpr int64_t kSampleTableHeadroomDivisor = 50;

    // loopDurationUs and loops are taken as at least 1; total as at least 1 µs.
    static LoopExtendPlan of(int64_t loopDurationUs, int64_t totalDurationUs, int loops);

    int64_t loopDurationUs() const { return loopUs_; }
    int64_t totalDurationUs() const { return totalUs_; }
    const std::vector<LoopSegment>& segments() const { return segments_; }
    const std::vector<LoopWrite>& writes() const { return writes_; }
    int repeats() const { return static_cast<int>(writes_.size()); }

    // A seam preview `leadMs` before the wrap at seamMs, inside the file.
    int64_t seamPreviewStartMs(int64_t seamMs, int64_t leadMs = LoopSpec::kSeamPreviewLeadMs) const;
    // What the file will weigh: every repeat copies the loop's samples again, plus the sample tables (2 %).
    int64_t estimatedBytes(int64_t loopBytes, int64_t soundtrackBytes) const;
    // The video bit rate that keeps a file this long under limitBytes.
    int fittingBitRate(int64_t limitBytes, int64_t soundtrackBytes) const;

private:
    int64_t loopUs_ = 1;
    int64_t totalUs_ = 1;
    std::vector<LoopSegment> segments_;
    std::vector<LoopWrite> writes_;
};

}  // namespace geode::output
