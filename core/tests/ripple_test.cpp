// Host-side checks for the water ripples (core/viz/fluid/RippleMath): the drop list behind Customize's
// Water ripples overlay and the Water style. Built and run by CI ("Native host tests") with the system
// compiler; no GL.
//
// What is pinned: a ring runs out at a steady speed, loses height with distance and age and is exactly
// flat at its lifetime; with nothing added the energy falls on every step, at any frame rate; 30, 60 and
// 120 fps give the same surface; the list and the wavelets are bounded; an edge echoes a ring once,
// softly; music drops come from measured onsets only and never in silence; reduced motion scales the
// height by safety::kReducedMotionScale; a finger rings once where it lands and then by distance.
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "viz/VisualSafety.hpp"
#include "viz/fluid/RippleMath.hpp"

namespace rp = geode::viz::fluid::ripple;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

constexpr float kAspect = 0.5f;

float surfaceHeight(const rp::Surface& s, float x, float y, float gain = 1.0f) {
    rp::Wavelet w[rp::kMaxWavelets];
    const int n = s.wavelets(kAspect, gain, w);
    float h = 0.0f;
    for (int i = 0; i < n; ++i) h += rp::height(w[i], x, y);
    return h;
}

float maxHeight(const rp::Surface& s) {
    float m = 0.0f;
    for (int j = 0; j <= 80; ++j) {
        for (int i = 0; i <= 40; ++i) {
            m = std::max(m, std::fabs(surfaceHeight(s, -kAspect + 2.0f * kAspect * i / 40.0f, -1.0f + 2.0f * j / 80.0f)));
        }
    }
    return m;
}

void ringDiesWithinItsLifetime() {
    for (const float damping : {0.9f, 0.985f, 0.999f}) {
        rp::Surface s;
        const float life = rp::lifetimeFor(damping);
        check(life >= rp::kMinLifetime && life <= rp::kMaxLifetime, "Damping maps into the lifetime range");
        s.configure(rp::speedFor(1.0f), life);
        check(s.add(0.1f, -0.2f, 0.055f, 1.0f), "a drop is taken");
        const float dt = 1.0f / 60.0f;
        float t = 0.0f;
        while (t + dt < life - 0.05f) {
            s.step(dt);
            t += dt;
        }
        check(s.size() == 1 && s.energy() > 0.0f, "the ring is still there just before its lifetime");
        check(maxHeight(s) < 0.01f, "and almost flat by then");
        for (int i = 0; i < 8; ++i) s.step(dt);
        check(s.flat() && s.energy() == 0.0f, "the list is empty at the lifetime");
        check(maxHeight(s) == 0.0f, "and the surface exactly flat");
    }
    check(rp::lifetimeFor(0.999f) <= 4.0f, "the longest ring lasts four seconds");
    check(rp::envelope(3.0f, 3.0f) == 0.0f && rp::envelope(9.0f, 3.0f) == 0.0f && rp::envelope(0.0f, 3.0f) == 1.0f, "envelope ends at 0");
}

void energyOnlyFalls() {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (const float fps : {30.0f, 60.0f, 120.0f, 0.0f}) {
        rp::Surface s;
        s.configure(rp::speedFor(1.3f), rp::lifetimeFor(0.999f));
        for (int i = 0; i < 9; ++i) s.add((u(rng) - 0.5f), u(rng) * 2.0f - 1.0f, 0.03f + 0.08f * u(rng), u(rng) * 2.0f - 1.0f);
        check(s.size() >= 6, "the drops landed");
        float before = s.energy();
        bool fell = true;
        float t = 0.0f;
        while (!s.flat() && t < 10.0f) {
            const float dt = fps > 0.0f ? 1.0f / fps : 0.002f + 0.05f * u(rng);
            s.step(dt);
            t += dt;
            const float now = s.energy();
            if (!(now < before)) fell = false;
            before = now;
        }
        check(fell, "energy falls on every step with nothing added");
        check(s.flat() && t <= rp::kMaxLifetime + 0.1f, "and reaches zero within the longest lifetime");
        s.step(0.0f);
        check(s.energy() == 0.0f, "a flat surface stays flat");
    }
}

