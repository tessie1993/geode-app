// Host-side checks for the pure render-thread math: the reaction matrix and
// the camera rig. Built and run by CI (see .github/workflows/android.yml,
// "Native host tests") with the system compiler; no GL, no device.
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <set>
#include <string>

#include "viz/CameraRig.hpp"
#include "viz/Circular.hpp"
#include "viz/VisualSafety.hpp"
#include "viz/EndlessZoom.hpp"
#include "viz/Macros.hpp"
#include "viz/MotionField.hpp"
#include "viz/ReactionDna.hpp"
#include "viz/Reactions.hpp"
#include "viz/ScenePrep.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

GeodeSoundFrame silence() { return GeodeSoundFrame{}; }

void foldIsBoundedAndContinuous() {
    float prev = ReactionEngine::fold(0.0f);
    check(std::fabs(prev) < 1e-6f, "fold(0) == 0");
    for (int i = 1; i < 4000; ++i) {
        const float x = static_cast<float>(i) * 0.01f;
        const float y = ReactionEngine::fold(x);
        check(y >= -1.0001f && y <= 1.0001f, "fold stays in -1..1");
        check(std::fabs(y - prev) <= 0.0101f, "fold has no jumps");
        prev = y;
    }
}

void followRouteTracksSource() {
    ReactionEngine e;
    e.routes[0] = {true, ReactSource::KickEnv, LfoTarget::Zoom, ReactMode::Follow, 0.5f, ModCurve::Linear, 0.0f, 0.0f};
    GeodeSoundFrame f = silence();
    f.kickEnv = 0.8f;
    e.tick(f, 1.0f / 60.0f);
    check(std::fabs(e.output(0) - 0.4f) < 1e-4f, "follow with zero smoothing outputs amount * source");
    const SceneParams p = e.apply(SceneParams{});
    check(std::fabs(p.zoom - 1.4f) < 1e-4f, "follow route lands on its target param");
}

// ---- Circular sources (viz/Circular.hpp): hues take the short arc, phases only go forward ----

void circularHelpersTakeTheShortWay() {
    check(std::fabs(circular::arcDelta(0.95f, 0.05f) - 0.1f) < 1e-5f, "0.95 -> 0.05 is +0.1 round the seam, not -0.9");
    check(std::fabs(circular::arcDelta(0.05f, 0.95f) + 0.1f) < 1e-5f, "and back is -0.1");
    check(std::fabs(circular::mixHue(0.9f, 0.1f, 0.5f)) < 1e-5f, "halfway from 0.9 to 0.1 is 0.0, not 0.5");
    check(std::fabs(circular::phaseDelta(0.98f, 0.02f) - 0.04f) < 1e-5f, "a phase wrap is a small step forward");
    check(circular::phaseDelta(0.5f, 0.45f) == 0.0f, "a phase a little behind holds, never runs back");
    check(std::fabs(circular::phaseDelta(0.6f, 0.0f) - 0.4f) < 1e-5f, "a phase reset to 0 goes forward through the seam");
    check(circular::wrap01(1.0f) == 0.0f && circular::wrap01(-0.25f) == 0.75f && circular::wrap01(NAN) == 0.0f, "wrap01 lands in [0, 1)");
    check(std::fabs(circular::follow(Circle::None, 0.95f, 0.05f, 0.5f) - 0.5f) < 1e-5f, "a plain value still eases on its line");
}

// Before: 0.95 -> 0.05 on a chord-hue route eased back through 0.5, every colour on the wheel in one glide.
void hueRouteWrapsTheShortWay() {
    ReactionEngine e;
    e.routes[0] = {true, ReactSource::ChordHue, LfoTarget::ColorShift, ReactMode::Follow, 1.0f, ModCurve::Linear, 0.3f, 0.3f};
    GeodeSoundFrame f = silence();
    f.chordHue = 0.95f;
    for (int i = 0; i < 240; ++i) e.tick(f, 1.0f / 60.0f);
    check(std::fabs(e.output(0) - 0.95f) < 1e-3f, "the hue route settles on its chord");
    f.chordHue = 0.05f;
    e.tick(f, 1.0f / 60.0f);
    check(e.output(0) > 0.95f, "the first step goes up toward the seam, not down toward 0.5");
    bool visitedMiddle = false;
    for (int i = 0; i < 240; ++i) {
        e.tick(f, 1.0f / 60.0f);
        const float v = e.output(0);
        visitedMiddle = visitedMiddle || (v > 0.1f && v < 0.9f);
    }
    check(!visitedMiddle, "0.95 -> 0.05 crosses 1.0 = 0.0 and never visits 0.5");
    check(std::fabs(e.output(0) - 0.05f) < 1e-3f, "and lands on the new chord");
}

// A beat phase ramps and wraps every beat; smoothed, it must keep turning forward through each wrap.
void phaseRouteStaysMonotonic() {
    ReactionEngine e;
    e.routes[0] = {true, ReactSource::BeatPhase, LfoTarget::ColorShift, ReactMode::Follow, 1.0f, ModCurve::Linear, 0.1f, 0.5f};
    GeodeSoundFrame f = silence();
    const float dt = 1.0f / 60.0f;
    float prev = 0.0f;
    float travelled = 0.0f;
    bool backward = false;
    for (int i = 0; i < 600; ++i) {  // 10 s at 120 bpm: 20 wraps
        const float t = static_cast<float>(i) * dt;
        f.beatPhase = 2.0f * t - std::floor(2.0f * t);
        e.tick(f, dt);
        const float v = e.output(0);
        if (i >= 60) {  // after the route's ease-in
            const float d = circular::wrap01(v - prev + 0.5f) - 0.5f;
            backward = backward || d < -1e-5f;
            travelled += d;
        }
        prev = v;
    }
    check(!backward, "a smoothed beat phase never steps backward, wraps included");
    check(std::fabs(travelled - 18.0f) < 0.1f, "and turns once per beat (18 beats in the checked 9 s)");
}

void stepRouteJumpsOncePerEdge() {
    ReactionEngine e;
    e.routes[0] = {true, ReactSource::HardCut, LfoTarget::CamYaw, ReactMode::Step, 0.25f, ModCurve::Linear, 0.0f, 0.0f};
    GeodeSoundFrame f = silence();
    f.hardCut = 1.0f;
    e.tick(f, 1.0f / 60.0f);
    check(std::fabs(e.output(0) - 0.25f) < 1e-4f, "step adds amount on the rising edge");
    // The held envelope is still high: holding must not add again.
    f.hardCut = 0.0f;
    for (int i = 0; i < 10; ++i) e.tick(f, 1.0f / 60.0f);
    check(std::fabs(e.output(0) - 0.25f) < 1e-4f, "step does not re-fire while the envelope stays high");
    // Let the 2.5 s envelope fall below the re-arm level, then fire again.
    for (int i = 0; i < 240; ++i) e.tick(f, 1.0f / 60.0f);
    f.hardCut = 1.0f;
    e.tick(f, 1.0f / 60.0f);
    check(std::fabs(e.output(0) - 0.5f) < 1e-4f, "step fires again after re-arming");
}

