// Host-side checks for the photosensitive flash limiter (core/viz/FlashLimiter). Built and run by CI
// ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL, no device.
//
// Closed loop, as on the device: each frame the limiter grades the source, the shown frame is quantized
// to the 16 × 16 RGBA8 grid OutputProbe reads back, and that measurement reaches the limiter one to
// three frames late. A WCAG oracle written here, apart from the limiter's own counter, then counts the
// flashes in what was shown.
//
// What is pinned: a 10 Hz full white/black strobe (and a red one, and one in a single quarter) shows at
// most three flashes in any second and, once held, no frame-to-frame swing of 0.1; a slow fade and a beat
// pulse pass exactly untouched; the hold lets go smoothly once the strobe stops.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <vector>

#include "viz/FlashLimiter.hpp"

using namespace geode::viz::safety;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr double kFps = 60.0;
constexpr int kSide = kProbeGrid;
using Rgb = std::array<float, 3>;
using Grid = std::vector<uint8_t>;
// The source picture: linear RGB of cell (x, y) at time t.
using Source = std::function<Rgb(int x, int y, double t)>;

uint8_t encode8(float linear) { return static_cast<uint8_t>(std::lround(toEncoded(linear) * 255.0f)); }

struct Run {
    std::vector<FrameLight> shown;  // what went out, as the grid measures it
    std::vector<Grade> grades;
    std::vector<Grid> grids;
    std::vector<Grid> sourceGrids;
    bool heldAtEnd = false;
};

Run simulate(const Source& source, double seconds, int lag) {
    FlashLimiter limiter;
    Run run;
    struct Pending {
        int frame;
        Grid grid;
        Grade grade;
        double t;
    };
    std::deque<Pending> inFlight;
    const int frames = static_cast<int>(seconds * kFps);
    for (int k = 0; k < frames; k++) {
        const double t = k / kFps;
        while (!inFlight.empty() && inFlight.front().frame <= k - lag) {
            const Pending& p = inFlight.front();
            limiter.observe(measureGrid(p.grid.data(), kSide), p.grade, p.t);
            inFlight.pop_front();
        }
        const Grade g = limiter.next(t);
        Grid grid(kSide * kSide * 4);
        Grid src(kSide * kSide * 4);
        for (int y = 0; y < kSide; y++) {
            for (int x = 0; x < kSide; x++) {
                const Rgb c = source(x, y, t);
                uint8_t* px = grid.data() + (y * kSide + x) * 4;
                uint8_t* sp = src.data() + (y * kSide + x) * 4;
                for (int ch = 0; ch < 3; ch++) {
                    // The composite's grade, in linear light.
                    px[ch] = encode8(g.anchor + (c[ch] - g.anchor) * g.scale);
                    sp[ch] = encode8(c[ch]);
                }
                px[3] = sp[3] = 255;
            }
        }
        run.shown.push_back(measureGrid(grid.data(), kSide));
        run.grades.push_back(g);
        run.grids.push_back(grid);
        run.sourceGrids.push_back(src);
        inFlight.push_back({k, std::move(grid), g, t});
        run.heldAtEnd = limiter.held();
    }
    return run;
}

// The oracle: frames at which a WCAG transition (an opposing swing of `threshold`, darker side below
// `ceiling`) completes, by peak and trough tracking.
std::vector<int> transitions(const std::vector<float>& v, float threshold, float ceiling) {
    std::vector<int> at;
    if (v.empty()) return at;
    float peak = v[0];
    float trough = v[0];
    int dir = 0;
    for (size_t i = 1; i < v.size(); i++) {
        peak = std::max(peak, v[i]);
        trough = std::min(trough, v[i]);
        if (dir != 1 && v[i] - trough >= threshold && trough < ceiling) {
            at.push_back(static_cast<int>(i));
            dir = 1;
            peak = trough = v[i];
        } else if (dir != -1 && peak - v[i] >= threshold && v[i] < ceiling) {
            at.push_back(static_cast<int>(i));
            dir = -1;
            peak = trough = v[i];
        }
    }
    return at;
}

