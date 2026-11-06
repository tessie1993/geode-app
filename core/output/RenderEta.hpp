#pragma once
#include <cstdint>

namespace geode::output {

// Time left for the running export, from progress samples (#422; ported from the Kotlin RenderEta).
//
// The bar is not one steady job: the AAC transcode races through its share of it, then the GPU render
// crawls through the rest. A sliding-window rate carried that early sprint into the render, so the estimate
// started near "about a minute", climbed to the real figure as the window caught up, and only then counted
// down (QA #24). So the caller says which samples are the steady render, and only those count:
//
// - Nothing is shown until the render has run for warmupMs (shader compile and encoder start are a slow
//   first frame or two), then for minSpanMs more with at least minProgress gained.
// - The rate is the mean since that baseline, not a recent window, so it settles rather than swings as one
//   heavy passage of the scene comes and goes.
// - The shown value counts down by wall time between samples and eases toward the fresh figure with time
//   constant smoothingMs, so it never jumps on a single sample.
//
// A non-steady sample, or progress moving backwards (a new run, an encoder retry), starts over.
//
// The state is a plain value the caller keeps (ExportRun holds it as the ABI's geode_render_eta), and a
// value-initialised state is a fresh estimate, so starting over needs no call into here. Float maths as the
// Kotlin had it, so the numbers did not move. Host-tested by core/tests/output_test.
struct RenderEtaState {
    bool steady = false;
    int64_t steadySinceMs = 0;
    bool based = false;
    int64_t baseMs = 0;
    float baseProgress = 0.0f;
    float latestProgress = 0.0f;
    bool shown = false;
    float shownSeconds = 0.0f;
    int64_t shownAtMs = 0;
};

struct RenderEtaTuning {
    int64_t warmupMs = 4'000;
    int64_t minSpanMs = 6'000;
    float minProgress = 0.01f;
    int64_t smoothingMs = 8'000;
};

// Seconds left, or -1 while there is not yet enough steady progress to say. progress is clamped to 0..1;
// 1 answers 0.
int64_t renderEtaSample(RenderEtaState& state, float progress, int64_t atMs, bool steady,
                        const RenderEtaTuning& tuning = {});

// How a number of seconds left reads: which phrase, and the count it carries (minutes or hours, rounded to
// the nearest). The words stay Kotlin (RenderEta.describe); the buckets are here.
enum class EtaPhrase : int { AlmostDone = 0, AboutAMinute = 1, Minutes = 2, Hours = 3 };
EtaPhrase renderEtaPhrase(int64_t secondsLeft, int64_t* count);

}  // namespace geode::output
