// Host checks for the Pool style's surface (core/viz/pool/PoolMath), the sums pool_frag draws. Built and
// run by CI ("Native host tests" in .github/workflows/native-host-tests.yml); no GL.
//
// Pinned: the caustics' whole swing fits one safety step and nothing on the surface repeats faster than
// the flash limit, at any control, finger or sound gain; the slope and curvature pool_frag reads are the
// height's true derivatives; silence holds the water still and a touch in it leaves nothing; 30, 60 and
// 120 fps give the same water; a wake rises once and then only falls; reduced motion slows it; every
// control Customize offers moves something.
//
// `pool_test --dump` prints the uniforms of a frame mid-song for tools/glslcheck (--set).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

#include "viz/VisualSafety.hpp"
#include "viz/pool/PoolMath.hpp"

using namespace geode::viz::pool;
namespace safety = geode::viz::safety;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr float kAspect = 9.0f / 19.5f;
constexpr Gains kLoud{1.0f, 1.0f, 1.0f};

Controls wild(std::mt19937& rng) {
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    Controls c;
    c.morph = u(rng);
    c.warp = u(rng);
    c.turbulence = 1.5f * u(rng);
    c.intensity = 0.2f + 1.8f * u(rng);
    c.speed = kMaxSpeed * u(rng);
    for (float& k : c.knobs) k = u(rng);
    return c;
}

Pool run(float fps, float seconds, const Controls& c, float touchAt = -1.0f, float x = 0.2f, float y = -0.3f) {
    Pool pool;
    const int frames = static_cast<int>(std::lround(seconds * fps));
    const float finger[2] = {x, y};
    for (int i = 0; i < frames; i++) {
        const bool down = touchAt >= 0.0f && static_cast<float>(i) / fps >= touchAt - 1.0e-4f;
        pool.step(1.0f / fps, c, finger, down ? 1 : 0, kAspect);
    }
    return pool;
}

// The water under the screen, sampled where the eye meets it.
template <typename Fn>
void overScreen(const View& v, Fn&& fn) {
    for (int iy = 0; iy <= 30; iy++) {
        for (int ix = 0; ix <= 14; ix++) {
            const float sx = kAspect * (static_cast<float>(ix) / 7.0f - 1.0f);
            const float sy = static_cast<float>(iy) / 15.0f - 1.0f;
            if (const auto p = surfacePoint(v, sx, sy)) fn((*p)[0], (*p)[1]);
        }
    }
}

void causticsFitOneSafetyStep() {
    check(kCausticSwing <= safety::kMaxFlashDepth + 1.0e-6f, "the caustics' designed swing is one safety step");
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    float lo = 0.0f;
    float hi = 0.0f;
    float shift = 0.0f;
    for (int trial = 0; trial < 40; trial++) {
        const Controls c = wild(rng);
        Pool pool;
        for (int f = 0; f < 240; f++) {
            float fingers[4] = {2.0f * u(rng) - 1.0f, 2.0f * u(rng) - 1.0f, 2.0f * u(rng) - 1.0f, 2.0f * u(rng) - 1.0f};
            pool.step(1.0f / 60.0f, c, fingers, f % 40 < 20 ? 2 : 0, kAspect);
        }
        const Surface s = pool.surface(c);
        check(s.look.caustics <= kCausticCeiling + 1.0e-6f, "the caustics never pass their ceiling");
        const Gains g = trial % 2 ? kLoud : Gains{u(rng), u(rng), u(rng)};
        overScreen(s.view, [&](float x, float y) {
            const float l = causticLuma(s, g, x, y);
            lo = std::min(lo, l);
            hi = std::max(hi, l);
            const auto r = refraction(s, g, x, y);
            shift = std::max(shift, std::hypot(r[0], r[1]));
        });
    }
    check(hi <= kCausticCeiling + 1.0e-5f, "the brightest caustic is the ceiling");
    check(lo >= -kCausticShade * kCausticCeiling - 1.0e-5f, "the darkest is the shade");
    check(hi - lo <= safety::kMaxFlashDepth + 1.0e-5f, "between them, one safety step");
    check(hi > 0.5f * kCausticCeiling, "and the light really gathers");
    check(shift <= kMaxRefraction + 1.0e-6f && shift > 0.2f * kMaxRefraction, "the floor bends, up to its cap");
}

