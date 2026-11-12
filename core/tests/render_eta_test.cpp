// Host-side checks for a render's time left (core/output/RenderEta) and its C ABI: a fast lead-in neither
// shows nor skews the estimate, nothing is said for the first ten seconds of the steady render, the figure
// then stays near the truth and only counts down, a new run starts over, and the wording's thresholds.
// Built and run by CI (.github/workflows/native-host-tests.yml, "Native export planning tests"); no device.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "api/geode_api.h"
#include "output/RenderEta.hpp"

using namespace geode::output;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// Analysis takes 0..0.2 of the bar in 3 s and the audio transcode 0.2..0.28 in 2 s, neither steady; `sample`
// is the estimator under test. Returns the clock after the lead-in.
template <typename Sample>
int64_t leadIn(Sample sample) {
    int64_t now = 0;
    for (int step = 0; step <= 30; ++step) {
        check(sample(0.2f * static_cast<float>(step) / 30.0f, now, false) == RenderEta::kUnknown, "analysis shows nothing");
        now += 100;
    }
    for (int step = 0; step <= 20; ++step) {
        check(sample(0.2f + 0.08f * static_cast<float>(step) / 20.0f, now, false) == RenderEta::kUnknown,
              "the transcode shows nothing");
        now += 100;
    }
    return now;
}

