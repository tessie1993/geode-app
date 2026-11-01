// Host-side proof of the output flash limiter's promise (core/viz/FlashLimiter, the control logic the
// GPU pass mirrors): whatever is drawn, what every region SHOWS never holds more than six WCAG 2.3.1
// transitions (three flashes) of luminance, nor six of saturated red, in any one-second window;
// while kicks up to 2 Hz, a single hard cut, slow fades and jitter pass bit for bit. Built and run by
// CI (see .github/workflows/android.yml, "Native host tests") with the system compiler; no GL.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <vector>

#include "viz/FlashLimiter.hpp"
#include "viz/VisualSafety.hpp"

using namespace geode::viz;
using flash_limit::kRegions;
using flash_limit::kTiles;
using Tiles = std::array<flash_limit::TileInput, kTiles>;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

flash_limit::TileInput pixel(float r, float g, float b) {
    const flash_limit::PixelMeasure m = flash_limit::measure(r, g, b);
    return {m.lum, m.red, m.guardedLum, 1.0f};
}

flash_limit::TileInput grey(float v) { return pixel(v, v, v); }

struct Run {
    std::vector<float> times;
    std::array<std::vector<float>, kRegions> lum;
    std::array<std::vector<float>, kRegions> red;
    std::array<std::vector<float>, kRegions> rawLum;
    std::array<std::vector<float>, kRegions> rawRed;
    bool untouched = true;  // every transform of every frame was the identity
};

// Feeds `frame(i, t, tiles)` to the limiter at `fps` for `seconds`.
Run limit(const std::function<void(int, float, Tiles&)>& frame, float fps, float seconds,
          const flash_limit::Layout& layout = flash_limit::Layout{}) {
    Run run;
    flash_limit::State state;
    flash_limit::State unlimited;  // never limits: fed nothing but primes, to read the raw region means
    const float dt = 1.0f / fps;
    const int frames = static_cast<int>(std::lround(seconds * fps));
    Tiles tiles;
    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) * dt;
        frame(i, t, tiles);
        const flash_limit::FrameResult out = flash_limit::step(state, layout, tiles, dt);
        unlimited.primed = false;
        const flash_limit::FrameResult raw = flash_limit::step(unlimited, layout, tiles, dt);
        run.times.push_back(t);
        for (int r = 0; r < kRegions; ++r) {
            run.lum[r].push_back(out.lumShown[r]);
            run.red[r].push_back(out.redShown[r]);
            run.rawLum[r].push_back(raw.lumShown[r]);
            run.rawRed[r].push_back(raw.redShown[r]);
        }
        for (const auto& x : out.transforms) run.untouched = run.untouched && x.identity();
    }
    return run;
}

std::function<void(int, float, Tiles&)> uniform(const std::function<flash_limit::TileInput(float)>& at) {
    return [at](int, float t, Tiles& tiles) { tiles.fill(at(t)); };
}

// The most counted transitions any alternating chain of full swings holds inside samples
// [first, last]: exhaustive, the definition itself rather than a detector. For luminance a leg
// counts when its darker end is under the dark limit (the rest may still link the chain).
int mostTransitions(const std::vector<float>& v, size_t first, size_t last, bool lum) {
    // Only a window's local extremes (and its ends) can carry a longest chain.
    std::vector<float> e;
    for (size_t i = first; i <= last; ++i) {
        if (i > first && i < last) {
            const float a = v[i - 1], b = v[i], c = v[i + 1];
            if (b == a || (a < b && b < c) || (a > b && b > c)) continue;
        }
        e.push_back(v[i]);
    }
    const float swing = (lum ? safety::kFlashSwing : safety::kRedSwing) - 1e-6f;
    std::vector<int> up(e.size(), 0), down(e.size(), 0);
    int most = 0;
    for (size_t i = 0; i < e.size(); ++i) {
        for (size_t j = 0; j < i; ++j) {
            const float d = e[i] - e[j];
            const int counted = (!lum || std::min(e[i], e[j]) < safety::kFlashDarkLimit) ? 1 : 0;
            if (d >= swing) up[i] = std::max(up[i], down[j] + counted);
            if (-d >= swing) down[i] = std::max(down[i], up[j] + counted);
        }
        most = std::max({most, up[i], down[i]});
    }
    return most;
}

int mostPerSecond(const Run& run, const std::vector<float>& v, bool lum) {
    int most = 0;
    size_t last = 0;
    for (size_t first = 0; first < v.size(); ++first) {
        while (last + 1 < v.size() && run.times[last + 1] - run.times[first] <= 1.0f + 1e-5f) ++last;
        most = std::max(most, mostTransitions(v, first, std::max(first, last), lum));
    }
    return most;
}