void nothingRepeatsFasterThanTheFlashLimit() {
    check(fastestHz(kMaxSpeed) < safety::kWcagFlashesPerSecond, "at the top Speed every term repeats under 3 Hz");
    check(fastestHz(1.0f) < 1.0f, "and at Speed 1 under once a second");
    check(std::isfinite(fastestHz(NAN)) && fastestHz(100.0f) == fastestHz(kMaxSpeed), "Speed is held to its range");
}

void derivativesAreTheHeights() {
    Controls c;
    c.turbulence = 1.0f;
    c.warp = 0.6f;
    Pool pool = run(60.0f, 3.0f, c, 0.5f);
    const Surface s = pool.surface(c);
    check(pool.wakeCount() == 1, "the test's finger left a wake");
    const float e = 1.0e-3f;
    float worstG = 0.0f;
    float worstH = 0.0f;
    float scaleG = 0.0f;
    float scaleH = 0.0f;
    overScreen(s.view, [&](float x, float y) {
        const auto g = gradient(s, kLoud, x, y);
        const float gx = (height(s, kLoud, x + e, y) - height(s, kLoud, x - e, y)) / (2.0f * e);
        const float gy = (height(s, kLoud, x, y + e) - height(s, kLoud, x, y - e)) / (2.0f * e);
        worstG = std::max({worstG, std::fabs(g[0] - gx), std::fabs(g[1] - gy)});
        scaleG = std::max({scaleG, std::fabs(gx), std::fabs(gy)});
        const Hessian h = hessian(s, kLoud, x, y);
        const auto px = gradient(s, kLoud, x + e, y);
        const auto mx = gradient(s, kLoud, x - e, y);
        const auto py = gradient(s, kLoud, x, y + e);
        const auto my = gradient(s, kLoud, x, y - e);
        const float hxx = (px[0] - mx[0]) / (2.0f * e);
        const float hxy = (py[0] - my[0]) / (2.0f * e);
        const float hyy = (py[1] - my[1]) / (2.0f * e);
        worstH = std::max({worstH, std::fabs(h.xx - hxx), std::fabs(h.xy - hxy), std::fabs(h.yy - hyy)});
        scaleH = std::max({scaleH, std::fabs(hxx), std::fabs(hyy)});
    });
    check(worstG <= 2.0e-3f * std::max(scaleG, 1.0f), "the slope is the height's gradient");
    check(worstH <= 2.0e-2f * std::max(scaleH, 1.0f), "the curvature is the slope's gradient");
}

void silenceHoldsTheWaterStill() {
    Controls c;
    Pool pool = run(60.0f, 2.0f, c);
    const Surface before = pool.surface(c);
    const float finger[2] = {0.0f, 0.0f};
    for (int f = 0; f < 120; f++) pool.step(0.0f, c, finger, 1, kAspect);
    const Surface after = pool.surface(c);
    bool same = true;
    for (int i = 0; i < kWaves; i++) same = same && before.waves[i].phase == after.waves[i].phase;
    for (int i = 0; i < kRings; i++) same = same && before.rings[i].phase == after.rings[i].phase && before.rings[i].x == after.rings[i].x;
    check(same, "a stopped clock moves no wave and no ring");
    check(pool.wakeCount() == 0, "a touch in silence drops no wake to appear later");
    const float spike[2] = {0.1f, 0.1f};
    pool.step(1.0f / 60.0f, c, spike, 1, kAspect);
    check(pool.wakeCount() == 0, "a finger already down when the music returns is not a new touch");
}

