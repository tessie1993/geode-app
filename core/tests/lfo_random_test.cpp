// Host-side checks for "Random on waves": the two random wave shapes of core/viz/Lfo (Random steps, Random
// glide) and the roll of a whole set of waves (core/viz/WaveRandomizer). No GL.
//
// The shapes: a new value each cycle, held in between, the same at 30, 60 and 120 fps, different per
// slot, set by the engine's seed, one step per cycle when the music clock jitters over a boundary, no
// dead cycle after a switch between the two, and a beat-timed wave with no measured tempo runs on its
// own period (never on an assumed tempo).
// The roll: targets the style reads and the slot may move, spread over different kinds of control,
// periods and depths in a useful range, luminance kept slow and smooth, same seed same waves.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "viz/Catalogue.hpp"
#include "viz/Lfo.hpp"
#include "viz/ModTargets.hpp"
#include "viz/VisualSafety.hpp"
#include "viz/WaveRandomizer.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

LfoConfig wave(LfoWave shape, float rateSeconds, float beats = 0.0f, LfoTarget target = LfoTarget::Zoom) {
    LfoConfig c;
    c.enabled = true;
    c.target = target;
    c.wave = shape;
    c.rateSeconds = rateSeconds;
    c.depth = 1.0f;
    c.beats = beats;
    return c;
}

GeodeSoundFrame clockAt(double beats, float bpm) {
    GeodeSoundFrame f{};
    f.bpm = bpm;
    f.beatConf = 1.0f;
    f.beatCount = static_cast<float>(std::fmod(std::floor(beats), 4096.0));
    f.beatPhase = static_cast<float>(beats - std::floor(beats));
    return f;
}

// Slot `slot`'s output sampled in the middle of each `period`-second window of a free run.
std::vector<float> midCycle(LfoEngine& e, int slot, float fps, float period, int cycles) {
    std::vector<float> out;
    const GeodeSoundFrame quiet{};
    const int perCycle = static_cast<int>(std::lround(period * fps));
    for (int k = 0; k < cycles; ++k) {
        for (int f = 0; f < perCycle; ++f) {
            const float v = e.tick(1.0f / fps, quiet)[static_cast<size_t>(slot)];
            if (f == perCycle / 2) out.push_back(v);
        }
    }
    return out;
}

void stepsHoldAndChangeEachCycle() {
    LfoEngine e;
    e.configs[0] = wave(LfoWave::Random, 2.0f);
    const GeodeSoundFrame quiet{};
    std::vector<float> values;
    float last = e.tick(1.0f / 60.0f, quiet)[0];
    int changes = 0;
    for (int i = 1; i < 60 * 80; ++i) {  // 40 cycles
        const float v = e.tick(1.0f / 60.0f, quiet)[0];
        if (v != last) {
            ++changes;
            values.push_back(v);
        }
        last = v;
    }
    check(changes >= 38 && changes <= 40, "random steps: one new value per cycle, held in between (" + std::to_string(changes) + ")");
    float lo = 1.0f, hi = -1.0f;
    for (float v : values) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
        check(v >= -1.0f && v <= 1.0f, "random steps stay in -1..1");
    }
    check(lo < -0.6f && hi > 0.6f, "random steps use the whole swing");
}

void sameAtEveryFrameRate() {
    for (const LfoWave shape : {LfoWave::Random, LfoWave::RandomGlide}) {
        std::vector<std::vector<float>> runs;
        for (const float fps : {30.0f, 60.0f, 120.0f}) {
            LfoEngine e;
            e.configs[0] = wave(shape, 2.0f);
            e.configs[1] = wave(shape, 0.7f, 0.0f, LfoTarget::Warp);
            runs.push_back(midCycle(e, 0, fps, 2.0f, 12));
        }
        for (size_t k = 0; k < runs[0].size(); ++k) {
            check(std::fabs(runs[0][k] - runs[1][k]) < 0.03f && std::fabs(runs[2][k] - runs[1][k]) < 0.03f,
                  "a random wave is the same at 30, 60 and 120 fps");
        }
    }
}