// The worst region of a run, luminance and red.
int worstShown(const Run& run) {
    int most = 0;
    for (int r = 0; r < kRegions; ++r) {
        if (r > 0 && run.lum[r] == run.lum[r - 1] && run.red[r] == run.red[r - 1]) continue;
        most = std::max({most, mostPerSecond(run, run.lum[r], true), mostPerSecond(run, run.red[r], false)});
    }
    return most;
}

int worstRaw(const Run& run) {
    int most = 0;
    for (int r = 0; r < kRegions; ++r) {
        most = std::max({most, mostPerSecond(run, run.rawLum[r], true), mostPerSecond(run, run.rawRed[r], false)});
    }
    return most;
}

constexpr float kFrameRates[] = {24.0f, 30.0f, 60.0f, 120.0f};

float square(float t, float hz) { return std::fmod(t * hz, 1.0f) < 0.5f ? 1.0f : 0.0f; }

void kicksAtTwoHertzPassUntouched() {
    for (const float fps : kFrameRates) {
        const Run run = limit(uniform([](float t) { return grey(0.6f * square(t, 2.0f)); }), fps, 6.0f);
        check(worstRaw(run) >= 4, "the 2 Hz kick really swings (sanity)");
        check(run.untouched, "a 2 Hz full-screen kick (0 <-> 0.6) passes bit for bit");
    }
    const Run sharp = limit(uniform([](float t) {
                                const float phase = std::fmod(t * 2.0f, 1.0f);
                                return grey(0.15f + 0.7f * std::exp(-phase / 0.06f));
                            }),
                            60.0f, 6.0f);
    check(sharp.untouched, "a 2 Hz kick with a fast decay passes bit for bit");
}

void strobesAreCappedAtThreeFlashes() {
    for (const float fps : kFrameRates) {
        for (const float hz : {3.0f, 4.0f, 5.0f, 7.0f, 10.0f, 15.0f, 25.0f}) {
            for (const auto& [lo, hi] : {std::pair{0.0f, 1.0f}, {0.2f, 0.5f}, {0.0f, 0.15f}, {0.72f, 1.0f}}) {
                const Run run = limit(uniform([=](float t) { return grey(lo + (hi - lo) * square(t, hz)); }), fps, 4.0f);
                const int shown = worstShown(run);
                // (3 Hz is exactly six transitions a window, and some rates alias at low frame rates.)
                if (hz == 4.0f || hz == 10.0f) {
                    if (lo == 0.0f && hi == 1.0f) {
                        std::printf("%5.0f fps %4.0f Hz black/white: raw %2d, shown %d transitions a second at most\n", fps, hz,
                                    worstRaw(run), shown);
                    }
                    check(worstRaw(run) > 6, "the raw 4 and 10 Hz strobes really exceed three flashes (sanity)");
                }
                check(shown <= 6, "a strobe shows at most six transitions (three flashes) in any second");
            }
        }
    }
    // Both states at or above the dark limit: not a flash by definition, so not touched.
    const Run bright = limit(uniform([](float t) { return grey(0.82f + 0.18f * square(t, 10.0f)); }), 60.0f, 3.0f);
    check(bright.untouched, "a flicker that never goes darker than 0.8 is not a flash and passes");
}

void aSingleCutPasses() {
    for (const float fps : kFrameRates) {
        const Run up = limit(uniform([](float t) { return grey(t < 1.0f ? 0.0f : 1.0f); }), fps, 3.0f);
        check(up.untouched, "a single black-to-white cut passes bit for bit");
        const Run down = limit(uniform([](float t) { return grey(t < 1.0f ? 1.0f : 0.0f); }), fps, 3.0f);
        check(down.untouched, "a single white-to-black cut passes bit for bit");
    }
}

void redFlashesAreCapped() {
    // Saturated red against a grey of the same luminance: no luminance change at all, only red.
    const float y = flash_limit::luminance(1.0f, 0.0f, 0.0f);
    for (const float fps : kFrameRates) {
        const Run run = limit(uniform([=](float t) { return square(t, 5.0f) > 0.5f ? pixel(1.0f, 0.0f, 0.0f) : grey(y); }), fps, 4.0f);
        int raw = 0, shown = 0;
        for (int r = 0; r < kRegions; ++r) {
            raw = std::max(raw, mostPerSecond(run, run.rawRed[r], false));
            shown = std::max({shown, mostPerSecond(run, run.red[r], false), mostPerSecond(run, run.lum[r], true)});
        }
        std::printf("%5.0f fps 5 Hz red/grey: raw %d red transitions, shown %d at most\n", fps, raw, shown);
        check(raw > 6, "the raw red flash really exceeds three a second (sanity)");
        check(shown <= 6, "a 5 Hz saturated-red flash shows at most three red flashes a second");
    }
    const Run once = limit(uniform([](float t) { return t > 1.0f && t < 1.2f ? pixel(0.9f, 0.05f, 0.05f) : grey(0.1f); }), 60.0f, 3.0f);
    check(once.untouched, "a single red flash passes");
}

