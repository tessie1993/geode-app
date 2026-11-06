// Host-side checks for the gradient palette (core/viz/Palette): sampling order and endpoints, the harmony
// helpers' hue offsets, the shell wire, and the palette sequence's beat cycle (steps exactly every N beats,
// crossfades continuously, and falls back to a free 120 BPM clock when the beat goes without a jump).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "viz/Palette.hpp"

using namespace geode::viz::palette;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

Gradient gradient(std::initializer_list<int32_t> colours) {
    Gradient g;
    for (int32_t c : colours) g.stops[static_cast<size_t>(g.count++)] = fromPacked(c);
    return g;
}

float angleDiff(float a, float b) {
    float d = std::fmod(a - b + 540.0f, 360.0f) - 180.0f;
    return d;
}

void gradientEndpointsAndOrder() {
    const Gradient g = gradient({0xFF0000, 0x00FF00, 0x0000FF});
    const Rgb a = sample(g, 0.0f), m = sample(g, 0.5f), z = sample(g, 1.0f);
    check(toPacked(a) == 0xFF0000, "t=0 is the first stop");
    check(toPacked(m) == 0x00FF00, "t=0.5 of three stops is the middle one");
    check(toPacked(z) == 0x0000FF, "t=1 is the last stop");
    check(toPacked(sample(g, -3.0f)) == 0xFF0000 && toPacked(sample(g, 9.0f)) == 0x0000FF, "t clamps to the ends");
    check(sample(Gradient{}, 0.5f).r == 0.0f, "an empty gradient is black");
    check(toPacked(sample(gradient({0x336699}), 0.7f)) == 0x336699, "one stop is flat");

    // Black to white: lightness only ever rises along the ramp (OKLab keeps the steps even).
    const Gradient bw = gradient({0x000000, 0xFFFFFF});
    float last = -1.0f;
    bool monotone = true;
    for (int i = 0; i <= 20; ++i) {
        const Rgb c = sample(bw, static_cast<float>(i) / 20.0f);
        if (c.r < last - 1e-4f) monotone = false;
        last = c.r;
    }
    check(monotone, "black to white rises monotonically");

    Baked baked;
    bake(g, baked);
    check(near(baked[0], 1.0f, 1e-3f) && near(baked[1], 0.0f, 1e-3f), "baked sample 0 is the first stop");
    const size_t end = static_cast<size_t>(kBakedSamples - 1) * 3;
    check(near(baked[end + 2], 1.0f, 1e-3f) && near(baked[end], 0.0f, 1e-3f), "the last baked sample is the last stop");
}

void harmonyHueOffsets() {
    const Rgb base = fromPacked(0x5A9E8C);
    const float h0 = oklchHue(base);
    Rgb out[3];
    int n = harmony(base, Harmony::Complementary, out);
    check(n == 1 && near(std::fabs(angleDiff(oklchHue(out[0]), h0)), 180.0f, 3.0f), "complementary turns 180 degrees");
    n = harmony(base, Harmony::Triad, out);
    check(n == 2 && near(angleDiff(oklchHue(out[0]), h0), 120.0f, 3.0f) && near(std::fabs(angleDiff(oklchHue(out[1]), h0)), 120.0f, 3.0f),
          "a triad turns 120 and 240");
    n = harmony(base, Harmony::Analogous, out);
    check(n == 2 && near(angleDiff(oklchHue(out[0]), h0), -30.0f, 3.0f) && near(angleDiff(oklchHue(out[1]), h0), 30.0f, 3.0f),
          "analogous turns -30 and +30");
    n = harmony(base, Harmony::SplitComplementary, out);
    check(n == 2 && near(angleDiff(oklchHue(out[0]), h0), 150.0f, 3.0f) && near(angleDiff(oklchHue(out[1]), h0), -150.0f, 3.0f),
          "split-complementary turns 150 and 210");
    // A vivid colour leaves gamut when turned; the hue must still hold.
    const Rgb vivid = fromPacked(0xFF2200);
    harmony(vivid, Harmony::Complementary, out);
    check(near(std::fabs(angleDiff(oklchHue(out[0]), oklchHue(vivid))), 180.0f, 6.0f), "a vivid colour keeps its turned hue");
    check(out[0].r >= 0.0f && out[0].r <= 1.0f && out[0].g >= 0.0f && out[0].g <= 1.0f && out[0].b >= 0.0f && out[0].b <= 1.0f,
          "turned colours stay in gamut");
}

void wireRoundTrip() {
    Wire w;
    w.gradient = gradient({0x112233, 0x445566});
    w.sequence.beats = 16;
    w.sequence.count = 2;
    w.sequence.palettes[0] = gradient({0xFF0000, 0x00FF00, 0x0000FF});
    w.sequence.palettes[1] = gradient({0xFFFFFF});
    int32_t buf[128];
    const int n = encodeWire(w, buf, 128);
    const Wire r = decodeWire(buf, n);
    check(r.gradient == w.gradient && r.sequence == w.sequence, "the wire round-trips");
    check(decodeWire(buf, 1).sequence.count == 0, "a short wire reads as no sequence");
    check(decodeWire(nullptr, 0).gradient.count == 0, "a null wire is empty");
    int32_t hostile[] = {99, 1, 2, 3, 99, 99};
    const Wire h = decodeWire(hostile, 6);
    check(h.gradient.count == kMaxStops && h.sequence.count >= 0 && h.sequence.count <= kMaxSequence, "counts clamp");
}