void slotsAndSeedsDiffer() {
    LfoEngine e;
    for (int i = 0; i < LfoEngine::kSlots; ++i) e.configs[static_cast<size_t>(i)] = wave(LfoWave::Random, 2.0f);
    LfoEngine again = e;
    LfoEngine other = e;
    other.seed = e.seed + 1;
    const auto a = midCycle(e, 0, 60.0f, 2.0f, 8);
    LfoEngine e1 = again, e2 = again;
    const auto b = midCycle(e1, 1, 60.0f, 2.0f, 8);
    const auto c = midCycle(e2, 2, 60.0f, 2.0f, 8);
    const auto same = midCycle(again, 0, 60.0f, 2.0f, 8);
    const auto seeded = midCycle(other, 0, 60.0f, 2.0f, 8);
    check(a == same, "the same seed gives the same wave");
    check(a != b && b != c && a != c, "each slot has its own random values");
    check(a != seeded, "another seed gives other values");
}

// The music clock does not move evenly: around a cycle boundary the phase can step back over it and
// forward again. That is one new cycle, not three.
void oneStepPerCycleUnderClockJitter() {
    LfoEngine e;
    e.configs[0] = wave(LfoWave::Random, 2.0f, 4.0f, LfoTarget::Sway);
    const float dt = 1.0f / 60.0f;
    float last = e.tick(dt, clockAt(0.5, 128.0f))[0];
    int changes = 0;
    std::set<int> seen;
    for (int cycle = 0; cycle < 16; ++cycle) {
        const double base = cycle * 4.0;
        for (const double at : {1.0, 2.0, 3.0, 3.97, 4.02, 3.98, 4.03, 3.99, 4.05, 4.5}) {
            const float v = e.tick(dt, clockAt(base + at, 128.0f))[0];
            if (v != last) ++changes;
            last = v;
        }
        seen.insert(static_cast<int>(last * 1000.0f));
    }
    check(seen.size() >= 14, "a beat-timed random wave lands on a new value each cycle");
    // Back over the boundary is the old cycle's value again, so the output flips with the clock but the
    // sequence does not run ahead: after the boundary settles it sits on the next value, not three on.
    LfoEngine steady;
    steady.configs[0] = e.configs[0];
    float want = steady.tick(dt, clockAt(0.5, 128.0f))[0];
    for (int cycle = 0; cycle < 16; ++cycle) {
        for (const double at : {1.0, 2.0, 3.0, 3.97, 4.05, 4.5}) want = steady.tick(dt, clockAt(cycle * 4.0 + at, 128.0f))[0];
    }
    check(std::fabs(want - last) < 1e-6f, "jitter over a boundary does not skip values");
}

void switchingBetweenTheRandomWavesHasNoDeadCycle() {
    const GeodeSoundFrame quiet{};
    LfoEngine e;
    e.configs[0] = wave(LfoWave::Random, 10.0f);
    for (int i = 0; i < 60 * 25; ++i) e.tick(1.0f / 60.0f, quiet);  // mid third cycle
    e.tick(1.0f / 60.0f, quiet);
    e.configs[0].wave = LfoWave::RandomGlide;
    const float first = e.tick(1.0f / 60.0f, quiet)[0];
    float moved = 0.0f;
    float v = first;
    for (int i = 0; i < 60 * 4; ++i) {
        v = e.tick(1.0f / 60.0f, quiet)[0];
        moved = std::max(moved, std::fabs(v - first));
    }
    check(moved > 0.01f, "random glide moves in the cycle it was switched on in");

    LfoEngine g;
    g.configs[0] = wave(LfoWave::RandomGlide, 10.0f);
    for (int i = 0; i < 60 * 25; ++i) g.tick(1.0f / 60.0f, quiet);
    g.configs[0].wave = LfoWave::Random;
    check(g.tick(1.0f / 60.0f, quiet)[0] != 0.0f, "random steps hold a value in the cycle they were switched on in");
}