void heightStaysUnderTheCrest() {
    rp::Surface s;
    s.configure(rp::speedFor(1.0f), rp::lifetimeFor(0.985f));
    s.add(0.0f, 0.0f, 0.055f, 0.8f);
    check(maxHeight(s) < 0.08f * 0.8f, "a drop wells up: nearly flat on the frame it lands");
    bool under = true;
    float last = rp::peak(s.drop(0));
    bool crestFalls = true;
    bool seen = false;
    while (!s.flat()) {
        const float crest = rp::peak(s.drop(0));
        if (crest > last) crestFalls = false;
        last = crest;
        const float h = maxHeight(s);
        if (h > crest * (1.0f + rp::kEdges * rp::kReflection) + 1e-5f) under = false;
        if (h > 0.2f) seen = true;
        s.step(1.0f / 60.0f);
    }
    check(seen, "the ring shows");
    check(under, "the surface never stands taller than the crest and its echoes");
    check(crestFalls, "the crest only loses height");
    for (float x = -5.0f; x <= 3.0f; x += 0.01f) {
        if (std::fabs(rp::profile(x)) > 1.0f) under = false;
        const float numeric = (rp::profile(x + 1e-2f) - rp::profile(x - 1e-2f)) / 2e-2f;
        if (!near(numeric, rp::profileSlope(x), 0.05f)) under = false;
    }
    check(under, "the cross-section stays within 1 and its slope is its derivative");
}

void steadySpeedAndDistanceLoss() {
    rp::Surface s;
    const float c = rp::speedFor(1.0f);
    s.configure(c, rp::lifetimeFor(0.999f));
    s.add(0.0f, 0.0f, 0.055f, 1.0f);
    const float start = s.drop(0).front;
    for (int i = 0; i < 60; ++i) s.step(1.0f / 60.0f);
    const float afterOne = s.drop(0).front;
    for (int i = 0; i < 120; ++i) s.step(1.0f / 60.0f);
    check(near(afterOne - start, c, 1e-3f) && near(s.drop(0).front - afterOne, 2.0f * c, 1e-3f), "the front runs at one steady speed");
    check(rp::spread(0.0f, 0.1f) == 1.0f && rp::spread(0.5f, 0.1f) < rp::spread(0.2f, 0.1f) && rp::spread(0.2f, 0.1f) < 1.0f, "height falls with distance");
    check(rp::speedFor(2.0f) == 2.0f * rp::speedFor(1.0f) && rp::speedFor(99.0f) == rp::speedFor(2.0f), "Wave speed scales the speed, clamped");
}

void sameAtEveryFrameRate() {
    const float fpsList[3] = {30.0f, 60.0f, 120.0f};
    rp::Surface s[3];
    for (auto& x : s) x.configure(rp::speedFor(0.8f), rp::lifetimeFor(0.985f));
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    bool same = true;
    for (int tick = 0; tick < 30 * 8; ++tick) {
        if (tick < 120 && tick % 7 == 0) {
            const float x = u(rng) - 0.5f, y = u(rng) * 2.0f - 1.0f, r = 0.04f + 0.07f * u(rng), a = 0.3f + 0.7f * u(rng);
            const bool took = s[0].add(x, y, r, a);
            if (s[1].add(x, y, r, a) != took || s[2].add(x, y, r, a) != took) same = false;
        }
        for (int k = 0; k < 3; ++k) {
            const int sub = static_cast<int>(fpsList[k] / 30.0f);
            for (int i = 0; i < sub; ++i) s[k].step(1.0f / fpsList[k]);
        }
        if (s[0].size() != s[1].size() || s[0].size() != s[2].size()) same = false;
        if (!near(s[0].energy(), s[1].energy(), 1e-3f) || !near(s[0].energy(), s[2].energy(), 1e-3f)) same = false;
        for (int p = 0; p < 12; ++p) {
            const float x = -0.45f + 0.08f * p, y = -0.9f + 0.16f * p;
            const float h = surfaceHeight(s[0], x, y);
            if (!near(h, surfaceHeight(s[1], x, y), 2e-3f) || !near(h, surfaceHeight(s[2], x, y), 2e-3f)) same = false;
        }
    }
    check(same, "30, 60 and 120 fps draw the same surface");
    check(s[0].flat() && s[1].flat() && s[2].flat(), "and all end flat");
}