void luminanceRoutesAreSlewLimited() {
    ReactionEngine e;
    e.routes[0] = {true, ReactSource::KickEnv, LfoTarget::Brightness, ReactMode::Follow, 1.0f, ModCurve::Linear, 0.0f, 0.0f};
    GeodeSoundFrame f = silence();
    f.kickEnv = 1.0f;
    e.tick(f, 1.0f / 60.0f);
    check(e.output(0) < 0.25f, "a brightness route cannot reach full depth in one frame");
}

void triggersHoldAndSilenceIsQuiet() {
    ReactSignals s;
    GeodeSoundFrame f = silence();
    f.hardCut = 1.0f;
    s.step(f, 1.0f / 60.0f);
    check(s.value(ReactSource::HardCut) > 0.99f, "a hard cut rises instantly");
    f.hardCut = 0.0f;
    for (int i = 0; i < 600; ++i) s.step(f, 1.0f / 60.0f);
    check(s.value(ReactSource::HardCut) < 0.05f, "a hard cut decays over seconds");
    check(s.value(ReactSource::Kick) == 0.0f && s.value(ReactSource::Onset) == 0.0f, "silence leaves hits at zero");
    f.kick = 0.1f;
    s.step(f, 1.0f / 60.0f);
    check(s.value(ReactSource::Kick) >= 0.5f, "even a soft hit crosses the edge Step, Jump and Toggle listen for");
}

void sourcesReadTheirFieldsInRange() {
    static_assert(kReactSourceCount == 117, "dev.geode.render.ReactSource has 117 entries");
    ReactSignals s;
    GeodeSoundFrame f = silence();
    f.bass = 1.0f;
    f.low = 0.7f;
    f.stereoPan = -0.4f;
    f.eq[9] = 1.0f;
    f.eq[15] = 3.0f;
    f.bassTime = 0.0f;
    s.step(f, 0.1f);
    f.bassTime = 0.1f;  // one second of clock per second
    s.step(f, 0.1f);
    check(std::fabs(s.value(ReactSource::Bass) - 0.5f) < 1e-5f, "a typical relative level reads 0.5");
    check(std::fabs(s.value(ReactSource::Low) - 0.7f) < 1e-5f, "a shaped level reads as it is");
    check(std::fabs(s.value(ReactSource::StereoPan) + 0.4f) < 1e-5f, "pan keeps its sign");
    check(std::fabs(s.value(ReactSource::Eq1k5) - 0.5f) < 1e-5f, "an EQ band reads its own band, halved");
    check(s.value(ReactSource::Eq22k) == 1.0f && s.value(ReactSource::Eq25) == 0.0f, "the last EQ band is the 22 kHz one");
    check(std::fabs(s.value(ReactSource::BassTime) - 0.5f) < 1e-4f, "a clock reads its rate");
    f.bassTime = 0.0f;  // the clock wrapped
    s.step(f, 0.1f);
    check(s.value(ReactSource::BassTime) == 0.0f, "a wrapping clock reads one still frame, not a leap");
    f = silence();
    for (int k = 0; k < GEODE_SOUND_SCALARS; ++k) reinterpret_cast<float*>(&f)[k] = (k % 3 == 0) ? 9.0f : NAN;
    s.step(f, 1.0f / 60.0f);
    for (int k = 0; k < kReactSourceCount; ++k) {
        const float v = s.value(static_cast<ReactSource>(k));
        check(std::isfinite(v) && v >= -1.0f && v <= 1.0f, "every source stays finite and in range on a wild frame");
    }
}

void deterministicForExport() {
    ReactionEngine a;
    ReactionEngine b;
    a.routes[0] = b.routes[0] = {true, ReactSource::Bass, LfoTarget::Warp, ReactMode::Spin, 0.7f, ModCurve::Smooth, 0.05f, 0.2f};
    for (int i = 0; i < 500; ++i) {
        GeodeSoundFrame f = silence();
        f.bass = 1.0f + 0.8f * std::sin(0.1f * static_cast<float>(i));
        a.tick(f, 1.0f / 30.0f);
        b.tick(f, 1.0f / 30.0f);
    }
    check(a.output(0) == b.output(0), "same frames give the same output");
}