// A beat-timed wave before the music has given a tempo: its own period, never an assumed tempo.
void beatTimedWithoutATempoRunsOnItsOwnPeriod() {
    const GeodeSoundFrame quiet{};
    LfoEngine e;
    e.configs[0] = wave(LfoWave::Saw, 5.0f, 4.0f);
    int wraps = 0;
    float last = e.tick(1.0f / 60.0f, quiet)[0];
    for (int i = 1; i < 60 * 30; ++i) {
        const float v = e.tick(1.0f / 60.0f, quiet)[0];
        if (v < last - 1.0f) ++wraps;
        last = v;
    }
    check(wraps == 6 || wraps == 5, "no tempo measured: a 4-beat wave turns once per its 5 s period (" + std::to_string(wraps) + " in 30 s)");

    // Once a tempo is measured the wave takes it, and keeps it through a gap.
    double beats = 0.0;
    wraps = 0;
    for (int i = 0; i < 60 * 30; ++i) {
        beats += 90.0 / 60.0 / 60.0;
        GeodeSoundFrame f = clockAt(beats, 90.0f);
        if (i >= 600 && i < 900) f = quiet;
        const float v = e.tick(1.0f / 60.0f, f)[0];
        if (i > 60 && v < last - 1.0f) ++wraps;
        last = v;
    }
    // 30 s at 90 BPM is 45 beats: 11 cycles of 4 beats.
    check(wraps >= 10 && wraps <= 12, "a measured tempo times the wave (" + std::to_string(wraps) + " cycles)");
}

void luminanceStepsStayUnderTheFlashRate() {
    const GeodeSoundFrame quiet{};
    LfoEngine e;
    e.configs[0] = wave(LfoWave::Random, 0.05f, 0.0f, LfoTarget::Brightness);
    float last = e.tick(1.0f / 120.0f, quiet)[0];
    int changes = 0;
    for (int i = 1; i < 120 * 10; ++i) {
        const float v = e.tick(1.0f / 120.0f, quiet)[0];
        if (v != last) ++changes;
        last = v;
    }
    check(changes <= 31, "random steps on brightness change at most three times a second (" + std::to_string(changes) + " in 10 s)");
}

// ---- The roll of a set of waves -------------------------------------------------------------------

using Waves = std::array<LfoConfig, LfoEngine::kSlots>;

bool sameWave(const LfoConfig& a, const LfoConfig& b) {
    return a.enabled == b.enabled && a.source == b.source && a.target == b.target && a.wave == b.wave &&
           a.rateSeconds == b.rateSeconds && a.depth == b.depth && a.polarity == b.polarity && a.curve == b.curve && a.beats == b.beats &&
           a.knob == b.knob && a.moment == b.moment;
}

bool sameWaves(const Waves& a, const Waves& b) { return sameWave(a[0], b[0]) && sameWave(a[1], b[1]) && sameWave(a[2], b[2]); }