void listIsBounded() {
    rp::Surface s;
    s.configure(rp::speedFor(1.0f), rp::lifetimeFor(0.999f));
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    rp::Wavelet w[rp::kMaxWavelets];
    bool bounded = true;
    for (int i = 0; i < 4000; ++i) {
        s.add(u(rng) - 0.5f, u(rng) * 2.0f - 1.0f, 0.02f + 0.1f * u(rng), 4.0f * (u(rng) - 0.5f));
        s.step(1.0f / 120.0f);
        if (s.size() > rp::kMaxDrops || s.wavelets(kAspect, 1.0f, w) > rp::kMaxWavelets) bounded = false;
        for (int k = 0; k < s.size(); ++k) {
            if (std::fabs(s.drop(k).amplitude) > rp::kMaxAmplitude) bounded = false;
        }
    }
    check(bounded, "never more than kMaxDrops drops, kMaxWavelets wavelets or kMaxAmplitude height");
    check(s.energy() <= static_cast<float>(rp::kMaxDrops) * rp::kMaxAmplitude * rp::kMaxAmplitude, "so the energy has a ceiling");

    rp::Surface held;
    held.configure(rp::speedFor(1.0f), rp::lifetimeFor(0.985f));
    int taken = 0;
    for (int i = 0; i < 300; ++i) {
        if (held.add(0.1f, 0.1f, 0.055f, 0.5f)) taken++;
        held.step(1.0f / 60.0f);
    }
    check(taken >= 5 && taken <= 21, "a source pouring every frame on one spot rings a few times a second, not every frame");
    check(!s.add(0.0f, 0.0f, 0.05f, 0.0f) && !s.add(NAN, 0.0f, 0.05f, 1.0f), "no drop from nothing or from a non-finite sample");
}

void edgesEchoOnceAndSoftly() {
    rp::Surface s;
    s.configure(rp::speedFor(1.0f), rp::lifetimeFor(0.999f));
    s.add(0.3f, 0.0f, 0.055f, 1.0f);
    rp::Wavelet w[rp::kMaxWavelets];
    check(s.wavelets(kAspect, 1.0f, w) == 1, "no echo before the ring reaches an edge");
    int most = 0;
    bool soft = true;
    bool mirrored = true;
    while (!s.flat()) {
        const int n = s.wavelets(kAspect, 1.0f, w);
        most = std::max(most, n);
        for (int i = 1; i < n; ++i) {
            if (!near(w[i].amplitude, w[0].amplitude * rp::kReflection, 1e-6f)) soft = false;
            const bool outside = std::fabs(w[i].x) > kAspect || std::fabs(w[i].y) > 1.0f;
            if (!outside || w[i].front != w[0].front) mirrored = false;
        }
        s.step(1.0f / 30.0f);
    }
    check(most > 1 && most <= 1 + rp::kEdges, "each edge gives one echo and an echo gives none");
    check(soft && rp::kReflection < 0.5f, "an echo is a soft copy of its ring");
    check(mirrored, "and is the ring mirrored in that edge");
}

GeodeSoundFrame playing(bool onset) {
    GeodeSoundFrame f{};
    f.presence = 1.0f;
    f.onset = onset ? 1.0f : 0.0f;
    f.onsetEnv = onset ? 0.8f : 0.3f;
    f.bassAtt = 1.4f;
    f.trebleAtt = 2.5f;
    return f;
}