void fadesAndJitterPass() {
    for (const float fps : kFrameRates) {
        const Run fade = limit(uniform([](float t) { return grey(t < 5.0f ? t / 5.0f : 2.0f - t / 5.0f); }), fps, 10.0f);
        check(fade.untouched, "a slow fade up and down passes bit for bit");
        const Run jitter = limit(uniform([fps](float t) {
                                     return grey(0.4f + 0.045f * (std::lround(t * fps) % 2 ? 1.0f : -1.0f));
                                 }),
                                 fps, 4.0f);
        check(jitter.untouched, "every-frame jitter under a full swing passes bit for bit");
    }
}

void kicksAndCutsTogether() {
    for (const float fps : kFrameRates) {
        const Run run = limit(uniform([](float t) {
                                  const float cut = std::fmod(t, 4.0f) < 2.0f ? 0.0f : 0.35f;  // a cut every 2 s (0.25 Hz)
                                  return grey(cut + 0.6f * square(t, 2.0f));
                              }),
                              fps, 8.0f);
        check(worstShown(run) <= 6, "a 2 Hz kick with a 0.25 Hz cut on top stays within three flashes");
    }
}

// Different content per tile, so overlapping regions disagree and holds spread between them.
void spatialAndRandomInputsHold() {
    const flash_limit::Layout layout;
    for (unsigned seed = 1; seed <= 12; ++seed) {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        std::array<float, kTiles> hz{}, phase{}, lo{}, hi{};
        std::array<bool, kTiles> isRed{};
        for (int t = 0; t < kTiles; ++t) {
            // Blocks of tiles share a strobe, so whole regions flash together.
            std::mt19937 block(seed * 977u + static_cast<unsigned>((t % layout.cols) / 4 + 10 * ((t / layout.cols) / 3)));
            hz[t] = 1.0f + 14.0f * unit(block);
            phase[t] = unit(block);
            lo[t] = 0.6f * unit(block);
            hi[t] = std::min(1.0f, lo[t] + 0.1f + 0.9f * unit(block));
            isRed[t] = unit(block) < 0.3f;
        }
        const bool noisy = seed % 3 == 0;
        const Run run = limit(
            [&](int, float t, Tiles& tiles) {
                for (int k = 0; k < kTiles; ++k) {
                    const bool on = noisy ? unit(rng) < 0.5f : square(t + phase[k], hz[k]) > 0.5f;
                    const float v = on ? hi[k] : lo[k];
                    tiles[k] = isRed[k] && on ? pixel(v, 0.04f * v, 0.02f * v) : grey(v);
                }
            },
            seed % 2 ? 60.0f : 30.0f, 5.0f);
        check(worstShown(run) <= 6, "random tiled strobes and noise: no region shows more than three flashes a second");
    }
    // Half the screen strobes at 10 Hz while the other half kicks at 2 Hz.
    const Run half = limit(
        [&](int, float t, Tiles& tiles) {
            for (int k = 0; k < kTiles; ++k) tiles[k] = grey(k % layout.cols < 8 ? square(t, 10.0f) : 0.6f * square(t, 2.0f));
        },
        60.0f, 5.0f);
    check(worstShown(half) <= 6, "a half-screen strobe beside a half-screen kick stays within three flashes");
    // The same in portrait (9 x 16 tiles): the left columns strobe, the right ones kick.
    const flash_limit::Layout portrait = flash_limit::Layout::forSize(1080, 2400);
    const Run tall = limit(
        [&](int, float t, Tiles& tiles) {
            for (int k = 0; k < kTiles; ++k) tiles[k] = grey(k % portrait.cols < 5 ? square(t, 8.0f) : 0.6f * square(t, 2.0f));
        },
        30.0f, 5.0f, portrait);
    check(portrait.cols == 9 && portrait.rows == 16, "portrait frames use a 9 x 16 grid");
    check(worstShown(tall) <= 6, "portrait: a strobe beside a kick stays within three flashes");
}