void rolledWavesAreUsable() {
    const SceneParams params{};
    std::set<int> targets, shapes, timings;
    int styles = 0;
    for (const std::string& style : catalogue::allIds()) {
        if (++styles % 7 != 0) continue;  // a spread of the catalogue
        for (std::uint64_t seed = 1; seed <= 60; ++seed) {
            const Waves w = waves::randomize(Waves{}, params, seed, style, waves::Roll::All);
            std::set<int> own;
            std::set<int> kinds;
            int luminance = 0;
            for (int i = 0; i < LfoEngine::kSlots; ++i) {
                const LfoConfig& c = w[static_cast<size_t>(i)];
                const std::string where = style + " seed " + std::to_string(seed) + " wave " + std::to_string(i + 1);
                check(c.enabled && c.source == ModSource::Lfo, where + ": on, a steady wave");
                check(c.target != LfoTarget::None && modtargets::offeredTo(c.target, style, i), where + ": a target this style reads");
                check(waves::kindOf(c.target) >= 0, where + ": a target the roll knows");
                ModChain chain{};
                check(!chainOf(c.target, chain), where + ": moves the picture, not another wave");
                check(c.target != LfoTarget::Retired5 && c.target != LfoTarget::Brightness && c.target != LfoTarget::Intensity &&
                          c.target != LfoTarget::CamShake,
                      where + ": no retired or flickering target");
                check(c.rateSeconds >= 1.0f && c.rateSeconds <= 24.0f, where + ": a period of 1 to 24 s");
                check(c.depth >= 0.1f && c.depth <= 0.45f, where + ": a visible depth that is not maxed");
                check(c.beats == 0.0f || c.beats == 2.0f || c.beats == 4.0f || c.beats == 8.0f || c.beats == 16.0f, where + ": free or 2-16 beats");
                const bool calm = isLuminanceTarget(c.target) || waves::kindOf(c.target) == waves::kCameraKind;
                if (calm) {
                    check(c.wave == LfoWave::Sine || c.wave == LfoWave::Triangle || c.wave == LfoWave::RandomGlide, where + ": colour and camera glide, never step");
                    check(c.rateSeconds >= 4.0f && (c.beats == 0.0f || c.beats >= 4.0f), where + ": colour and camera move slowly");
                }
                if (isLuminanceTarget(c.target)) {
                    ++luminance;
                    check(c.depth <= 0.3f, where + ": a gentle depth on colour");
                }
                own.insert(static_cast<int>(c.target));
                kinds.insert(waves::kindOf(c.target));
                targets.insert(static_cast<int>(c.target));
                shapes.insert(static_cast<int>(c.wave));
                timings.insert(static_cast<int>(c.beats));
            }
            check(own.size() == LfoEngine::kSlots, style + ": three different targets");
            check(kinds.size() == LfoEngine::kSlots, style + ": spread over three kinds of control");
            check(luminance <= 1, style + ": at most one wave on colour");
        }
    }
    check(targets.size() >= 15, "the roll reaches many targets (" + std::to_string(targets.size()) + ")");
    check(shapes.size() == 6, "every wave shape is rolled");
    check(timings.size() == 5, "free and every beat length are rolled");
}

void sameSeedSameWaves() {
    const SceneParams params{};
    const std::string style = catalogue::allIds().front();
    const Waves a = waves::randomize(Waves{}, params, 42, style, waves::Roll::All);
    const Waves b = waves::randomize(Waves{}, params, 42, style, waves::Roll::All);
    const Waves c = waves::randomize(Waves{}, params, 43, style, waves::Roll::All);
    check(sameWaves(a, b), "the same seed rolls the same waves");
    check(!sameWaves(a, c), "another seed rolls others");
}

// A category roll (Music's Randomize, randomize everything) only re-rolls the steady waves already
// switched on: it never switches a wave on, and leaves a wave that follows the sound alone.
void aCategoryRollOnlyMovesRunningWaves() {
    const SceneParams params{};
    const std::string style = catalogue::allIds().front();
    check(sameWaves(waves::randomize(Waves{}, params, 7, style, waves::Roll::Running), Waves{}), "no wave on: nothing rolled");
    Waves mine{};
    mine[0] = wave(LfoWave::Sine, 2.0f);
    mine[1] = wave(LfoWave::Sine, 2.0f, 0.0f, LfoTarget::Warp);
    mine[1].source = ModSource::Bass;
    const Waves rolled = waves::randomize(mine, params, 7, style, waves::Roll::Running);
    check(!sameWave(rolled[0], mine[0]) && rolled[0].enabled, "the running wave is rolled");
    check(sameWave(rolled[1], mine[1]), "a wave following the bass is left alone");
    check(sameWave(rolled[2], mine[2]) && !rolled[2].enabled, "a wave that is off stays off");
    check(rolled[0].target != LfoTarget::Warp, "and the rolled wave keeps off the other's target");
    // A wave on a macro knob is the user's link: a category roll leaves it, its knob and its moment alone.
    Waves knob{};
    knob[0] = wave(LfoWave::Saw, 3.0f, 0.0f, LfoTarget::Macro);
    knob[0].knob = 2;
    knob[0].moment = 0;
    check(sameWave(waves::randomize(knob, params, 7, style, waves::Roll::Running)[0], knob[0]), "a wave on a knob is not rolled");
    for (std::uint64_t seed = 1; seed <= 100; ++seed) {
        for (const LfoConfig& c : waves::randomize(Waves{}, params, seed, style, waves::Roll::All)) {
            check(c.target != LfoTarget::Macro && c.knob == 0 && c.moment == -1, "a roll never puts a wave on a knob or a moment");
        }
    }
}