void musicDropsComeFromOnsetsOnly() {
    rp::OnsetDrops drops;
    int n = 0;
    for (int i = 0; i < 3600; ++i) n += drops.step(playing(false), kAspect).drop ? 1 : 0;
    check(n == 0, "a minute of loud music without an onset drops nothing: no clock of its own");
    std::mt19937 rng(5);
    int onsets = 0;
    bool inside = true;
    for (int i = 0; i < 3600; ++i) {
        const bool onset = rng() % 23 == 0;
        onsets += onset ? 1 : 0;
        const auto out = drops.step(playing(onset), kAspect);
        if (out.drop != onset) inside = false;
        if (out.drop && (std::fabs(out.x) > kAspect || std::fabs(out.y) > 1.0f || !(out.amplitude > 0.0f) || out.amplitude > rp::kMaxAmplitude)) inside = false;
    }
    check(onsets > 50 && inside, "one drop per measured onset, on the surface, of bounded height");
    GeodeSoundFrame silent = playing(true);
    silent.presence = 0.0f;
    check(!drops.step(silent, kAspect).drop, "silence drops nothing, even on an onset");
    GeodeSoundFrame fading = playing(true);
    fading.presence = 0.2f;
    const auto faint = drops.step(fading, kAspect);
    const auto full = drops.step(playing(true), kAspect);
    check(faint.drop && faint.amplitude < full.amplitude, "a fading song drops lower rings (sound::motionRate)");
}

void reducedMotionScalesTheHeight() {
    rp::Surface s;
    s.configure(rp::speedFor(1.0f), rp::lifetimeFor(0.985f));
    s.add(0.0f, 0.0f, 0.055f, 1.0f);
    for (int i = 0; i < 40; ++i) s.step(1.0f / 60.0f);
    rp::Wavelet whole[rp::kMaxWavelets];
    rp::Wavelet reduced[rp::kMaxWavelets];
    const int n = s.wavelets(kAspect, 1.0f, whole);
    check(n == s.wavelets(kAspect, geode::viz::safety::kReducedMotionScale, reduced), "the same rings");
    bool scaled = n > 0;
    for (int i = 0; i < n; ++i) {
        if (!near(reduced[i].amplitude, whole[i].amplitude * geode::viz::safety::kReducedMotionScale, 1e-6f)) scaled = false;
    }
    check(scaled, "reduced motion draws them at safety::kReducedMotionScale");
}

struct Taken {
    std::vector<float> amps;
};
void take(void* ctx, float, float, float amplitude) { static_cast<Taken*>(ctx)->amps.push_back(amplitude); }

void fingersRingOnceThenByDistance() {
    rp::Wake wake;
    Taken t;
    float xy[4] = {0.1f, 0.2f, 0.0f, 0.0f};
    wake.step(xy, 1, 1.0f / 60.0f, &t, take);
    check(t.amps.size() == 1, "a finger rings where it lands");
    for (int i = 0; i < 600; ++i) wake.step(xy, 1, 1.0f / 60.0f, &t, take);
    check(t.amps.size() == 1, "a finger held still for ten seconds rings no more");
    wake.step(xy, 0, 1.0f / 60.0f, &t, take);
    wake.step(xy, 1, 1.0f / 60.0f, &t, take);
    check(t.amps.size() == 2, "lifting and landing again rings again");

    size_t counts[2] = {0, 0};
    const float fpsList[2] = {30.0f, 120.0f};
    for (int k = 0; k < 2; ++k) {
        rp::Wake w;
        Taken path;
        const int frames = static_cast<int>(fpsList[k]) * 2;
        for (int i = 0; i <= frames; ++i) {
            float p[2] = {-0.4f + 0.8f * static_cast<float>(i) / static_cast<float>(frames), 0.0f};
            w.step(p, 1, 1.0f / fpsList[k], &path, take);
        }
        counts[k] = path.amps.size();
        for (float a : path.amps) check(a > 0.0f && a <= rp::kMaxAmplitude, "a wake's rings are bounded");
    }
    check(counts[0] == counts[1] && counts[0] >= 3 && counts[0] <= 6, "a drag rings by distance, the same at 30 and 120 fps");
}

