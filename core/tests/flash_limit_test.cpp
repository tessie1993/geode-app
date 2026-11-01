// Host-side proof of the output flash limiter's promise (core/viz/FlashLimiter, the maths the GPU
// pass mirrors): whatever a tile's raw luminance does, the luminance it shows never holds more than
// three WCAG 2.3.1 general flashes (six opposing swings of >= 10%) in any one-second window, while
// slow fades and a single beat flash go through untouched. Built and run by CI (see
// .github/workflows/android.yml, "Native host tests") with the system compiler; no GL, no device.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "viz/FlashLimiter.hpp"
#include "viz/VisualSafety.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

struct Run {
    std::vector<float> times;
    std::vector<float> raw;
    std::vector<float> shown;
};

// Feeds `signal(t)` to one tile at `fps` for `seconds`.
template <typename Signal>
Run limit(Signal signal, float fps, float seconds) {
    Run run;
    flash_limit::TileState state;
    const float dt = 1.0f / fps;
    const int frames = static_cast<int>(std::lround(seconds * fps));
    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) * dt;
        const float raw = signal(t);
        state = flash_limit::step(state, raw, dt);
        run.times.push_back(t);
        run.raw.push_back(flash_limit::quantise(raw));
        run.shown.push_back(state.shown);
    }
    return run;
}

// The most WCAG transitions (alternating swings of >= 0.1 between any two frames) the samples
// [first, last] hold. Exhaustive over every alternating chain, so it is the true maximum and not a
// detector's estimate: best[i][d] = the longest chain ending at frame i with a swing in direction d.
int mostTransitions(const std::vector<float>& v, size_t first, size_t last) {
    const float swing = safety::kFlashSwing - 1e-6f;
    std::vector<int> up(last - first + 1, 0);
    std::vector<int> down(last - first + 1, 0);
    int most = 0;
    for (size_t i = first; i <= last; ++i) {
        for (size_t j = first; j < i; ++j) {
            const float d = v[i] - v[j];
            if (d >= swing) up[i - first] = std::max(up[i - first], down[j - first] + 1);
            if (-d >= swing) down[i - first] = std::max(down[i - first], up[j - first] + 1);
        }
        most = std::max({most, up[i - first], down[i - first]});
    }
    return most;
}

// The most transitions in any window of one second (every window that starts on a frame).
int mostTransitionsPerSecond(const Run& run, const std::vector<float>& v) {
    int most = 0;
    size_t last = 0;
    for (size_t first = 0; first < v.size(); ++first) {
        while (last + 1 < v.size() && run.times[last + 1] - run.times[first] <= 1.0f + 1e-5f) ++last;
        if (last < first) last = first;
        most = std::max(most, mostTransitions(v, first, last));
    }
    return most;
}

float maxDifference(const Run& run) {
    float most = 0.0f;
    for (size_t i = 0; i < run.raw.size(); ++i) most = std::max(most, std::fabs(run.raw[i] - run.shown[i]));
    return most;
}

constexpr float kLsb = 1.0f / flash_limit::kQuantSteps;

void squareWaveIsHeldToThreeFlashes(float fps) {
    const auto square = [](float t) { return std::fmod(t * 10.0f, 1.0f) < 0.5f ? 1.0f : 0.0f; };
    const Run run = limit(square, fps, 6.0f);
    check(mostTransitionsPerSecond(run, run.raw) >= 19, "the raw 10 Hz square wave really flashes (sanity)");
    const int shown = mostTransitionsPerSecond(run, run.shown);
    std::printf("10 Hz full square wave at %.0f fps: at most %d transitions (%d flashes) in any second\n", fps, shown, shown / 2);
    check(shown <= 6, "a 10 Hz black/white square wave shows at most six transitions (three flashes) a second");
}

// Every rate and depth an adversary might pick, and white noise, at 30, 60 and 120 fps.
void noStrobeGetsThrough() {
    for (const float fps : {24.0f, 30.0f, 60.0f, 120.0f}) {
        for (const float hz : {2.0f, 3.0f, 3.5f, 4.0f, 5.0f, 7.0f, 10.0f, 15.0f, 25.0f}) {
            for (const float lo : {0.0f, 0.2f, 0.45f}) {
                for (const float depth : {0.12f, 0.25f, 0.5f, 1.0f}) {
                    const float hi = std::min(lo + depth, 1.0f);
                    const auto wave = [=](float t) { return std::fmod(t * hz, 1.0f) < 0.5f ? hi : lo; };
                    const Run run = limit(wave, fps, 4.0f);
                    check(mostTransitionsPerSecond(run, run.shown) <= 6, "no square wave shows more than three flashes a second");
                    const auto sine = [=](float t) { return lo + (hi - lo) * (0.5f + 0.5f * std::sin(6.2831853f * hz * t)); };
                    const Run smooth = limit(sine, fps, 4.0f);
                    check(mostTransitionsPerSecond(smooth, smooth.shown) <= 6, "no sine flicker shows more than three flashes a second");
                }
            }
        }
        std::mt19937 rng(1234u + static_cast<unsigned>(fps));
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        std::vector<float> noise(static_cast<size_t>(fps * 8.0f) + 2);
        for (float& n : noise) n = unit(rng);
        const auto random = [&](float t) { return noise[static_cast<size_t>(std::lround(t * fps))]; };
        const Run run = limit(random, fps, 8.0f);
        check(mostTransitionsPerSecond(run, run.shown) <= 6, "white noise shows at most three flashes a second");
    }
}