// The most flashes (two transitions each) in any one-second window.
float worstFlashesPerSecond(const std::vector<int>& at) {
    const int window = static_cast<int>(kFps);
    int most = 0;
    for (size_t i = 0; i < at.size(); i++) {
        int n = 0;
        for (size_t j = i; j < at.size() && at[j] - at[i] < window; j++) n++;
        most = std::max(most, n);
    }
    return static_cast<float>(most) / 2.0f;
}

std::vector<float> series(const Run& run, int region, bool red) {
    std::vector<float> out;
    for (const FrameLight& f : run.shown) out.push_back(red ? saturatedRed(f.rgb[region]) : luminance(f.rgb[region]));
    return out;
}

float worstStepAfter(const std::vector<float>& v, int fromFrame) {
    float worst = 0.0f;
    for (size_t i = static_cast<size_t>(std::max(fromFrame, 1)); i < v.size(); i++) worst = std::max(worst, std::fabs(v[i] - v[i - 1]));
    return worst;
}

bool on10Hz(double t) { return std::fmod(t * 10.0, 1.0) < 0.5; }

void strobe10HzIsLimited() {
    const Source strobe = [](int, int, double t) {
        const float v = on10Hz(t) ? 1.0f : 0.0f;
        return Rgb{v, v, v};
    };
    for (int lag = 1; lag <= 3; lag++) {
        const Run run = simulate(strobe, 10.0, lag);
        const std::vector<float> luma = series(run, 0, false);
        const float flashes = worstFlashesPerSecond(transitions(luma, 0.1f, 0.8f));
        std::printf("10 Hz strobe, lag %d: worst %.1f flashes/s shown (source: 10), worst step after 1 s %.3f\n", lag, flashes,
                    worstStepAfter(luma, static_cast<int>(kFps)));
        check(flashes <= 3.0f, "a 10 Hz strobe shows at most three flashes in any second");
        check(worstStepAfter(luma, static_cast<int>(kFps)) < 0.1f, "held, no frame swings by a transition");
        check(run.heldAtEnd, "the limiter still holds while the strobe goes on");
    }
}

void redStrobeIsLimited() {
    const Source red = [](int, int, double t) { return on10Hz(t) ? Rgb{1.0f, 0.0f, 0.0f} : Rgb{0.0f, 0.0f, 0.0f}; };
    const Run run = simulate(red, 6.0, 2);
    const std::vector<float> r = series(run, 0, true);
    const float flashes = worstFlashesPerSecond(transitions(r, 20.0f / 320.0f, 2.0f));
    std::printf("10 Hz red strobe: worst %.1f red flashes/s shown\n", flashes);
    check(flashes <= 3.0f, "a red strobe shows at most three red flashes in any second");
    check(worstStepAfter(r, static_cast<int>(kFps)) < 20.0f / 320.0f, "held, red never swings by a red transition");
}

void strobeInOneQuarterIsLimited() {
    // Only the top-left quarter strobes; the rest is a still dim grey, so the whole frame's mean
    // moves by a quarter of the swing.
    const Source corner = [](int x, int y, double t) {
        const bool inCorner = x < kSide / 2 && y < kSide / 2;
        const float v = inCorner ? (on10Hz(t) ? 1.0f : 0.0f) : 0.05f;
        return Rgb{v, v, v};
    };
    const Run run = simulate(corner, 6.0, 2);
    const std::vector<float> quarter = series(run, 1, false);
    const float flashes = worstFlashesPerSecond(transitions(quarter, 0.1f, 0.8f));
    std::printf("10 Hz strobe in one quarter: worst %.1f flashes/s shown there\n", flashes);
    check(flashes <= 3.0f, "a strobing quarter is limited even when the whole frame barely moves");
}