void cameraTravelsAndStaysOrthonormal() {
    CameraRig rig;
    SceneParams p;
    p.camFly = 1.0f;
    for (int i = 0; i < 600; ++i) rig.step(p, {}, 1.0f / 60.0f, false);
    const CameraPose& c = rig.pose();
    check(c.travel > 10.0f, "glide path advances");
    auto dot = [](const std::array<float, 3>& a, const std::array<float, 3>& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    check(std::fabs(dot(c.fwd, c.fwd) - 1.0f) < 1e-3f, "forward is unit");
    check(std::fabs(dot(c.fwd, c.right)) < 1e-3f && std::fabs(dot(c.fwd, c.up)) < 1e-3f, "basis is orthogonal");
    check(c.fwd[2] > 0.8f, "glide looks down the travel axis");
}

void cameraSpringsInsteadOfSnapping() {
    CameraRig rig;
    SceneParams p;
    rig.step(p, {}, 1.0f / 60.0f, false);
    p.camPush = 1.0f;
    rig.step(p, {}, 1.0f / 60.0f, false);
    check(rig.pose().dolly > 0.0f && rig.pose().dolly < 0.3f, "dolly eases toward a pushed target");
    for (int i = 0; i < 300; ++i) rig.step(p, {}, 1.0f / 60.0f, false);
    check(std::fabs(rig.pose().dolly - 1.0f) < 0.01f, "dolly settles on the target");
}

void cameraPathRepeatsAcrossTheWrap() {
    for (int path = 0; path < 4; ++path) {
        const auto a = CameraRig::pathPoint(path, 0.5f, 3.0f);
        const auto b = CameraRig::pathPoint(path, 0.5f, 3.0f + CameraRig::kPathPeriod);
        check(std::fabs(a[0] - b[0]) < 1e-2f && std::fabs(a[1] - b[1]) < 1e-2f, "path x/y repeat every kPathPeriod");
    }
}

// The rig is fed what the frame feeds it: params after safety::apply, which owns the
// reduced-motion scale of forward travel. The fly-through must come out slowed exactly once.
void reducedMotionSlowsTravel() {
    CameraRig fast;
    CameraRig slow;
    SceneParams p;
    p.camFly = 1.0f;
    const SceneParams full = safety::apply(p, false);
    const SceneParams reduced = safety::apply(p, true);
    for (int i = 0; i < 300; ++i) {
        fast.step(full, {}, 1.0f / 60.0f, false);
        slow.step(reduced, {}, 1.0f / 60.0f, true);
    }
    const float ratio = slow.pose().travel / fast.pose().travel;
    check(std::fabs(ratio - safety::kReducedMotionScale) < 0.05f, "reduced motion scales the fly-through exactly once");
}

bool finite3(const std::array<float, 3>& v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

GeodeSoundFrame busyFrame(int i) {
    GeodeSoundFrame f = silence();
    f.bass = 1.0f + 0.5f * std::sin(0.3f * static_cast<float>(i));
    f.kick = (i % 7 == 0) ? 1.0f : 0.0f;
    f.kickEnv = (i % 7 == 0) ? 1.0f : 0.2f;
    f.onset = (i % 5 == 0) ? 0.8f : 0.0f;
    f.surge = (i % 50 == 0) ? 1.0f : 0.0f;
    f.hardCut = (i % 40 == 0) ? 1.0f : 0.0f;
    return f;
}

void oddFrameTimesStayFiniteAndBounded() {
    const float odd[] = {0.0f, -1.0f, -1e9f, 1e6f, NAN, INFINITY, -INFINITY};
    for (float dt : odd) {
        ReactionEngine e;
        e.routes[0] = {true, ReactSource::Bass, LfoTarget::Rotation, ReactMode::Spin, 1.0f, ModCurve::Linear, 0.05f, 0.3f};
        e.routes[1] = {true, ReactSource::Kick, LfoTarget::CamPush, ReactMode::Follow, 0.5f, ModCurve::Smooth, 0.0f, 0.2f};
        e.routes[2] = {true, ReactSource::HardCut, LfoTarget::CamYaw, ReactMode::Step, 0.6f, ModCurve::Linear, 0.5f, 0.5f};
        CameraRig rig;
        SceneParams p;
        p.camFly = 2.0f;
        p.camShake = 1.0f;
        for (int i = 0; i < 120; ++i) {
            e.tick(busyFrame(i), dt);
            const SceneParams q = e.apply(p);
            rig.step(q, {0.5f, -0.5f}, dt, false);
            for (int r = 0; r < 3; ++r) check(std::isfinite(e.output(r)) && std::fabs(e.output(r)) <= 1.0001f, "route output finite and bounded for odd dt");
        }
        const CameraPose& c = rig.pose();
        check(finite3(c.pos) && finite3(c.fwd) && finite3(c.up) && finite3(c.right), "camera pose finite for odd dt");
        check(std::isfinite(c.focal) && c.focal > 0.0f, "camera focal finite and positive for odd dt");
        if (!(dt > 0.0f) || !std::isfinite(dt)) check(c.travel == 0.0f, "no time passing means no travel");
    }
}

void zeroDtFreezesTheCameraAndHugeDtIsOneClampedStep() {
    SceneParams p;
    p.camFly = 1.0f;
    CameraRig a;
    CameraRig b;
    for (int i = 0; i < 30; ++i) {
        a.step(p, {}, 1.0f / 60.0f, false);
        b.step(p, {}, 1.0f / 60.0f, false);
    }
    const float before = a.pose().travel;
    a.step(p, {}, 0.0f, false);
    check(a.pose().travel == before, "dt = 0 does not move the camera");
    a.step(p, {}, 1e6f, false);
    b.step(p, {}, 0.0f, false);
    b.step(p, {}, 0.1f, false);
    check(std::fabs(a.pose().travel - b.pose().travel) < 1e-5f, "a huge dt is treated as the 0.1 s cap");
}

void stillStylesKeepTheBankOutOfTheFlatRoll() {
    // A style that does not turn (SceneRegistry::spinRate 0) must not roll as
    // the glide path curves; the turning ones still bank.
    SceneParams p;
    p.camFly = 1.0f;
    CameraRig still;
    CameraRig turning;
    float maxStill = 0.0f;
    float maxTurning = 0.0f;
    for (int i = 0; i < 1200; ++i) {
        still.step(p, {}, 1.0f / 60.0f, false, false);
        turning.step(p, {}, 1.0f / 60.0f, false, true);
        maxStill = std::fmax(maxStill, std::fabs(still.pose().flat[3]));
        maxTurning = std::fmax(maxTurning, std::fabs(turning.pose().flat[3]));
    }
    check(maxStill < 1e-6f, "a still style's flat picture never rolls on its own");
    check(maxTurning > 0.01f, "a turning style still banks into the curve");
}

// One kick per 0.5 s at 60 fps, for `beats` kicks.
void playBeats(ReactionEngine& e, int beats, int& count) {
    for (int b = 0; b < beats; ++b) {
        for (int i = 0; i < 30; ++i) {
            GeodeSoundFrame f = silence();
            if (i == 0) f.kick = 1.0f;
            e.tick(f, 1.0f / 60.0f);
        }
        ++count;
    }
}

void jumpHoldsANewSeededValuePerHit() {
    static_assert(kReactModeCount == 5, "dev.geode.render.ReactMode has 5 entries");
    ReactionEngine a;
    ReactionEngine b;
    a.reset();
    b.reset();
    a.routes[0] = b.routes[0] = {true, ReactSource::Kick, LfoTarget::Twist, ReactMode::Jump, 1.0f, ModCurve::Linear, 0.0f, 0.0f};
    int ca = 0;
    int cb = 0;
    float last = 0.0f;
    int moved = 0;
    for (int hit = 0; hit < 12; ++hit) {
        playBeats(a, 1, ca);
        playBeats(b, 1, cb);
        const float v = a.output(0);
        check(v >= -1.0f && v <= 1.0f, "jump stays within the amount");
        if (std::fabs(v - last) >= 0.49f) ++moved;
        last = v;
        check(a.output(0) == b.output(0), "jump draws the same values for the same frames");
    }
    check(moved == 12, "every hit lands at least half a unit from the last");
}

void toggleFlipsBetweenZeroAndAmount() {
    ReactionEngine e;
    e.reset();
    e.routes[0] = {true, ReactSource::Kick, LfoTarget::Twist, ReactMode::Toggle, 1.0f, ModCurve::Linear, 0.0f, 0.0f};
    // The bug Toggle replaces: Step by 2 folds back to 0 on every edge.
    check(ReactionEngine::fold(2.0f) == ReactionEngine::fold(0.0f), "a step of 2 never moves");
    int count = 0;
    for (int hit = 0; hit < 6; ++hit) {
        playBeats(e, 1, count);
        check(e.output(0) == (hit % 2 == 0 ? 1.0f : 0.0f), "each kick flips the toggle");
    }
}

// A spread of real style ids with made-up traits: every fourth one turns, two
// in three are shader styles.
constexpr const char* kStyleIds[] = {
    "kaleido", "plasma", "julia", "tunnel", "aurora", "voronoi", "arbor_grove", "arbor_bolt", "bulb_julia", "bulb_ink",
    "chrys_bloom", "chrys_fall", "lucid_mandala", "milkdrop", "fluid", "water", "silk", "bars", "spiral", "kifs",
    "chrome_nebula", "crystal_canyon", "gyroid_cavern", "jelly_abyss", "fractal_dive", "aurora_ocean", "bead_vortex",
    "curl_bloom", "acid_show", "blacklight_bloom",
};
constexpr int kStyleCount = static_cast<int>(sizeof(kStyleIds) / sizeof(kStyleIds[0]));

StyleTraits traitsAt(int i) { return {i % 4 == 0 ? (i % 8 == 0 ? 1.0f : -0.7f) : 0.0f, i % 3 != 2}; }

bool sameRoutes(const StyleDna& a, const StyleDna& b) {
    if (a.count != b.count) return false;
    for (int i = 0; i < a.count; ++i) {
        const ReactRoute& x = a.routes[static_cast<size_t>(i)];
        const ReactRoute& y = b.routes[static_cast<size_t>(i)];
        if (x.source != y.source || x.target != y.target || x.mode != y.mode || x.amount != y.amount || x.curve != y.curve ||
            x.attackSeconds != y.attackSeconds || x.releaseSeconds != y.releaseSeconds) {
            return false;
        }
    }
    return true;
}

void dnaIsDeterministicAndSeeded() {
    for (int i = 0; i < kStyleCount; ++i) {
        check(sameRoutes(buildStyleDna(kStyleIds[i], 3, traitsAt(i)), buildStyleDna(kStyleIds[i], 3, traitsAt(i))),
              "the same id and seed give the same DNA (live view and export agree)");
    }
    check(!sameRoutes(buildStyleDna("kifs", 0, traitsAt(0)), buildStyleDna("kifs", 1, traitsAt(0))), "re-rolling the seed changes the DNA");
}

void stylesGetDifferentPersonalities() {
    std::set<int> leads;
    int pulses = 0;
    int beatZooms = 0;
    for (int i = 0; i < kStyleCount; ++i) {
        const StyleDna dna = buildStyleDna(kStyleIds[i], 0, traitsAt(i));
        leads.insert(static_cast<int>(dna.lead));
        if (dna.lead == Personality::Pulse) ++pulses;
        for (int r = 0; r < dna.count; ++r) {
            const ReactRoute& route = dna.routes[static_cast<size_t>(r)];
            const bool beat = route.source == ReactSource::Kick || route.source == ReactSource::Kick2 ||
                              route.source == ReactSource::KickEnv || route.source == ReactSource::Onset;
            if (beat && (route.target == LfoTarget::CamPush || route.target == LfoTarget::Zoom)) {
                ++beatZooms;
                break;
            }
        }
    }
    check(leads.size() >= 6, "thirty styles draw at least six different pulse personalities");
    check(pulses * 4 <= kStyleCount, "at most a quarter of styles push in on the kick");
    check(beatZooms * 3 <= kStyleCount, "most styles do not zoom on the kick at all");
}

bool kickSide(ReactSource s) { return s == ReactSource::Kick || s == ReactSource::Kick2 || s == ReactSource::Onset; }
bool snareSide(ReactSource s) {
    return s == ReactSource::Snare || s == ReactSource::Snare2 || s == ReactSource::Hat || s == ReactSource::NoteChange;
}

void kickAndSnareSidesAnswerDifferently() {
    for (int i = 0; i < kStyleCount; ++i) {
        const StyleDna dna = buildStyleDna(kStyleIds[i], 0, traitsAt(i));
        std::set<int> a;
        std::set<int> b;
        for (int r = 0; r < dna.count; ++r) {
            const ReactRoute& route = dna.routes[static_cast<size_t>(r)];
            if (kickSide(route.source)) a.insert(static_cast<int>(route.target));
            if (snareSide(route.source)) b.insert(static_cast<int>(route.target));
        }
        check(!a.empty() && !b.empty(), "both sides of the groove move something");
        check(dna.lead != dna.second && a != b, "the two sides answer it differently");
    }
}

void dnaSourcesVaryAcrossStyles() {
    // The user asked for no fixed default reaction: no layer may listen to the same source in
    // every style. Count the distinct sources the thirty styles use, layer by layer (route index).
    std::set<int> sources;
    std::set<int> hues;
    for (int i = 0; i < kStyleCount; ++i) {
        const StyleDna dna = buildStyleDna(kStyleIds[i], 0, traitsAt(i));
        for (int r = 0; r < dna.count; ++r) {
            const ReactRoute& route = dna.routes[static_cast<size_t>(r)];
            sources.insert(static_cast<int>(route.source));
            if (route.target == LfoTarget::ColorShift) hues.insert(static_cast<int>(route.source));
        }
    }
    check(sources.size() >= 20, "thirty styles listen to at least twenty different sources");
    check(hues.size() >= 4, "hue follows at least four different sources across styles");
}

void dnaRespectsStyleTraits() {
    for (int i = 0; i < kStyleCount; ++i) {
        for (int seed = 0; seed < 6; ++seed) {
            const StyleTraits t = traitsAt(i);
            const StyleDna dna = buildStyleDna(kStyleIds[i], seed, t);
            for (int r = 0; r < dna.count; ++r) {
                const LfoTarget target = dna.routes[static_cast<size_t>(r)].target;
                if (t.spinRate == 0.0f) check(target != LfoTarget::Rotation && target != LfoTarget::CamRoll, "still styles never spin or roll");
                if (!t.shaderLook) check(target != LfoTarget::Morph && target != LfoTarget::PaletteMix, "shader-only targets stay on shader styles");
            }
        }
    }
}

void dnaLeavesRoomAndStaysInRange() {
    for (int i = 0; i < kStyleCount; ++i) {
        for (int seed = 0; seed < 6; ++seed) {
            const StyleDna dna = buildStyleDna(kStyleIds[i], seed, traitsAt(i));
            check(dna.count >= 5 && dna.count <= StyleDna::kMaxRoutes, "the DNA fills most slots and leaves three free");
            for (int r = 0; r < dna.count; ++r) {
                const ReactRoute& route = dna.routes[static_cast<size_t>(r)];
                check(route.enabled && route.target != LfoTarget::None, "every route moves something");
                check(route.amount >= -2.0f && route.amount <= 2.0f, "amount within what native accepts");
                check(route.attackSeconds >= 0.0f && route.attackSeconds <= 5.0f, "attack within range");
                check(route.releaseSeconds >= 0.0f && route.releaseSeconds <= 10.0f, "release within range");
            }
        }
    }
}


// Music playing at a steady level, for the camera's own moves.
GeodeSoundFrame playing() {
    GeodeSoundFrame f = silence();
    f.presence = 1.0f;
    f.intensity = 1.0f;
    f.bassAtt = 1.0f;
    return f;
}

void hardCutsJumpWithoutATweenAndKeepTheirGap() {
    SceneParams p;
    p.camFly = 1.0f;
    CameraRig rig;
    GeodeSoundFrame f = playing();
    for (int i = 0; i < 60; ++i) rig.step(p, {}, 1.0f / 60.0f, false, true, &f);
    const float before = rig.pose().travel;
    f.hardCut = 1.0f;
    rig.step(p, {}, 1.0f / 60.0f, false, true, &f);
    check(rig.cuts() == 1, "a hard cut cuts");
    check(std::fabs(rig.pose().travel - before) > 30.0f, "a cut lands somewhere else on the path");
    // Another cut five seconds later is ignored: cuts keep their gap.
    f.hardCut = 0.0f;
    for (int i = 0; i < 300; ++i) rig.step(p, {}, 1.0f / 60.0f, false, true, &f);
    f.hardCut = 1.0f;
    rig.step(p, {}, 1.0f / 60.0f, false, true, &f);
    check(rig.cuts() == 1, "cuts closer than the gap are ignored");
    f.hardCut = 0.0f;
    for (int i = 0; i < 60 * 15; ++i) rig.step(p, {}, 1.0f / 60.0f, false, true, &f);
    f.hardCut = 1.0f;
    rig.step(p, {}, 1.0f / 60.0f, false, true, &f);
    check(rig.cuts() == 2, "a cut after the gap cuts again");
}

void silenceAndTheMotionDialStillTheMusicMoves() {
    SceneParams p;
    p.camFly = 1.0f;
    CameraRig quiet;
    CameraRig none;
    CameraRig still;
    SceneParams noMotion = p;
    noMotion.motionAmount = 0.0f;
    GeodeSoundFrame f = silence();  // presence 0: no music
    GeodeSoundFrame loud = playing();
    for (int i = 0; i < 600; ++i) {
        f.midTime = loud.midTime = 0.01f * static_cast<float>(i);
        f.kick = loud.kick = (i % 30 == 0) ? 1.0f : 0.0f;
        f.hardCut = loud.hardCut = (i == 300) ? 1.0f : 0.0f;
        quiet.step(p, {}, 1.0f / 60.0f, false, true, &f);
        none.step(p, {}, 1.0f / 60.0f, false, true, nullptr);
        still.step(noMotion, {}, 1.0f / 60.0f, false, true, &loud);
    }
    check(std::fabs(quiet.pose().yaw - none.pose().yaw) < 1e-5f && quiet.cuts() == 0, "with no music the camera moves as if it had no sound");
    check(std::fabs(still.pose().yaw - none.pose().yaw) < 1e-5f && still.cuts() == 0, "Motion at 0 turns the music's moves off");
}

void traumaShakeIsBoundedAndFinite() {
    SceneParams p;
    CameraRig rig;
    GeodeSoundFrame f = playing();
    float maxYaw = 0.0f;
    for (int i = 0; i < 600; ++i) {
        f.kick = 1.0f;
        f.snare = 1.0f;
        f.flux = 4.0f;
        f.midTime = 1e7f;
        rig.step(p, {}, 1.0f / 60.0f, false, true, &f);
        maxYaw = std::fmax(maxYaw, std::fabs(rig.pose().yaw));
        check(std::isfinite(rig.pose().yaw) && std::isfinite(rig.pose().pitch), "shake stays finite");
    }
    check(maxYaw < 0.4f, "a wall of hits still shakes within bounds");
    f = playing();
    f.kick = NAN;
    f.presence = INFINITY;
    rig.step(p, {}, 1.0f / 60.0f, false, true, &f);
    check(std::isfinite(rig.pose().yaw) && std::isfinite(rig.pose().focal), "a wild frame leaves the camera finite");
}

void motionFieldVariantsDiffer() {
    std::set<int> hues;
    GeodeSoundFrame f = playing();
    f.pitchClass = 0.25f;
    f.pitchConfidence = 1.0f;
    f.centroid = 0.75f;
    f.noteRandom = 0.5f;
    f.kickRandom = 0.0f;
    for (int i = 0; i < kStyleCount; ++i) {
        MotionField m;
        m.setVariant(styleVariantSeed(kStyleIds[i]));
        for (int k = 0; k < 600; ++k) m.step(f, 1.0f / 30.0f);
        hues.insert(static_cast<int>(std::lround(m.state().keyHue * 100.0f)));
    }
    check(hues.size() >= 3, "styles follow different hue drivers");
}
void sourceCatalogueIsCompleteAndUnique() {
    std::set<std::string> names;
    const std::set<std::string> groups = {"LEVELS", "DRUMS", "EVENTS", "MELODY", "CLOCKS", "TEXTURE", "EQ", "RHYTHM", "HARMONY", "FORM"};
    for (int i = 0; i < kReactSourceCount; ++i) {
        const ReactSourceInfo& s = reactSourceInfo(static_cast<ReactSource>(i));
        check(s.name != nullptr && s.label != nullptr && s.name[0] != '\0' && s.label[0] != '\0', "every source has a name and a label");
        names.insert(s.name);
        check(groups.count(s.group) == 1, "every source sits in a picker section");
    }
    check(static_cast<int>(names.size()) == kReactSourceCount, "source names are unique (they are the saved names)");
    check(reactSourceInfo(ReactSource::Kick).event && !reactSourceInfo(ReactSource::KickEnv).event, "hits are events, envelopes are levels");
    check(reactSourceInfo(ReactSource::ChordHue).circle == Circle::Hue && reactSourceInfo(ReactSource::KeyHue).circle == Circle::Hue,
          "hue sources are marked circular");
    check(reactSourceInfo(ReactSource::BeatPhase).circle == Circle::Phase && reactSourceInfo(ReactSource::PhrasePhase).circle == Circle::Phase,
          "phase sources are marked as phases");
    check(reactSourceInfo(ReactSource::Bass).circle == Circle::None && reactSourceInfo(ReactSource::Tension).circle == Circle::None,
          "levels are lines");
}

// ---- Customize › Macros (viz/Macros) ----

MacroKnobs knobs(Macro m, float v) {
    MacroKnobs k{};
    k[static_cast<size_t>(m)] = v;
    return k;
}

void macrosAtRestChangeNothing() {
    SceneParams p;
    p.speed = 1.7f;
    p.vignette = 0.3f;
    const SceneParams q = macros::apply(p, MacroKnobs{});
    check(q.speed == p.speed && q.vignette == p.vignette && q.zoom == p.zoom && q.bloom == p.bloom, "knobs at centre leave the look as styled");
    GeodeSoundTuning t{};
    t.reactivity = 1.3f;
    t.speed = 0.8f;
    const GeodeSoundTuning u = macros::tuning(t, MacroKnobs{});
    check(u.reactivity == 1.3f && u.speed == 0.8f, "knobs at centre leave Settings › Audio as set");
    const CameraRig::Moves m = macros::camera(MacroKnobs{});
    check(m.orbit == 1.0f && m.shake == 1.0f && m.cut == 1.0f, "knobs at centre leave the camera's music moves as styled");
    for (const ReactRoute& r : macros::routes(MacroKnobs{})) check(!r.enabled, "knobs at centre add no routes");
}

void macrosTurnTowardTheEndsAndStayInRange() {
    SceneParams p;
    const SceneParams up = macros::apply(p, knobs(Macro::Speed, 1.0f));
    check(std::fabs(up.speed - 4.0f) < 1e-5f, "a full turn up reaches the top of the range");
    const SceneParams half = macros::apply(p, knobs(Macro::Speed, 0.5f));
    check(half.speed > p.speed && half.speed < up.speed, "half a turn goes part of the way");
    const SceneParams down = macros::apply(p, knobs(Macro::Speed, -1.0f));
    check(std::fabs(down.speed - 0.05f) < 1e-5f, "a full turn down reaches the bottom");
    const SceneParams wide = macros::apply(p, knobs(Macro::Space, 1.0f));
    check(wide.zoom < p.zoom && wide.vignette <= p.vignette && wide.camFov > p.camFov, "Space up widens: zoom out, less vignette, wider lens");
    SceneParams wild = p;
    wild.bloom = 9.0f;
    check(macros::apply(wild, knobs(Macro::Energy, 1.0f)).bloom <= 0.25f, "a knob never pushes a field past its slider");
    check(macros::apply(p, knobs(Macro::Mood, 1.0f)).speed == p.speed, "a knob only moves its own fields");
}

void macrosScaleTheSoundAndTheCamera() {
    GeodeSoundTuning t{};
    t.reactivity = 1.0f;
    t.speed = 1.0f;
    check(std::fabs(macros::tuning(t, knobs(Macro::Energy, 1.0f)).reactivity - 2.0f) < 1e-5f, "Energy up doubles Reactivity");
    check(std::fabs(macros::tuning(t, knobs(Macro::Speed, -1.0f)).speed - 0.5f) < 1e-5f, "Speed down halves the clocks");
    check(macros::camera(knobs(Macro::Chaos, -1.0f)).cut == 0.0f, "Chaos all the way down stops hard cuts");
    check(macros::camera(knobs(Macro::Space, 1.0f)).orbit == 2.0f, "Space up doubles the orbit");
    int on = 0;
    for (const ReactRoute& r : macros::routes(knobs(Macro::Chaos, 0.5f))) {
        if (!r.enabled) continue;
        ++on;
        check(r.amount > 0.0f && r.amount <= 0.25f + 1e-5f, "a half turn plays its routes at half depth");
    }
    check(on == 2, "Chaos up adds its two routes and nothing else");
}

void chaosDownStopsCameraCuts() {
    SceneParams p;
    p.camFly = 1.0f;
    CameraRig rig;
    rig.setMoves(macros::camera(knobs(Macro::Chaos, -1.0f)));
    GeodeSoundFrame f = playing();
    for (int i = 0; i < 60; ++i) rig.step(p, {}, 1.0f / 60.0f, false, true, &f);
    f.hardCut = 1.0f;
    rig.step(p, {}, 1.0f / 60.0f, false, true, &f);
    check(rig.cuts() == 0, "with Chaos all the way down a big moment does not cut");
}

void surpriseIsSeededAndKeepsLocks() {
    const MacroKnobs start = knobs(Macro::Mood, 0.9f);
    const unsigned moodLock = 1u << static_cast<int>(Macro::Mood);
    const MacroKnobs a = macros::surprise(start, moodLock, 42);
    check(a == macros::surprise(start, moodLock, 42), "the same seed draws the same knobs");
    check(a[static_cast<size_t>(Macro::Mood)] == 0.9f, "a locked knob stays where it was");
    const MacroKnobs c = macros::surprise(start, 0, 42);
    check(a[0] == c[0], "locking one knob does not change what the others draw");
    for (float v : c) check(std::fabs(v) <= 0.8f + 1e-6f, "Surprise stays within most of each knob's reach");
    check(macros::surprise(start, 0, 43) != c, "a new seed draws a new look");
}

void startersAreNamedAndInRange() {
    std::set<std::string> names;
    for (const macros::Starter& s : macros::starters()) {
        names.insert(s.name);
        for (float v : s.knobs) check(v >= -1.0f && v <= 1.0f, "a starter's knobs are in range");
    }
    check(static_cast<int>(names.size()) == macros::kStarterCount, "starter names are unique");
    check(macros::starters()[0].knobs == MacroKnobs{}, "the first starter is As styled");
}

// Endless zoom used to spell its magnification as a triangle, 1x -> 2x -> 1x, so the dive
// backed out every other half-octave. Each layer must only ever grow, the pair must be the
// same picture either side of the phase wrap, and phase 0 is plain 1x.
float layerWeight(const endless::Layers& l, float mag) {
    float w = 0.0f;
    if (std::fabs(l.mag0 - mag) < 1e-4f) w += 1.0f - l.weight1;
    if (std::fabs(l.mag1 - mag) < 1e-4f) w += l.weight1;
    return w;
}

void endlessZoomOnlyDivesIn() {
    // Follow the two layers through one octave by identity (the outgoing one starts at 1x).
    float prevOut = 0.0f;
    float prevIn = 0.0f;
    int twoLayerFrames = 0;
    for (int i = 0; i <= 1000; ++i) {
        const float ph = static_cast<float>(i) / 1000.0f;
        const endless::Layers l = endless::layers(ph);
        const float out = std::exp2(ph);
        const float in = std::exp2(ph - 1.0f);
        check(layerWeight(l, out) + layerWeight(l, in) > 0.999f, "the two layers are the octave pair");
        check(l.weight1 >= 0.0f && l.weight1 <= 0.5f, "the second layer never dominates");
        check(l.mag0 >= 0.5f && l.mag0 <= 2.0f && l.mag1 >= 0.5f && l.mag1 <= 2.0f, "scale stays within an octave of 1x");
        if (i > 0) check(out > prevOut && in > prevIn, "both layers only ever zoom in");
        if (l.weight1 > 0.0f) ++twoLayerFrames;
        prevOut = out;
        prevIn = in;
    }
    // The second pass is the whole cost on a low-end GPU: it must stay inside the ~40% window.
    check(twoLayerFrames <= 410, "the second layer draws only inside the crossfade window");
    const endless::Layers start = endless::layers(0.0f);
    check(start.mag0 == 1.0f && start.weight1 == 0.0f, "phase 0 (and off) is one layer at exactly 1x");
    // Seam: what is on screen just before the wrap is what is on screen just after it.
    const endless::Layers before = endless::layers(0.99999f);
    check(std::fabs(before.mag0 - start.mag0) < 1e-3f && before.weight1 == 0.0f, "no pop at the phase wrap");
    // The visible mix moves smoothly: no step changes either layer's weight by much.
    float prevW = 0.0f;
    for (int i = 1; i <= 1000; ++i) {
        const float ph = static_cast<float>(i) / 1000.0f;
        const float w = layerWeight(endless::layers(ph), std::exp2(ph - 1.0f));
        check(std::fabs(w - prevW) < 0.01f, "the crossfade has no jumps");
        prevW = w;
    }
    // The magnification the screen settles on is continuous too: the weighted log-scale.
    float prevLog = 0.0f;
    for (int i = 0; i <= 2000; ++i) {
        const float ph = std::fmod(static_cast<float>(i) / 1000.0f, 1.0f);
        const endless::Layers l = endless::layers(ph);
        const float logMag = (1.0f - l.weight1) * std::log2(l.mag0) + l.weight1 * std::log2(l.mag1);
        if (i > 0) check(std::fabs(logMag - prevLog) < 0.01f, "the blended scale never jumps, across the wrap too");
        prevLog = logMag;
    }
}

void endlessZoomPhaseWrapsForEverInBothDirections() {
    float ph = 0.0f;
    for (int i = 0; i < 60 * 60 * 60; ++i) ph = endless::advance(ph, 1.2f, 1.0f / 60.0f);  // an hour at full speed
    check(ph >= 0.0f && ph < 1.0f, "phase stays in [0, 1) after an hour");
    float back = 0.3f;
    for (int i = 0; i < 600; ++i) back = endless::advance(back, -0.7f, 1.0f / 60.0f);
    check(back >= 0.0f && back < 1.0f, "a negative speed wraps back into [0, 1)");
    check(endless::advance(0.4f, 0.0f, 1.0f / 60.0f) == 0.4f, "zero speed holds the phase");
    check(endless::advance(0.4f, NAN, 1.0f / 60.0f) == 0.4f, "NaN speed holds the phase");
    check(endless::advance(0.4f, INFINITY, 1.0f / 60.0f) == 0.4f, "infinite speed holds the phase");
    check(endless::advance(NAN, 0.3f, 1.0f / 60.0f) == 0.0f, "a NaN phase recovers to 0");
    check(endless::advance(0.0f, -1e-9f, 1.0f) < 1.0f, "a tiny negative step never lands on 1.0");
}

// The music pushes the dive the MilkDrop way, on the relative bass (over the song's own mean),
// and never on loudness. Silence drifts slowly; it never stops the dive.
void endlessZoomFollowsTheBassNotTheLoudness() {
    GeodeSoundFrame s = silence();
    check(std::fabs(endless::musicRate(s, 1.0f) - endless::kSilenceRate) < 1e-6f, "silence drifts slowly");
    check(endless::musicRate(s, 2.0f) > 0.0f, "silence never stops the dive, at any Audio drive");
    s.presence = 1.0f;
    s.bassAtt = 1.0f;
    check(std::fabs(endless::musicRate(s, 1.0f) - 1.0f) < 1e-6f, "typical bass dives at Dive speed");
    s.bassAtt = 2.0f;
    const float hit = endless::musicRate(s, 1.0f);
    check(hit > 1.2f, "a bass hit pushes the dive");
    GeodeSoundFrame loud = s;
    loud.vol = loud.volAtt = 3.0f;
    loud.rms = 0.9f;
    loud.loudness = 1.0f;
    loud.intensity = 1.0f;
    loud.energyTrend = 1.0f;
    loud.surge = 1.0f;
    check(endless::musicRate(loud, 1.0f) == hit, "loudness never changes the dive rate");
    s.bassAtt = 0.0f;
    check(endless::musicRate(s, 2.0f) >= endless::kMinRate, "a bass drop slows the dive but never stalls it");
    check(endless::musicRate(s, 0.0f) == 1.0f, "Audio drive 0 leaves the dive steady");
    check(endless::musicRate(silence(), 0.0f) == 1.0f, "Audio drive 0 keeps Dive speed through silence too");
    s.bassAtt = NAN;
    s.presence = INFINITY;
    const float odd = endless::musicRate(s, NAN);
    check(std::isfinite(odd) && odd >= endless::kMinRate, "non-finite sound leaves a finite rate");
}

void endlessZoomDrivesTheRigForward() {
    CameraRig still;
    CameraRig diving;
    SceneParams p;
    p.camFly = 0.0f;
    SceneParams q = p;
    q.endlessZoom = true;
    q.endlessZoomSpeed = 0.5f;
    for (int i = 0; i < 600; ++i) {
        still.step(p, {}, 1.0f / 60.0f, false, false, nullptr);
        diving.step(q, {}, 1.0f / 60.0f, false, false, nullptr);
    }
    check(diving.pose().travel > still.pose().travel + 1.0f, "Endless zoom flies the 3D styles forward");
}

// The 3D dive, like the 2D one, is slowed once by safety::apply and never again by the rig.
void endlessZoomReducedMotionScalesTheDiveOnce() {
    SceneParams p;
    p.camFly = 0.0f;
    p.endlessZoom = true;
    p.endlessZoomSpeed = 1.0f;
    const SceneParams normal = safety::apply(p, false);
    const SceneParams reduced = safety::apply(p, true);
    CameraRig full;
    CameraRig slow;
    for (int i = 0; i < 600; ++i) {
        full.step(normal, {}, 1.0f / 60.0f, false, false, nullptr);
        slow.step(reduced, {}, 1.0f / 60.0f, true, false, nullptr);
    }
    const float ratio = slow.pose().travel / full.pose().travel;
    check(std::fabs(ratio - safety::kReducedMotionScale) < 0.05f, "reduced motion scales the 3D dive exactly once");
}

void endlessZoomLeavesGlassAndTruchetOnTheOldLaw() {
    check(endless::keepsTriangle("glass_orb") && endless::keepsTriangle("truchet_arcs"), "Glass and Truchet keep their law");
    check(!endless::keepsTriangle("plasma") && !endless::keepsTriangle("arbor_bolt"), "every other style dives");
    check(std::fabs(endless::triangle(0.5f).mag0 - 2.0f) < 1e-6f && endless::triangle(0.0f).mag0 == 1.0f, "the old law is kept exactly");
    check(endless::triangle(0.5f).weight1 == 0.0f, "the old law draws one layer");
}

void buildBudgetBoundsAFrame() {
    BuildBudget b;
    b.begin(10.0);
    check(b.allows(10.5, true), "the first piece goes even when the frame is already late");
    b.spend(true);
    check(!b.allows(10.0, true), "a second blocking compile waits for the next frame");
    check(!b.allows(10.5, false), "nothing more goes once the frame's slice is spent");
    b.begin(20.0);
    b.spend(false);
    check(b.allows(20.0 + BuildBudget::kFrameSeconds * 0.5, false), "cheap work goes on inside the slice");
    check(b.allows(20.0 + BuildBudget::kFrameSeconds * 0.5, true), "one blocking compile may follow cheap work in the slice");
    check(!b.allows(20.0 + BuildBudget::kFrameSeconds * 1.5, false), "nothing goes past the slice");
    b.begin(30.0);
    check(b.pieces() == 0 && b.allows(30.0, true), "a new frame starts a new budget");
}

void scenePrepRetargetsAndCaps() {
    ScenePrep prep;
    check(!prep.active(), "nothing is prepared at first");
    check(prep.retarget("silk_web"), "a new scene is a new target");
    check(!prep.retarget("silk_web"), "asking again for the same scene keeps its progress");
    check(prep.next(false) == ScenePrep::Step::Wait, "a busy stager makes the frame wait");
    check(prep.attempts() == 0, "waiting is not an attempt");
    check(prep.next(true) == ScenePrep::Step::Attempt, "an idle stager makes the frame attempt the build");
    check(prep.attempts() == 1, "the attempt is counted");
    check(prep.retarget("fluid"), "another scene restarts preparation");
    check(prep.attempts() == 0 && prep.target() == "fluid", "the new target starts with no attempts");
    for (int i = 0; i < ScenePrep::kMaxAttempts; ++i) check(prep.next(true) == ScenePrep::Step::Attempt, "attempts up to the cap");
    check(prep.next(true) == ScenePrep::Step::Block, "a scene that keeps deferring is built inline");
    check(prep.next(false) == ScenePrep::Step::Block, "blocking wins over waiting once capped");
    prep.done();
    check(!prep.active() && prep.retarget("fluid"), "a finished target is forgotten");
}

// The renderer's staged protocol against a stand-in stager: the first attempt discovers the scene's
// programs, waits compile one per frame, and the attempt after the stager goes idle succeeds.
void stagedBuildFinishesInBoundedFrames() {
    constexpr int kPrograms = 5;
    ScenePrep prep;
    int queued = 0;
    int compiled = 0;
    bool discovered = false;
    bool built = false;
    int frames = 0;
    int mostCompilesInAFrame = 0;
    while (!built && frames < 100) {
        ++frames;
        int compilesThisFrame = 0;
        prep.retarget("silk_web");
        switch (prep.next(queued == 0)) {
            case ScenePrep::Step::Wait: {
                BuildBudget budget;
                budget.begin(0.0);
                while (queued > 0 && budget.allows(0.0, true)) {
                    budget.spend(true);
                    --queued;
                    ++compiled;
                    ++compilesThisFrame;
                }
                break;
            }
            case ScenePrep::Step::Attempt:
                if (!discovered) {
                    discovered = true;
                    queued = kPrograms;
                } else {
                    built = compiled == kPrograms;
                }
                break;
            case ScenePrep::Step::Block:
                built = true;
                break;
        }
        if (compilesThisFrame > mostCompilesInAFrame) mostCompilesInAFrame = compilesThisFrame;
    }
    check(built, "the staged scene gets built");
    check(frames == kPrograms + 2, "one discovery frame, one frame per program, one frame to assemble");
    check(prep.attempts() == 2, "two attempts: discover, then assemble");
    check(mostCompilesInAFrame == 1, "no frame compiles more than one program without the parallel extension");
}

}  // namespace

int main() {
    foldIsBoundedAndContinuous();
    followRouteTracksSource();
    circularHelpersTakeTheShortWay();
    hueRouteWrapsTheShortWay();
    phaseRouteStaysMonotonic();
    stepRouteJumpsOncePerEdge();
    luminanceRoutesAreSlewLimited();
    triggersHoldAndSilenceIsQuiet();
    sourcesReadTheirFieldsInRange();
    deterministicForExport();
    jumpHoldsANewSeededValuePerHit();
    toggleFlipsBetweenZeroAndAmount();
    dnaIsDeterministicAndSeeded();
    stylesGetDifferentPersonalities();
    kickAndSnareSidesAnswerDifferently();
    dnaSourcesVaryAcrossStyles();
    dnaRespectsStyleTraits();
    dnaLeavesRoomAndStaysInRange();
    cameraTravelsAndStaysOrthonormal();
    cameraSpringsInsteadOfSnapping();
    cameraPathRepeatsAcrossTheWrap();
    reducedMotionSlowsTravel();
    oddFrameTimesStayFiniteAndBounded();
    zeroDtFreezesTheCameraAndHugeDtIsOneClampedStep();
    stillStylesKeepTheBankOutOfTheFlatRoll();
    hardCutsJumpWithoutATweenAndKeepTheirGap();
    silenceAndTheMotionDialStillTheMusicMoves();
    traumaShakeIsBoundedAndFinite();
    motionFieldVariantsDiffer();
    sourceCatalogueIsCompleteAndUnique();
    macrosAtRestChangeNothing();
    macrosTurnTowardTheEndsAndStayInRange();
    macrosScaleTheSoundAndTheCamera();
    chaosDownStopsCameraCuts();
    surpriseIsSeededAndKeepsLocks();
    startersAreNamedAndInRange();
    endlessZoomOnlyDivesIn();
    endlessZoomPhaseWrapsForEverInBothDirections();
    endlessZoomFollowsTheBassNotTheLoudness();
    endlessZoomDrivesTheRigForward();
    endlessZoomLeavesGlassAndTruchetOnTheOldLaw();
    endlessZoomReducedMotionScalesTheDiveOnce();
    buildBudgetBoundsAFrame();
    scenePrepRetargetsAndCaps();
    stagedBuildFinishesInBoundedFrames();
    if (failures == 0) std::printf("viz_math_test: all passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