// The render takes the rest of the bar at an even pace over four minutes.
template <typename Sample>
void evenRender(Sample sample, int64_t now) {
    const int64_t renderMs = 240'000;
    const int64_t renderStart = now;
    int shown = 0;
    int64_t previous = RenderEta::kUnknown;
    bool quietStart = true;
    bool near = true;
    bool countsDown = true;
    while (now - renderStart <= renderMs) {
        const float done = static_cast<float>(now - renderStart) / static_cast<float>(renderMs);
        const int64_t estimate = sample(0.28f + 0.72f * done, now, true);
        if (now - renderStart < 10'000 && estimate != RenderEta::kUnknown) quietStart = false;
        if (estimate != RenderEta::kUnknown) {
            const int64_t truth = (renderMs - (now - renderStart)) / 1000;
            if (estimate - truth > 3 || truth - estimate > 3) near = false;
            if (previous != RenderEta::kUnknown && estimate > previous + 1) countsDown = false;
            previous = estimate;
            ++shown;
        }
        now += 100;
    }
    check(quietStart, "nothing for the first ten seconds of the render");
    check(near, "within three seconds of the truth");
    check(countsDown, "never climbs by more than a second");
    check(shown > 2'000, "shown for most of the render");
    check(previous == 0, "ends at zero");
}

void evenPace() {
    RenderEta eta;
    const auto sample = [&eta](float progress, int64_t atMs, bool steady) { return eta.sample(progress, atMs, steady); };
    evenRender(sample, leadIn(sample));
}

void gates() {
    RenderEta eta;
    check(eta.sample(0.10f, 1'000, true) == RenderEta::kUnknown, "the first steady sample starts the warm-up");
    check(eta.sample(0.11f, 4'999, true) == RenderEta::kUnknown, "still warming up");
    check(eta.sample(0.20f, 5'000, true) == RenderEta::kUnknown, "the warm-up's end sets the baseline");
    check(eta.sample(0.30f, 10'999, true) == RenderEta::kUnknown, "under the minimum span");
    // 0.2 gained in 6 s with 0.6 left: 18 s.
    check(eta.sample(0.40f, 11'000, true) == 18, "the mean rate since the baseline");
    check(eta.sample(1.0f, 11'100, true) == 0, "a full bar has nothing left");
    check(eta.sample(2.0f, 11'200, true) == 0, "progress past one is one");

    RenderEta slow;
    slow.sample(0.5f, 0, true);
    slow.sample(0.5f, 4'000, true);
    check(slow.sample(0.505f, 60'000, true) == RenderEta::kUnknown, "under the minimum progress says nothing");
    check(slow.sample(0.52f, 64'000, true) > 0, "and speaks once more than a hundredth is gained");
}

void startsOver() {
    RenderEta eta;
    eta.sample(0.1f, 0, true);
    eta.sample(0.2f, 4'000, true);
    check(eta.sample(0.4f, 10'000, true) != RenderEta::kUnknown, "an estimate is up");
    check(eta.sample(0.3f, 10'100, true) == RenderEta::kUnknown, "progress moving backwards starts over");
    check(eta.sample(0.35f, 12'000, true) == RenderEta::kUnknown, "and warms up again");
    check(eta.sample(0.4f, 16'000, true) == RenderEta::kUnknown, "the new baseline");
    check(eta.sample(0.6f, 22'000, true) == 12, "the new run's own rate");
    check(eta.sample(0.6f, 22'100, false) == RenderEta::kUnknown, "a sample that is not steady starts over");
    check(eta.sample(0.9f, 40'000, true) == RenderEta::kUnknown, "and the warm-up runs again after it");
    eta.reset();
    check(eta.sample(0.95f, 60'000, true) == RenderEta::kUnknown, "after a reset nothing is known");

    RenderEta odd;
    check(odd.sample(std::nanf(""), 0, true) == RenderEta::kUnknown, "not-a-number is no progress");
    check(odd.sample(-1.0f, 100, true) == RenderEta::kUnknown, "nor is less than none");
    const int64_t far = std::numeric_limits<int64_t>::max() / 4;
    odd.sample(0.0f, far, true);
    odd.sample(0.0f, far + 4'000, true);
    check(odd.sample(0.01f, far + 4'000 + 3'000'000'000, true) > 0, "a huge clock and a slow render still answer");
}

void wording() {
    using namespace eta;
    check(wordingFor(0).phrase == kAlmostDone && wordingFor(29).phrase == kAlmostDone, "under half a minute");
    check(wordingFor(-5).phrase == kAlmostDone && wordingFor(29).count == 0, "less than none, and no count");
    check(wordingFor(30).phrase == kAboutAMinute && wordingFor(89).phrase == kAboutAMinute, "about a minute");
    check(wordingFor(90).phrase == kMinutes && wordingFor(90).count == 2, "a minute and a half rounds to two");
    check(wordingFor(149).count == 2 && wordingFor(150).count == 3, "minutes round to the nearest");
    check(wordingFor(3599).phrase == kMinutes && wordingFor(3599).count == 60, "just under the hour");
    check(wordingFor(3600).phrase == kHours && wordingFor(3600).count == 1, "an hour");
    check(wordingFor(5399).count == 1 && wordingFor(5400).count == 2, "hours round to the nearest");
    check(wordingFor(std::numeric_limits<int64_t>::max()).count == std::numeric_limits<int32_t>::max(),
          "the hour count stops at what an int holds");
}

void abi() {
    geode_render_eta* h = geode_render_eta_create();
    check(h != nullptr, "ABI create");
    const auto sample = [h](float progress, int64_t atMs, bool steady) {
        return geode_render_eta_sample(h, progress, atMs, steady ? 1 : 0);
    };
    evenRender(sample, leadIn(sample));
    geode_render_eta_reset(h);
    check(geode_render_eta_sample(h, 0.0f, 0, 1) == GEODE_RENDER_ETA_UNKNOWN, "ABI reset starts over");
    geode_render_eta_destroy(h);

    check(geode_render_eta_sample(nullptr, 0.5f, 0, 1) == GEODE_RENDER_ETA_UNKNOWN, "no handle knows nothing");
    geode_render_eta_reset(nullptr);
    geode_render_eta_destroy(nullptr);

    check(geode_render_eta_phrase(10) == GEODE_ETA_ALMOST_DONE && geode_render_eta_phrase_count(10) == 0, "ABI almost done");
    check(geode_render_eta_phrase(60) == GEODE_ETA_ABOUT_A_MINUTE, "ABI about a minute");
    check(geode_render_eta_phrase(600) == GEODE_ETA_MINUTES && geode_render_eta_phrase_count(600) == 10, "ABI minutes");
    check(geode_render_eta_phrase(7200) == GEODE_ETA_HOURS && geode_render_eta_phrase_count(7200) == 2, "ABI hours");
}

}  // namespace

int main() {
    evenPace();
    gates();
    startsOver();
    wording();
    abi();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("render_eta_test: all passed\n");
    return 0;
}