void frameRateDoesNotChangeTheWater() {
    Controls c;
    c.speed = 1.7f;
    const Surface a = run(30.0f, 4.0f, c, 1.0f).surface(c);
    const Surface b = run(60.0f, 4.0f, c, 1.0f).surface(c);
    const Surface d = run(120.0f, 4.0f, c, 1.0f).surface(c);
    float worst = 0.0f;
    for (int i = 0; i < kWaves; i++) {
        worst = std::max({worst, std::fabs(a.waves[i].phase - d.waves[i].phase), std::fabs(b.waves[i].phase - d.waves[i].phase)});
    }
    for (int i = 0; i < kRings; i++) {
        worst = std::max({worst, std::fabs(a.rings[i].phase - d.rings[i].phase), std::fabs(a.rings[i].x - d.rings[i].x)});
    }
    check(worst < 1.0e-3f, "waves and rings are where they are at any fps");
    float wake = 0.0f;
    for (int i = 0; i < kMaxWakes; i++) wake = std::max(wake, std::fabs(a.wakes[i].amp - d.wakes[i].amp));
    check(wake < 0.05f * kWakeHeight, "a wake's height does not depend on the fps either");
}

void aWakeRisesOnceThenOnlyFalls() {
    float peak = 0.0f;
    float last = 0.0f;
    bool fell = false;
    bool rose = false;
    for (int f = 0; f <= 400; f++) {
        const float a = wakeAmp(1.0f, static_cast<float>(f) / 60.0f);
        if (a < last - 1.0e-7f) fell = true;
        if (fell && a > last + 1.0e-7f) rose = true;
        peak = std::max(peak, a);
        last = a;
    }
    check(!rose, "after its peak a wake never grows back");
    check(peak <= kWakeHeight && peak > 0.5f * kWakeHeight, "its peak is near the ceiling");
    check(wakeAmp(1.0f, kWakeLifeSeconds) == 0.0f && wakeAmp(1.0f, -1.0f) == 0.0f, "it lives kWakeLifeSeconds");

    Controls c;
    check(run(60.0f, 3.0f, c, 0.2f).wakeCount() == 1, "a held finger leaves one wake");
    check(run(60.0f, 6.0f, c, -1.0f).wakeCount() == 0, "no finger, no wake");
    Pool drag;
    for (int f = 0; f < 60; f++) {
        const float finger[2] = {-0.8f + 1.6f * static_cast<float>(f) / 60.0f, 0.0f};
        drag.step(1.0f / 60.0f, c, finger, 1, kAspect);
    }
    check(drag.wakeCount() >= 3, "a dragged finger leaves a trail of them");
    Pool many;
    for (int f = 0; f < 600; f++) {
        const float finger[2] = {std::sin(static_cast<float>(f) * 0.3f), std::cos(static_cast<float>(f) * 0.2f)};
        many.step(1.0f / 60.0f, c, finger, f % 2, kAspect);
    }
    check(many.wakeCount() <= kMaxWakes, "the wakes are a fixed handful");
}

void reducedMotionArrivesThroughSpeedAndTurbulence() {
    // safety::apply scales Speed and Turbulence under reduced motion; the water follows both, once.
    Controls c;
    c.turbulence = 1.0f;
    Controls calm = c;
    calm.speed *= safety::kReducedMotionScale;
    calm.turbulence *= safety::kReducedMotionScale;
    const Surface a = run(60.0f, 2.0f, c).surface(c);
    const Surface b = run(60.0f, 2.0f, calm).surface(calm);
    check(std::fabs(b.waves[0].phase - safety::kReducedMotionScale * a.waves[0].phase) < 1.0e-4f, "the waves run slower by the factor");
    check(b.waves[kWaves - 1].amp < a.waves[kWaves - 1].amp, "and the chop is lower");
}

void everyControlMovesSomething() {
    const Controls base;
    const Surface s = Pool().surface(base);
    const auto moves = [&](Controls c) {
        const Surface t = Pool().surface(c);
        bool diff = std::memcmp(&s.look, &t.look, sizeof(Look)) != 0 || std::memcmp(&s.view, &t.view, sizeof(View)) != 0;
        for (int i = 0; i < kWaves; i++) diff = diff || s.waves[i].amp != t.waves[i].amp;
        for (int i = 0; i < kRings; i++) diff = diff || s.rings[i].amp != t.rings[i].amp;
        return diff;
    };
    Controls c = base;
    c.morph = 1.0f;
    check(moves(c) && viewFor(c).sinTilt < viewFor(base).sinTilt, "Morph turns the view toward straight down");
    c = base;
    c.warp = 1.0f;
    check(moves(c) && Pool().surface(c).look.refraction > s.look.refraction, "Warp deepens the water");
    c = base;
    c.turbulence = 1.5f;
    check(moves(c) && Pool().surface(c).waves[kWaves - 1].amp > s.waves[kWaves - 1].amp, "Turbulence raises the chop");
    c = base;
    c.intensity = 2.0f;
    check(moves(c) && Pool().surface(c).look.caustics > s.look.caustics, "Intensity brightens the caustics");
    for (int k = 0; k < 6; k++) {
        if (k == 3) continue;  // k3 TONE is the tiles' hue, which pool_frag reads itself
        c = base;
        c.knobs[static_cast<size_t>(k)] = 1.0f;
        char what[48];
        std::snprintf(what, sizeof what, "knob %d moves the water", k);
        check(moves(c), what);
    }
}

