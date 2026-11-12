#pragma once
#include <cstdint>

namespace geode::output {

// Time left for the running export, from progress samples (core/output; ADR 0003).
//
// The bar is not one steady job: the AAC transcode races through its share of it, then the GPU render
// crawls through the rest. A rate taken over the whole bar carries that early sprint into the render, so
// the estimate starts near "about a minute", climbs to the real figure and only then counts down. The
// caller says which samples are the steady render, and only those count:
//
// - Nothing is shown until the render has run for kWarmupMs (shader compile and encoder start are a slow
//   first frame or two), then for kMinSpanMs more with at least kMinProgress gained.
// - The rate is the mean since that baseline, not a recent window, so it settles rather than swings as one
//   heavy passage of the scene comes and goes.
// - The shown value counts down by wall time between samples and eases toward the fresh figure with time
//   constant kSmoothingMs, so it never jumps on a single sample.
//
// A sample that is not steady, or progress moving backwards (a new run, an encoder retry), starts over.
// One thread at a time; the C ABI's handle adds the lock.
class RenderEta {
public:
    // sample(): there is not yet enough steady progress to say.
    static constexpr int64_t kUnknown = -1;
    static constexpr int64_t kWarmupMs = 4'000;
    static constexpr int64_t kMinSpanMs = 6'000;
    static constexpr float kMinProgress = 0.01f;
    static constexpr int64_t kSmoothingMs = 8'000;

    // Whole seconds left after `progress` (0..1; anything else is clamped, not-a-number is 0) at atMs on a
    // monotonic clock, or kUnknown.
    int64_t sample(float progress, int64_t atMs, bool steady);
    void reset();

private:
    static constexpr int64_t kNone = -1;

    int64_t steadySinceMs_ = kNone;
    int64_t baseMs_ = kNone;
    float baseProgress_ = 0.0f;
    float latestProgress_ = 0.0f;
    bool shown_ = false;
    float shownSeconds_ = 0.0f;
    int64_t shownAtMs_ = 0;
};

// How the shell words an estimate: which phrase, and the number a counted phrase carries.
namespace eta {

enum Phrase : int { kAlmostDone = 0, kAboutAMinute = 1, kMinutes = 2, kHours = 3 };

struct Wording {
    Phrase phrase = kAlmostDone;
    int count = 0;  // minutes or hours, rounded to the nearest; 0 for the two fixed phrases
};

// Under half a minute is "almost done", under a minute and a half "about a minute", under an hour whole
// minutes, then whole hours.
Wording wordingFor(int64_t secondsRemaining);

}  // namespace eta

}  // namespace geode::output