void slowFadesPassUnchanged() {
    // Black to white over ten seconds, and back.
    const auto fade = [](float t) { return t < 10.0f ? t / 10.0f : std::max(0.0f, 2.0f - t / 10.0f); };
    for (const float fps : {30.0f, 60.0f}) {
        const Run run = limit(fade, fps, 20.0f);
        check(maxDifference(run) <= 2.0f * kLsb, "a ten-second fade passes unchanged");
    }
    // A brisker scene fade: 0.3 of luminance in half a second, once.
    const auto brisk = [](float t) { return t < 1.0f ? 0.2f : t < 1.5f ? 0.2f + 0.6f * (t - 1.0f) : 0.5f; };
    const Run run = limit(brisk, 60.0f, 4.0f);
    check(maxDifference(run) <= 2.0f * kLsb, "a half-second 0.3 fade passes unchanged");
}

void singleBeatFlashPasses() {
    // A beat: +0.2 of full-screen luminance on one frame, decaying with a 100 ms time constant.
    const auto beat = [](float t) { return t < 1.0f ? 0.25f : 0.25f + 0.2f * std::exp(-(t - 1.0f) / 0.1f); };
    for (const float fps : {30.0f, 60.0f}) {
        const Run run = limit(beat, fps, 4.0f);
        check(maxDifference(run) <= 2.0f * kLsb, "a single beat flash passes unchanged");
    }
    // Jitter under the band (a busy tile's mean twitching every frame) never touches the budget.
    const auto jitter = [](float t) { return 0.4f + 0.009f * (std::fmod(t * 60.0f, 2.0f) < 1.0f ? 1.0f : -1.0f); };
    const Run run = limit(jitter, 60.0f, 30.0f);
    check(maxDifference(run) <= 2.0f * kLsb, "sub-band jitter passes unchanged");
}

void firstFrameIsShownAsItIs() {
    flash_limit::TileState state;
    state = flash_limit::step(state, 0.9f, 1.0f / 60.0f);
    check(state.primed && std::fabs(state.shown - 0.9f) <= kLsb, "the first frame is shown as measured, no fade-in from black");
    check(std::fabs(state.budget - safety::kFlashBudgetCapacity) <= 1e-4f, "the first frame starts with a full bucket");
}

void hardCutIsLimitedNotFrozen() {
    const auto cut = [](float t) { return t < 1.0f ? 0.0f : 1.0f; };
    const Run run = limit(cut, 60.0f, 8.0f);
    const size_t at = 60;
    const float jump = run.shown[at] - run.shown[at - 1];
    check(jump > 0.3f && jump < 0.4f, "a black-to-white cut shows its first jump at once, up to the bucket");
    check(run.shown.back() > 0.999f, "and reaches white within a few seconds");
}

void gainReachesTheShownLuminance() {
    // A tile of four pixels (linear luminance); dim and raise it and check the mean lands on target.
    const float pixels[4] = {0.0f, 0.1f, 0.6f, 1.0f};
    const float raw = (pixels[0] + pixels[1] + pixels[2] + pixels[3]) / 4.0f;
    for (const float target : {0.0f, 0.1f, raw, 0.6f, 1.0f}) {
        const flash_limit::TileGain g = flash_limit::gainFor(target, raw);
        float mean = 0.0f;
        for (const float p : pixels) {
            const float out = flash_limit::applyGain(g, p);
            check(out >= -1e-6f && out <= 1.0f + 1e-6f, "the gain never leaves 0..1 (no clipping)");
            mean += out / 4.0f;
        }
        check(std::fabs(mean - target) < 1e-5f, "the gain puts the tile's mean luminance on the limited value");
    }
    const flash_limit::TileGain same = flash_limit::gainFor(raw, raw);
    check(same.gain == 1.0f && same.lift == 0.0f, "an unlimited tile is left exactly as drawn");
    for (int i = 0; i <= 255; ++i) {
        const float c = static_cast<float>(i) / 255.0f;
        check(std::fabs(flash_limit::linearToSrgb(flash_limit::srgbToLinear(c)) - c) < 1e-4f, "sRGB decode and encode round-trip");
    }
}

}  // namespace

int main() {
    squareWaveIsHeldToThreeFlashes(30.0f);
    squareWaveIsHeldToThreeFlashes(60.0f);
    noStrobeGetsThrough();
    slowFadesPassUnchanged();
    singleBeatFlashPasses();
    firstFrameIsShownAsItIs();
    hardCutIsLimitedNotFrozen();
    gainReachesTheShownLuminance();
    if (failures == 0) std::printf("flash_limit_test: all passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