void firstFrameAndRelease() {
    // A strobe for two seconds, then a still picture: the held tiles return to it quickly.
    const Run run = limit(uniform([](float t) { return grey(t < 2.0f ? square(t, 10.0f) : 0.3f); }), 60.0f, 4.0f);
    check(std::fabs(run.lum[0].front() - run.rawLum[0].front()) < 1e-6f, "the first frame is shown as drawn");
    const size_t settled = static_cast<size_t>(60 * (2.0f + 1.0f + 0.3f));
    check(std::fabs(run.lum[0][settled] - run.rawLum[0][settled]) < 1e-4f,
          "after a strobe stops, the picture is back once the window clears plus a short ease");
}

void transformsLandOnTheShownValue() {
    // A tile of four linear pixels, one saturated red; dim it and raise it (touched, so guarded), check means.
    const float px[4][3] = {{0.0f, 0.0f, 0.0f}, {0.1f, 0.2f, 0.3f}, {0.9f, 0.05f, 0.02f}, {1.0f, 1.0f, 1.0f}};
    float lum = 0.0f, red = 0.0f, guarded = 0.0f;
    for (const auto& p : px) {
        const flash_limit::PixelMeasure m = flash_limit::measure(p[0], p[1], p[2]);
        lum += m.lum / 4.0f;
        red += m.red / 4.0f;
        guarded += m.guardedLum / 4.0f;
    }
    for (const float target : {0.1f, lum, 0.8f}) {
        const bool guard = target != lum;
        const float base = guard ? guarded : lum;
        const float gain = target < base ? target / base : 1.0f;
        const float lift = target > base ? (target - base) / (1.0f - base) : 0.0f;
        float outLum = 0.0f, outRed = 0.0f;
        for (const auto& p : px) {
            float c[3] = {p[0], p[1], p[2]};
            if (guard) flash_limit::guard(c[0], c[1], c[2]);
            for (float& v : c) v = v * gain + (1.0f - v * gain) * lift;
            const flash_limit::PixelMeasure m = flash_limit::measure(c[0], c[1], c[2]);
            outLum += m.lum / 4.0f;
            outRed += m.red / 4.0f;
        }
        check(std::fabs(outLum - target) < 1e-5f, "a tile's transform puts its mean luminance on the shown value");
        check(outRed == (guard ? 0.0f : red), "and its red is 0 once touched (guarded), its own when not");
    }
    // The guard: for every saturated red, red to exactly 0, luminance kept, and a later gain or lift
    // never makes it saturated again.
    for (int ri = 1; ri <= 32; ++ri) {
        for (int gi = 0; gi <= 8; ++gi) {
            for (int bi = 0; bi <= 8; ++bi) {
                float r = static_cast<float>(ri) / 32.0f, g = r * static_cast<float>(gi) / 40.0f, b = r * static_cast<float>(bi) / 40.0f;
                const flash_limit::PixelMeasure before = flash_limit::measure(r, g, b);
                flash_limit::guard(r, g, b);
                const flash_limit::PixelMeasure after = flash_limit::measure(r, g, b);
                check(after.red == 0.0f, "the guard leaves no saturated red");
                check(std::fabs(after.lum - before.lum) < 1e-6f && std::fabs(before.guardedLum - before.lum) < 1e-6f,
                      "the guard keeps the pixel's luminance");
                for (const float k : {0.3f, 0.9f}) {
                    check(flash_limit::measure(r * k, g * k, b * k).red == 0.0f, "a gain keeps a guarded pixel unsaturated");
                    const auto lifted = [k](float v) { return v + (1.0f - v) * (1.0f - k); };
                    check(flash_limit::measure(lifted(r), lifted(g), lifted(b)).red == 0.0f, "a lift keeps a guarded pixel unsaturated");
                }
            }
        }
    }
    for (int i = 0; i <= 255; ++i) {
        const float c = static_cast<float>(i) / 255.0f;
        check(std::fabs(flash_limit::linearToSrgb(flash_limit::srgbToLinear(c)) - c) < 1e-4f, "sRGB decode and encode round-trip");
    }
}

}  // namespace

int main() {
    kicksAtTwoHertzPassUntouched();
    strobesAreCappedAtThreeFlashes();
    aSingleCutPasses();
    redFlashesAreCapped();
    fadesAndJitterPass();
    kicksAndCutsTogether();
    spatialAndRandomInputsHold();
    firstFrameAndRelease();
    transformsLandOnTheShownValue();
    if (failures == 0) std::printf("flash_limit_test: all passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