void heightRateIsTheTimeDerivative() {
    const rp::Wavelet w{0.0f, 0.0f, 0.4f, 0.5f, 1.0f / 0.12f};
    const float c = 0.6f;
    bool ok = true;
    for (float x = 0.05f; x < 0.7f; x += 0.013f) {
        const float eps = 1e-3f;
        rp::Wavelet later = w;
        later.front += c * eps;
        rp::Wavelet earlier = w;
        earlier.front -= c * eps;
        const float numeric = (rp::height(later, x, 0.02f) - rp::height(earlier, x, 0.02f)) / (2.0f * eps);
        if (!near(numeric, rp::heightRate(w, x, 0.02f, c), 0.05f)) ok = false;
    }
    check(ok, "the G channel is the height's rate of change as the ring runs");
}

// --dump <tap|touch|music> <fps> <seconds> <aspect>: the wavelets of every frame as TSV (time, speed,
// energy, count, then x y front amplitude 1/wavelength each), for tools/glslcheck --ripples to draw.
int dump(const char* scenario, float fps, float seconds, float aspect) {
    const std::string name = scenario;
    rp::Surface s;
    s.configure(rp::speedFor(1.0f), rp::lifetimeFor(0.985f));
    rp::Wake wake;
    rp::OnsetDrops onsets;
    const float onsetTimes[] = {0.3f, 0.72f, 0.95f, 1.6f, 1.83f, 2.9f, 3.05f, 3.4f};
    size_t nextOnset = 0;
    const float dt = 1.0f / fps;
    const int frames = static_cast<int>(seconds * fps);
    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) * dt;
        s.step(dt);
        float xy[2] = {0.3f * aspect, 0.3f};
        int fingers = 0;
        if (name == "tap") fingers = t >= 0.2f && t < 0.3f ? 1 : 0;
        if (name == "touch" && t >= 0.2f && t < 1.6f) {
            const float u = std::clamp((t - 0.5f) / 1.0f, 0.0f, 1.0f);
            xy[0] = aspect * (0.3f - 0.9f * u);
            xy[1] = 0.3f - 0.8f * u * u;
            fingers = 1;
        }
        wake.step(xy, fingers, dt, &s, [](void* ctx, float x, float y, float a) {
            static_cast<rp::Surface*>(ctx)->add(x, y, rp::kTouchRadius, a);
        });
        if (name == "music") {
            const bool onset = nextOnset < std::size(onsetTimes) && t >= onsetTimes[nextOnset];
            if (onset) nextOnset++;
            const auto d = onsets.step(playing(onset), aspect);
            if (d.drop) s.add(d.x, d.y, d.radius, d.amplitude);
        }
        rp::Wavelet w[rp::kMaxWavelets];
        const int n = s.wavelets(aspect, 1.0f, w);
        std::printf("%.5f\t%.5f\t%.6f\t%d", t, s.speed(), s.energy(), n);
        for (int k = 0; k < n; ++k) std::printf("\t%.5f\t%.5f\t%.5f\t%.6f\t%.5f", w[k].x, w[k].y, w[k].front, w[k].amplitude, w[k].invWavelength);
        std::printf("\n");
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 6 && std::string(argv[1]) == "--dump") return dump(argv[2], std::stof(argv[3]), std::stof(argv[4]), std::stof(argv[5]));
    ringDiesWithinItsLifetime();
    energyOnlyFalls();
    heightStaysUnderTheCrest();
    steadySpeedAndDistanceLoss();
    sameAtEveryFrameRate();
    listIsBounded();
    edgesEchoOnceAndSoftly();
    musicDropsComeFromOnsetsOnly();
    reducedMotionScalesTheHeight();
    fingersRingOnceThenByDistance();
    heightRateIsTheTimeDerivative();
    if (failures == 0) std::printf("ripple_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