void slowFadeAndBeatPassUntouched() {
    // A fade black to white and back every four seconds, and a full-swing flash on every beat at 60 bpm.
    // (Faster full-swing beats can put four transitions in one second, where the limiter takes hold on
    // purpose: two flashes a second leaves the readback's lag a flash of margin under three.)
    const Source fade = [](int, int, double t) {
        const double phase = std::fmod(t / 4.0, 1.0);
        const float v = static_cast<float>(phase < 0.5 ? phase * 2.0 : 2.0 - phase * 2.0);
        return Rgb{v, v * 0.8f, v * 0.6f};
    };
    const Source beat = [](int, int, double t) {
        const float v = std::fmod(t, 1.0) < 0.15 ? 1.0f : 0.1f;
        return Rgb{v, v, v};
    };
    for (const Source& s : {fade, beat}) {
        const Run run = simulate(s, 12.0, 2);
        bool untouched = true;
        bool identical = true;
        for (size_t i = 0; i < run.grades.size(); i++) {
            untouched = untouched && run.grades[i].scale == 1.0f;
            identical = identical && run.grids[i] == run.sourceGrids[i];
        }
        check(untouched, "a slow fade or a beat pulse never engages the limiter");
        check(identical, "untouched means every shown pixel is the source's, byte for byte");
    }
}

void letsGoSmoothlyAfterTheStrobe() {
    // Three seconds of strobe, then a still picture.
    const Source s = [](int, int, double t) {
        const float v = t < 3.0 ? (on10Hz(t) ? 1.0f : 0.0f) : 0.3f;
        return Rgb{v, v, v};
    };
    const Run run = simulate(s, 8.0, 2);
    const std::vector<float> luma = series(run, 0, false);
    const int released = static_cast<int>(6.0 * kFps);
    check(run.grades[static_cast<size_t>(released)].scale == 1.0f, "the hold lets go within three seconds of the strobe stopping");
    check(worstStepAfter(luma, static_cast<int>(kFps)) < 0.1f, "taking hold and letting go never jump");
    const float flashes = worstFlashesPerSecond(transitions(luma, 0.1f, 0.8f));
    check(flashes <= 3.0f, "nor flash");
}

void measuresInLinearLight() {
    Grid grid(kSide * kSide * 4, 255);
    for (int y = kSide / 2; y < kSide; y++) {
        for (int x = 0; x < kSide; x++) {
            uint8_t* px = grid.data() + (y * kSide + x) * 4;
            px[0] = px[1] = px[2] = 0;
        }
    }
    const FrameLight light = measureGrid(grid.data(), kSide);
    check(std::fabs(luminance(light.rgb[0]) - 0.5f) < 1e-4f, "half white half black is half the light");
    check(luminance(light.rgb[1]) > 0.999f && luminance(light.rgb[2]) > 0.999f, "the white quarters read white");
    check(luminance(light.rgb[3]) < 1e-4f && luminance(light.rgb[4]) < 1e-4f, "the black quarters read black");
    check(saturatedRed({1.0f, 0.0f, 0.0f}) == 1.0f && saturatedRed({1.0f, 0.5f, 0.0f}) == 0.0f, "only saturated red is red");
}

void counterFollowsWcag() {
    TransitionCounter c;
    int n = 0;
    for (float v : {1.0f, 0.85f, 1.0f, 0.85f, 1.0f}) n += c.step(v, 0.1f, 0.8f) ? 1 : 0;
    check(n == 0, "a swing between two bright levels is not a transition");
    n = 0;
    c.reset();
    for (float v : {1.0f, 0.85f, 0.5f}) n += c.step(v, 0.1f, 0.8f) ? 1 : 0;
    check(n == 1, "a bright swing that carries on into the dark counts once");
    n = 0;
    c.reset();
    for (int i = 0; i <= 120; i++) n += c.step(static_cast<float>(i) / 120.0f, 0.1f, 0.8f) ? 1 : 0;
    check(n == 1, "a fade is one transition, however long");
}

}  // namespace

int main() {
    measuresInLinearLight();
    counterFollowsWcag();
    strobe10HzIsLimited();
    redStrobeIsLimited();
    strobeInOneQuarterIsLimited();
    slowFadeAndBeatPassUntouched();
    letsGoSmoothlyAfterTheStrobe();
    if (failures > 0) {
        std::printf("%d flash limiter check(s) failed\n", failures);
        return 1;
    }
    std::printf("flash limiter: all checks passed\n");
    return 0;
}