// A wave on a control sitting at the end of its range swings into the range, not against the stop.
void polarityFollowsWhereTheControlSits() {
    const std::string style = catalogue::allIds().front();
    int checked = 0;
    for (std::uint64_t seed = 1; seed <= 200; ++seed) {
        SceneParams low{};
        const Waves w = waves::randomize(Waves{}, low, seed, style, waves::Roll::All);
        for (const LfoConfig& c : w) {
            const int slot = paramSlotOf(c.target);
            if (slot < 0) continue;
            const ParamSpec& spec = SceneParams::spec(static_cast<ParamSlot>(slot));
            if (spec.kind == ParamKind::Circular || !(spec.max > spec.min)) continue;
            const float at = (low.getSlot(slot) - spec.min) / (spec.max - spec.min);
            ++checked;
            if (at < 0.2f) check(c.polarity == ModPolarity::Positive, "a control at its low end is pushed up only");
            else if (at > 0.8f) check(c.polarity == ModPolarity::Negative, "a control at its high end is pulled down only");
            else check(c.polarity == ModPolarity::Bipolar, "a control mid-range swings both ways");
        }
    }
    check(checked > 100, "polarity checked");
}

void liquidTargetsOnlyWhenLiquidIsOn() {
    const std::string style = catalogue::allIds().front();
    SceneParams off{};
    off.flowEnabled = false;
    for (std::uint64_t seed = 1; seed <= 200; ++seed) {
        for (const LfoConfig& c : waves::randomize(Waves{}, off, seed, style, waves::Roll::All)) {
            check(c.target != LfoTarget::FlowCurl && c.target != LfoTarget::FlowForce && c.target != LfoTarget::FlowStrength,
                  "no wave on the liquid while Liquid is off");
        }
    }
}

void theWireRoundTrips() {
    const SceneParams params{};
    const Waves w = waves::randomize(Waves{}, params, 99, catalogue::allIds().front(), waves::Roll::All);
    float wire[waves::kWireFloats];
    waves::toWire(w, wire);
    check(sameWaves(waves::fromWire(wire), w), "a set of waves survives the wire");
    Waves linked = w;
    linked[1] = wave(LfoWave::Triangle, 4.0f, 0.0f, LfoTarget::Macro);
    linked[1].knob = 7;
    linked[1].moment = 4;
    waves::toWire(linked, wire);
    check(sameWaves(waves::fromWire(wire), linked), "with a knob and a moment");
}

// A wave on a knob runs no faster than the flash rate: the knob may link a luminance param.
void aWaveOnAKnobStaysUnderTheFlashRate() {
    check(safety::limitLfoRate(20.0f, LfoTarget::Macro) <= 3.0f && safety::limitLfoRate(20.0f, LfoTarget::Zoom) == 20.0f,
          "a knob is held like a luminance target");
}

}  // namespace

int main() {
    stepsHoldAndChangeEachCycle();
    sameAtEveryFrameRate();
    slotsAndSeedsDiffer();
    oneStepPerCycleUnderClockJitter();
    switchingBetweenTheRandomWavesHasNoDeadCycle();
    beatTimedWithoutATempoRunsOnItsOwnPeriod();
    luminanceStepsStayUnderTheFlashRate();
    rolledWavesAreUsable();
    sameSeedSameWaves();
    aCategoryRollOnlyMovesRunningWaves();
    polarityFollowsWhereTheControlSits();
    liquidTargetsOnlyWhenLiquidIsOn();
    theWireRoundTrips();
    aWaveOnAKnobStaysUnderTheFlashRate();
    if (failures == 0) std::printf("lfo_random_test: ok\n");
    return failures == 0 ? 0 : 1;
}