struct Beats {
    // A song at 120 BPM: the count rises at each beat, the phase ramps 0..1 between.
    double t = 0.0;
    float start = 5.0f;
    float conf = 0.9f;
    bool lost = false;
    double lostAt = 0.0, lostFor = 0.0;
    void frame(float dt, float& count, float& phase, float& c) {
        t += dt;
        const double beats = t * 2.0;
        count = std::fmod(start + static_cast<float>(std::floor(beats)), 4096.0f);
        phase = static_cast<float>(beats - std::floor(beats));
        c = (lostFor > 0.0 && t >= lostAt && t < lostAt + lostFor) ? 0.0f : conf;
    }
};

float maxDelta(const Baked& a, const Baked& b) {
    float m = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) m = std::max(m, std::fabs(a[i] - b[i]));
    return m;
}

Sequence threePalettes(int beats) {
    Sequence s;
    s.beats = beats;
    s.count = 3;
    s.palettes[0] = gradient({0xFF0000, 0xFF0000});
    s.palettes[1] = gradient({0x00FF00, 0x00FF00});
    s.palettes[2] = gradient({0x0000FF, 0x0000FF});
    return s;
}

void sequenceStepsEveryNBeats() {
    for (int beats : {4, 8, 16, 32}) {
        SequenceRunner run;
        run.set(threePalettes(beats));
        Beats song;
        std::vector<float> stepsAtCount;
        int last = 0;
        const float dt = 1.0f / 60.0f;
        float count = 0, phase = 0, conf = 0;
        for (int f = 0; f < 60 * 160; ++f) {
            song.frame(dt, count, phase, conf);
            run.step(count, phase, conf, dt);
            if (run.index() != last) {
                stepsAtCount.push_back(count + phase);
                last = run.index();
            }
        }
        check(stepsAtCount.size() >= 3, "the sequence keeps stepping");
        bool aligned = true, spaced = true;
        for (size_t i = 0; i < stepsAtCount.size(); ++i) {
            if (std::fabs(std::remainder(stepsAtCount[i], static_cast<float>(beats))) > 0.1f) aligned = false;
            if (i > 0 && std::fabs(stepsAtCount[i] - stepsAtCount[i - 1] - static_cast<float>(beats)) > 0.1f) spaced = false;
        }
        check(aligned, "steps land on multiples of the cycle (phrase boundaries at 16 and 32)");
        check(spaced, "steps are exactly N beats apart");
    }
}

void sequenceCrossfadesContinuously() {
    SequenceRunner run;
    run.set(threePalettes(4));
    Beats song;
    Baked prev = run.current();
    float worst = 0.0f;
    const float dt = 1.0f / 60.0f;
    float count, phase, conf;
    bool sawMid = false;
    for (int f = 0; f < 60 * 40; ++f) {
        song.frame(dt, count, phase, conf);
        run.step(count, phase, conf, dt);
        worst = std::max(worst, maxDelta(prev, run.current()));
        prev = run.current();
        if (run.mix() > 0.2f && run.mix() < 0.8f) sawMid = true;
    }
    check(worst < 0.06f, "no frame jumps: the crossfade is continuous");
    check(sawMid, "a step passes through a blend, not a cut");
}

void sequenceSurvivesBeatLoss() {
    SequenceRunner run;
    run.set(threePalettes(8));
    Beats song;
    song.lostAt = 20.0;
    song.lostFor = 25.0;
    Baked prev = run.current();
    float worst = 0.0f;
    const float dt = 1.0f / 60.0f;
    float count, phase, conf;
    std::vector<double> stepTimes;
    int last = 0;
    for (int f = 0; f < 60 * 80; ++f) {
        song.frame(dt, count, phase, conf);
        run.step(count, phase, conf, dt);
        worst = std::max(worst, maxDelta(prev, run.current()));
        prev = run.current();
        if (run.index() != last) {
            stepTimes.push_back(song.t);
            last = run.index();
        }
    }
    check(worst < 0.06f, "losing and regaining the beat never jumps the colour");
    int freeSteps = 0;
    for (size_t i = 1; i < stepTimes.size(); ++i) {
        if (stepTimes[i - 1] > song.lostAt + 1.0 && stepTimes[i] < song.lostAt + song.lostFor) {
            ++freeSteps;
            check(near(static_cast<float>(stepTimes[i] - stepTimes[i - 1]), 4.0f, 0.1f), "the free clock steps every N beats at 120 BPM");
        }
    }
    check(freeSteps >= 3, "the sequence keeps stepping with no beat");
    check(stepTimes.size() >= 8, "and keeps stepping after the beat returns");
}

void sequenceEdgeCases() {
    SequenceRunner run;
    check(!run.active(), "off by default");
    Sequence one;
    one.beats = 8;
    one.count = 1;
    one.palettes[0] = gradient({0x123456, 0x654321});
    run.set(one);
    for (int f = 0; f < 600; ++f) run.step(static_cast<float>(f / 30), 0.0f, 0.9f, 1.0f / 60.0f);
    check(run.index() == 0, "a single palette never steps");
    check(SequenceRunner::beatsBetween(4095.0f, 1.0f) == 2, "beat counts wrap at 4096");
    float count = NAN;
    run.step(count, 0.0f, 0.9f, 0.016f);
    check(std::isfinite(run.current()[0]), "a NaN beat count is survived");
    Sequence off = one;
    off.beats = 0;
    run.set(off);
    check(!run.active(), "beats = 0 turns the sequence off");
}

}  // namespace

int main() {
    gradientEndpointsAndOrder();
    harmonyHueOffsets();
    wireRoundTrip();
    sequenceStepsEveryNBeats();
    sequenceCrossfadesContinuously();
    sequenceSurvivesBeatLoss();
    sequenceEdgeCases();
    if (failures == 0) std::printf("palette_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