void theEyeMeetsTheWater() {
    const View v = viewFor(Controls{});
    const auto centre = surfacePoint(v, 0.0f, 0.0f);
    check(centre && std::fabs((*centre)[1] - kEyeHeight * v.sinTilt / v.cosTilt) < 1.0e-4f, "the screen's centre is where the eye looks");
    const auto near = surfacePoint(v, 0.0f, -1.0f);
    check(near && (*near)[1] < (*centre)[1], "the bottom of the screen is the near end");
    Controls slant;
    slant.knobs[5] = 1.0f;
    check(!surfacePoint(viewFor(slant), 0.0f, 1.0f), "at the lowest slant the far wall gives way to sky");
    check(!surfacePoint(v, 0.0f, NAN), "a broken point meets nothing");
}

void brokenInputsStayFinite() {
    Controls c;
    c.morph = NAN;
    c.warp = INFINITY;
    c.speed = NAN;
    c.intensity = NAN;
    c.knobs[0] = NAN;
    Pool pool;
    const float finger[2] = {NAN, 0.0f};
    pool.step(NAN, c, finger, 1, NAN);
    pool.step(1.0f / 60.0f, c, nullptr, 3, 1.0f);
    const Surface s = pool.surface(c);
    bool finite = std::isfinite(s.look.caustics) && std::isfinite(s.look.refraction) && std::isfinite(s.view.sinTilt);
    for (const Wave& w : s.waves) finite = finite && std::isfinite(w.amp) && std::isfinite(w.phase);
    check(finite && std::isfinite(causticLuma(s, kLoud, 0.0f, 1.0f)), "nothing non-finite reaches the shader");
}

void dump() {
    Controls c;
    const Surface s = run(60.0f, 7.0f, c, 6.6f).surface(c);
    std::printf("--set uPoolWaves=");
    for (int i = 0; i < kWaves; i++) {
        const Wave& w = s.waves[i];
        std::printf("%s%g,%g,%g,%g", i ? "," : "", w.kx, w.ky, w.phase, w.amp);
    }
    std::printf(" --set uPoolRings=");
    for (int i = 0; i < kRings; i++) {
        const Ring& r = s.rings[i];
        std::printf("%s%g,%g,%g,%g", i ? "," : "", r.x, r.y, r.phase, r.amp);
    }
    std::printf(" --set uPoolWakes=");
    for (int i = 0; i < kMaxWakes; i++) {
        const Wake& w = s.wakes[i];
        std::printf("%s%g,%g,%g,%g", i ? "," : "", w.x, w.y, w.sigma, w.amp);
    }
    std::printf(" --set uPoolLook=%g,%g,%g,%g", s.look.refraction, s.look.focus, s.look.caustics, s.look.shade);
    std::printf(" --set uPoolView=%g,%g,%g,%g\n", s.view.sinTilt, s.view.cosTilt, s.view.height, s.view.fov);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--dump") == 0) {
        dump();
        return 0;
    }
    causticsFitOneSafetyStep();
    nothingRepeatsFasterThanTheFlashLimit();
    derivativesAreTheHeights();
    silenceHoldsTheWaterStill();
    frameRateDoesNotChangeTheWater();
    aWakeRisesOnceThenOnlyFalls();
    reducedMotionArrivesThroughSpeedAndTurbulence();
    everyControlMovesSomething();
    theEyeMeetsTheWater();
    brokenInputsStayFinite();
    if (failures == 0) std::printf("pool_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
