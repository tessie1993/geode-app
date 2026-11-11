// Host-side checks for the pure render-thread math: the reaction matrix and
// the camera rig. Built and run by CI (see .github/workflows/android.yml,
// "Native host tests") with the system compiler; no GL, no device.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#include <set>
#include <string>

#include "viz/CameraRig.hpp"
#include "viz/Circular.hpp"
#include "viz/VisualSafety.hpp"
#include "viz/EndlessZoom.hpp"
#include "viz/MotionField.hpp"
#include "viz/ReactionDna.hpp"
#include "viz/Lfo.hpp"
#include "viz/Reactions.hpp"
#include "viz/ScenePrep.hpp"
#include "viz/SpeedClock.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// The frame most of these tests start from. Its drive (the music's absolute magnitude, core/sound/DriveGain) is
// full, so a route or a motion term reads at the strength the test says; driveScalesTheMagnitudeOfMotion below
// is the one that turns it down.
GeodeSoundFrame silence() {
    GeodeSoundFrame f{};
    f.drive = 1.0f;
    return f;
}

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
    static_assert(kReactSourceCount == 118, "dev.geode.render.ReactSource has 118 entries");
    ReactSignals s;
    GeodeSoundFrame f = silence();
    f.bass = 1.0f;
    f.low = 0.7f;
    f.stereoPan = -0.4f;
    f.eq[9] = 1.0f;
    f.eq[15] = 3.0f;
    f.fadeGain = 0.25f;
    f.bassTime = 0.0f;
    s.step(f, 0.1f);
    f.bassTime = 0.1f;  // one second of clock per second
    s.step(f, 0.1f);
    check(std::fabs(s.value(ReactSource::Bass) - 0.5f) < 1e-5f, "a typical relative level reads 0.5");
    check(std::fabs(s.value(ReactSource::Low) - 0.7f) < 1e-5f, "a shaped level reads as it is");
    check(std::fabs(s.value(ReactSource::StereoPan) + 0.4f) < 1e-5f, "pan keeps its sign");
    check(std::fabs(s.value(ReactSource::FadeGain) - 0.25f) < 1e-5f, "the player's fade gain reads as it is");
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
    for (int path = 0; path < CameraRig::kPathCount; ++path) {
        for (float s : {3.0f, 117.5f, 801.0f}) {
            const auto a = CameraRig::pathPoint(path, 0.5f, s);
            const auto b = CameraRig::pathPoint(path, 0.5f, s + CameraRig::kPathPeriod);
            check(std::fabs(a[0] - b[0]) < 1e-2f && std::fabs(a[1] - b[1]) < 1e-2f, "path x/y repeat every kPathPeriod");
            // z is the travel itself on the paths that go somewhere, a closed loop on the rest.
            const float dz = b[2] - a[2];
            check(std::fabs(dz) < 1e-2f || std::fabs(dz - CameraRig::kPathPeriod) < 1e-2f, "path z repeats or advances by exactly one period");
        }
    }
}

float angleBetween(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    const float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    return std::acos(std::clamp(d, -1.0f, 1.0f));
}

void backwardTravelUnwindsAndTheTurnIsContinuous() {
    for (int path : {0, 1, 4, 5, 6}) {
        CameraRig rig;
        SceneParams p;
        p.camPath = path;
        p.camFly = 1.0f;
        for (int i = 0; i < 2400; ++i) rig.step(p, {}, 1.0f / 60.0f, false);
        const float start = rig.pose().travel;
        // Flip the direction. No frame may jump: the position stays within one step of speed and the
        // view turns round over many frames instead of flipping.
        p.camFly = -1.0f;
        CameraPose prev = rig.pose();
        float maxStep = 0.0f;
        float maxTurn = 0.0f;
        bool sawBackFacing = false;
        for (int i = 0; i < 600; ++i) {
            rig.step(p, {}, 1.0f / 60.0f, false);
            const CameraPose& c = rig.pose();
            maxStep = std::fmax(maxStep, std::sqrt((c.pos[0] - prev.pos[0]) * (c.pos[0] - prev.pos[0]) +
                                                   (c.pos[1] - prev.pos[1]) * (c.pos[1] - prev.pos[1]) +
                                                   (c.pos[2] - prev.pos[2]) * (c.pos[2] - prev.pos[2])));
            maxTurn = std::fmax(maxTurn, angleBetween(c.fwd, prev.fwd));
            sawBackFacing = sawBackFacing || c.fwd[2] < -0.5f;
            prev = c;
        }
        check(maxStep < 0.15f, "reversing never jumps the camera");
        check(maxTurn < 0.35f, "reversing turns the view round smoothly");
        check(sawBackFacing, "backward travel faces the way it goes");
        check(rig.pose().travel < start, "backward travel runs the distance back");
        // And steady backward travel only ever decreases the distance (mod the wrap).
        float last = rig.pose().travel;
        for (int i = 0; i < 300; ++i) {
            rig.step(p, {}, 1.0f / 60.0f, false);
            const float now = rig.pose().travel;
            check(now < last || now - last > CameraRig::kTravelWrap * 0.5f, "backward travel decreases monotonically");
            last = now;
        }
    }
}

void reverseDoesNotChangeForwardLooks() {
    // The turn-round is 1 while moving forward, so the default look is bit-for-bit what it was.
    CameraRig rig;
    SceneParams p;
    for (int i = 0; i < 300; ++i) rig.step(p, {}, 1.0f / 60.0f, false);
    check(rig.pose().fwd[2] > 0.8f, "forward glide still looks down the travel axis");
}

void flatStylesDiveBothWays() {
    SceneParams p;
    p.camFlat = 0.0f;
    check(CameraRig::flatDive(p) == 0.0f, "flat travel is off by default");
    p.camFlat = 1.0f;
    p.camFly = 1.0f;
    const float in = CameraRig::flatDive(p);
    p.camFly = -1.0f;
    const float out = CameraRig::flatDive(p);
    check(in > 0.0f && out < 0.0f && std::fabs(in + out) < 1e-6f, "forward zooms in, backward zooms out");
    p.camPath = 3;
    check(CameraRig::flatDive(p) == 0.0f, "hover does not dive");
    // Backward shrinks the picture without a pop: the endless zoom phase wraps either way.
    float ph = 0.0f;
    float last = ph;
    float maxJump = 0.0f;
    for (int i = 0; i < 1200; ++i) {
        ph = endless::advance(ph, out, 1.0f / 60.0f);
        const float moved = std::fabs(ph - last);
        maxJump = std::fmax(maxJump, std::fmin(moved, 1.0f - moved));
        last = ph;
    }
    check(maxJump < 0.05f, "zooming out loops without a pop");
    // Reduced motion already scaled Fly speed in safety::apply, so the dive shrinks with it.
    p.camPath = 0;
    p.camFly = 1.0f;
    check(CameraRig::flatDive(safety::apply(p, true)) < CameraRig::flatDive(p), "reduced motion slows the flat dive");
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
// ---- Speed-integrated clocks (viz/SpeedClock, MotionField::State::flowTime) ----

// A frame whose loudness wanders, so the flow rate is not a constant.
GeodeSoundFrame wandering(int k) {
    GeodeSoundFrame f = playing();
    f.volAtt = 1.0f + 0.8f * std::sin(0.013f * static_cast<float>(k));
    return f;
}

void flowTimeIsFlowPhaseAtSpeedOne() {
    MotionField m;
    bool same = true;
    for (int k = 0; k < 30 * 600; ++k) {
        // Hitches past MotionField's 0.1 s dt clamp too: both clocks see the clamped dt.
        m.step(wandering(k), k % 997 == 0 ? 0.4f : 1.0f / 30.0f);
        same = same && m.state().flowTime == m.state().flowPhase;
    }
    check(same, "uFlowTime equals uFlowPhase at Speed 1, bit for bit, so no Speed-1 look changes");
}

void flowTimeRunsAtSpeedAndNeverJumps() {
    // Ten minutes at Speed 1, then nudge Speed to 1.05 and step once.
    MotionField m;
    const float dt = 1.0f / 30.0f;
    for (int k = 0; k < 30 * 600; ++k) m.step(wandering(k), dt, 1.0f);
    const float phase = m.state().flowPhase;
    const float before = m.state().flowTime;
    m.step(wandering(30 * 600), dt, 1.05f);
    const float step = m.state().flowTime - before;
    // The old uFlowPhase * uSpeed moved by phase * 0.05 in that one frame: many seconds of flow.
    check(phase * 0.05f > 1.0f, "the old flowPhase * speed would have jumped by over a second of flow");
    check(step > 0.0f && step < 0.05f, "uFlowTime moves by one frame of flow when Speed changes, never a jump");
    // Rate follows Speed: at 2, two seconds of clock take one second.
    MotionField a;
    MotionField b;
    for (int k = 0; k < 300; ++k) {
        a.step(playing(), dt, 1.0f);
        b.step(playing(), dt, 2.0f);
    }
    check(std::fabs(b.state().flowTime - 2.0f * a.state().flowTime) < 1e-3f, "uFlowTime runs at Speed's rate");
}

void followerScalesASoundClockWithoutJumps() {
    using speedclock::Follower;
    Follower one;
    Follower two;
    float source = 12.5f;  // the engine's clock is already running when a style loads
    check(one.step(source, 1.0f, 7100.0f) == source, "a follower starts on the source's own value");
    two.step(source, 2.0f, 7100.0f);
    for (int k = 0; k < 100; ++k) {
        source += 0.02f;
        one.step(source, 1.0f, 7100.0f);
        two.step(source, 2.0f, 7100.0f);
    }
    check(std::fabs(one.value - source) < 1e-3f, "at Speed 1 the follower reads the source");
    check(std::fabs((two.value - 12.5f) - 2.0f * (source - 12.5f)) < 1e-3f, "at Speed 2 it advances twice as far");
    // The source wraps or restarts (a new track): the follower takes one small step, not a jump.
    const float held = two.value;
    two.step(0.01f, 2.0f, 7100.0f);
    check(two.value > held && two.value - held < 0.05f, "a source that steps back never jumps the follower");
    // Changing Speed moves nothing by itself.
    const float at = one.value;
    one.step(source, 1.5f, 7100.0f);
    check(one.value == at, "a Speed change with no source advance leaves the clock where it is");
}

// #359: the drift's spin is a rate, so it must stay bounded and never jump. It used to add the
// wrapped drift angle itself: the spin climbed to the clamp, then reversed in one frame at the wrap.
void motionFieldDriftSpinIsBoundedAndContinuous() {
    GeodeSoundFrame f = playing();
    f.midAtt = 2.0f;  // the loudest mids: the fastest drift
    SceneParams p;
    p.rotation = 0.0f;
    p.motionDrift = 1.0f;
    const float spin = 1.4f;  // bead_vortex, the fastest turning style
    const float bound = 0.9f * spin + 1e-3f;
    MotionField m;
    float prev = 0.0f;
    float peak = 0.0f;
    bool bounded = true;
    bool smooth = true;
    for (int k = 0; k < 60 * 300; ++k) {   // five minutes at 60 fps: past two old wrap cycles
        f.hardCut = (k % (60 * 20) == 0) ? 1.0f : 0.0f;  // re-aim the drift's sign now and then
        m.step(f, 1.0f / 60.0f);
        const float r = m.apply(p, false, spin).rotation;
        if (!(std::fabs(r) <= bound)) bounded = false;
        if (k > 0 && std::fabs(r - prev) > 0.05f) smooth = false;
        peak = std::max(peak, std::fabs(r));
        prev = r;
    }
    check(bounded, "the drift spin stays under its bound, far from the rotation clamp");
    check(smooth, "the drift spin never jumps between frames");
    check(peak > 0.1f, "the drift still spins a turning style");
    check(m.apply(p, false, 0.0f).rotation == 0.0f, "a style that does not turn gets no drift spin");
}

// Reduced motion scales a rate once: MotionField leaves rotation, sway and turbulence to
// safety::apply, which scales them right after it, so the pair lands at 0.4 of full, not 0.16.
void motionFieldReducedMotionScalesRatesOnce() {
    GeodeSoundFrame f = playing();
    f.midAtt = 1.0f;
    f.trebleAtt = 2.0f;
    f.hitTime = 0.3f;  // a bar phase off zero, so sway has a term
    SceneParams p;
    p.rotation = 0.0f;
    p.sway = 0.0f;
    p.turbulence = 0.2f;
    p.motionAmount = 1.0f;
    p.motionDrift = 1.0f;
    MotionField m;
    for (int k = 0; k < 120; ++k) m.step(f, 1.0f / 60.0f);
    const SceneParams full = safety::apply(m.apply(p, false, 1.0f), false);
    const SceneParams calm = safety::apply(m.apply(p, true, 1.0f), true);
    const float s = safety::kReducedMotionScale;
    check(std::fabs(full.rotation) > 1e-3f && std::fabs(calm.rotation - s * full.rotation) < 1e-5f,
          "reduced motion scales the drift spin once");
    check(full.sway > 1e-3f && std::fabs(calm.sway - s * full.sway) < 1e-5f, "reduced motion scales the sway once");
    check(std::fabs(calm.turbulence - s * full.turbulence) < 1e-5f, "reduced motion scales the turbulence once");
}

// uDrift is an angle the styles turn by, times a fixed multiplier (bulb_prism 0.3 and 0.07,
// chrys_* 0.08 to 0.25, lucid_mandala 0.35, bulb_hybrid 0.5). Its wrap must land each of those on a
// whole turn, or the style pops at the wrap; the old 4 pi wrap popped 0.3 * uDrift by 3.8 rad.
void motionFieldDriftWrapNeverPopsAStyle() {
    const float multipliers[] = {0.07f, 0.08f, 0.1f, 0.12f, 0.15f, 0.25f, 0.3f, 0.35f, 0.5f};
    GeodeSoundFrame f = playing();
    f.midAtt = 2.0f;  // the fastest drift, so the wrap comes soonest
    MotionField m;
    float prev = m.state().drift;
    bool wrapped = false;
    bool seamless = true;
    for (int k = 0; k < 50000; ++k) {  // 5000 s at dt 0.1: past one wrap at the fastest rate
        m.step(f, 0.1f);
        const float d = m.state().drift;
        if (d < prev - 1.0f) wrapped = true;
        for (const float mul : multipliers) {
            const float step = std::remainder(mul * d - mul * prev, 6.2831853f);
            if (std::fabs(step) > 0.02f) seamless = false;
        }
        prev = d;
    }
    check(wrapped, "the drift angle wraps on a long session");
    check(seamless, "every style's turn is seamless across the wrap");
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

// The share of the pixel drawn at each magnification from 1/64x to 4x: what the dive stage puts on screen.
std::array<float, 9> tunnelPicture(float phase, float x, float y) {
    std::array<float, 9> out{};
    for (int j = 0; j < 9; ++j) out[static_cast<size_t>(j)] = endless::tunnel::share(phase, std::exp2(static_cast<float>(j - 6)), x, y);
    return out;
}

// Endless zoom is the picture nested inside itself an octave apart. Every copy only ever grows, the frame
// is always covered, no copy is magnified past two times (four in the rounded frame's corner slivers), and the picture just before the phase wraps is
// the picture just after it. An earlier law ran one copy 1x -> 2x -> 1x, which backed out every half-octave.
void endlessZoomIsOneSeamlessTunnel() {
    namespace t = endless::tunnel;
    const float pts[][2] = {{0.0f, 0.0f}, {0.03f, 0.01f}, {0.2f, -0.1f}, {0.5f, 0.5f}, {-0.7f, 0.3f}, {0.95f, 0.0f}, {1.0f, 1.0f}, {-1.0f, 0.4f}};
    for (const auto& pt : pts) {
        const auto before = tunnelPicture(0.99999f, pt[0], pt[1]);
        const auto after = tunnelPicture(0.0f, pt[0], pt[1]);
        for (size_t j = 0; j < before.size(); ++j) check(std::fabs(before[j] - after[j]) < 2e-3f, "no pop at the phase wrap");
        for (int i = 1; i <= 1000; ++i) {
            const float ph = static_cast<float>(i) / 1000.0f * 0.99999f;
            float total = 0.0f;
            for (int k = t::kInner; k >= -t::kBase; --k) {
                const float s = t::scale(ph, k);
                const float part = t::share(ph, s, pt[0], pt[1]);
                check(part >= 0.0f && part <= 1.0f, "a copy's share is a share");
                const bool corner = t::rim(pt[0], pt[1]) > 1.0f;
                check(part == 0.0f || s <= (corner ? 4.0f : 2.0f), "no copy is magnified past two times, four in the corner slivers");
                total += part;
            }
            check(total <= 1.0f + 1e-5f, "the copies never add up to more than the frame");
            check(total >= 1.0f - t::kShade - 1e-5f, "a wall's shadow is all that ever darkens it");
        }
    }
    for (int k = t::kInner; k >= -t::kBase; --k) {
        check(t::scale(0.6f, k) > t::scale(0.4f, k), "diving in, every copy grows");
        if (k > -t::kBase) check(std::fabs(t::scale(0.3f, k) * 2.0f - t::scale(0.3f, k - 1)) < 1e-6f, "the copies sit an octave apart");
    }
    check(t::scale(0.0f, 0) == 1.0f && t::cover(0.0f, t::kInner, 0.0f, 0.0f) == 0.0f, "phase 0 is the plain picture with the newest copy still shut");
    // The centre is never a hole: the copy there is whole, and small.
    for (int i = 0; i <= 100; ++i) {
        const float ph = static_cast<float>(i) / 100.0f * 0.99999f;
        float centre = 0.0f;
        for (int k = t::kInner; k >= t::kInner - 1; --k) centre += t::share(ph, t::scale(ph, k), 0.0f, 0.0f);
        check(std::fabs(centre - 1.0f) < 1e-5f, "the centre is always the two innermost copies");
    }
    // What a point shows changes smoothly as the dive runs: a copy's soft edge crossing it, never a cut.
    for (const auto& pt : pts) {
        float worst = 0.0f;
        for (int i = 1; i <= 2000; ++i) {
            const float a = static_cast<float>(i - 1) / 2000.0f * 0.99999f;
            const float b = static_cast<float>(i) / 2000.0f * 0.99999f;
            for (int k = t::kInner; k >= -t::kBase; --k) {
                worst = std::max(worst, std::fabs(t::share(b, t::scale(b, k), pt[0], pt[1]) - t::share(a, t::scale(a, k), pt[0], pt[1])));
            }
        }
        check(worst < 0.02f, "no copy's share of a pixel jumps");
    }
}

// A shader style's Trails in the feedback pass: MilkDrop's sustain. The echo zoom and warp are motion,
// pinned in feedback_motion_test.
void sustainHoldsLongerAtLongerTrails() {
    namespace fb = endless::feedback;
    const float dt = 1.0f / 60.0f;
    check(fb::sustainInject(1.0f, dt) < fb::sustainInject(0.5f, dt) && fb::sustainInject(0.5f, dt) < fb::sustainInject(0.0f, dt),
          "a longer trail pours less of the new frame");
    const float keep60 = 1.0f - fb::sustainInject(0.7f, 1.0f / 60.0f);
    const float keep30 = 1.0f - fb::sustainInject(0.7f, 1.0f / 30.0f);
    check(std::fabs(keep30 - keep60 * keep60) < 1e-5f, "the trail lasts as long at 30 Hz as at 60");
    float kept = 1.0f;
    for (int i = 0; i < 180; ++i) kept *= 1.0f - fb::sustainInject(1.0f, dt);
    check(std::fabs(kept - 0.5f) < 0.01f, "the longest trail is half gone after its half-life");
    check(fb::sustainInject(std::nanf(""), dt) > 0.0f, "non-finite input holds still");
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

// A song as the sound engine reports it: a locked beat `beatSeconds` long (what Rhythm measured, never a
// tempo the dive assumes), a steady drive, and whatever the test adds on top.
GeodeSoundFrame songAt(double t, double beatSeconds, float drive) {
    GeodeSoundFrame f = playing();
    f.beatConf = 0.9f;
    f.beatPhase = static_cast<float>(std::fmod(t, beatSeconds) / beatSeconds);
    f.drive = drive;
    return f;
}

// Octaves a dive at `speed` travels through `seconds` of a song whose drive is `driveAt(t)`.
template <typename DriveAt>
double diveTravel(float fps, double seconds, double beatSeconds, float speed, DriveAt driveAt, float* lastRate = nullptr) {
    endless::Follow follow;
    const float dt = 1.0f / fps;
    double octaves = 0.0;
    const int frames = static_cast<int>(std::lround(seconds * fps));
    for (int i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / fps;
        follow.step(songAt(t, beatSeconds, driveAt(t)), dt, 1.0f);
        octaves += static_cast<double>(endless::travel(speed, follow)) * dt;
    }
    if (lastRate != nullptr) *lastRate = follow.rate();
    return octaves;
}

// The dive is its own thing: no hit, band or beat moves it. The music's only hand on it is how big it has
// been over the last eight beats the tracker measured, worth a few percent, and nothing without a locked beat.
void endlessZoomIgnoresHitsAndFollowsOnlyTheLongRun() {
    using endless::Follow;
    const float dt = 1.0f / 60.0f;
    check(endless::travel(0.3f, Follow{}) == 0.3f && endless::travel(-0.3f, Follow{}) == -0.3f, "Dive speed is the rate, in and out");
    check(endless::travel(9.0f, Follow{}) == endless::kMaxSpeed && endless::travel(-9.0f, Follow{}) == -endless::kMaxSpeed, "clamped to its range");
    check(endless::travel(NAN, Follow{}) == 0.0f, "a non-finite speed holds still");

    // No measured beat: the rate is Dive speed exactly, however loud or busy the frame.
    Follow unlocked;
    for (int i = 0; i < 3600; ++i) {
        GeodeSoundFrame f = songAt(i * dt, 0.47, 1.0f);
        f.beatConf = 0.2f;
        f.bassAtt = 3.0f;
        f.kickEnv = 1.0f;
        unlocked.step(f, dt, 2.0f);
        check(unlocked.rate() == 1.0f, "without a locked beat the music does not touch the dive");
    }

    // Every per-hit value differs between these two songs; the dive is the same to the bit.
    Follow plain;
    Follow hit;
    for (int i = 0; i < 3600; ++i) {
        const GeodeSoundFrame f = songAt(i * dt, 0.47, 0.6f);
        GeodeSoundFrame g = f;
        g.bassAtt = (i % 28) < 4 ? 3.0f : 0.2f;
        g.midAtt = g.trebleAtt = g.volAtt = 2.5f;
        g.bass = g.mid = g.treble = g.vol = 2.0f;
        g.kickEnv = g.snareEnv = g.hatEnv = (i % 28) < 4 ? 1.0f : 0.0f;
        g.surge = g.intensity = g.loudness = g.energyTrend = 1.0f;
        plain.step(f, dt, 1.0f);
        hit.step(g, dt, 1.0f);
        check(plain.rate() == hit.rate(), "bass, bands, drums and loudness never reach the dive");
    }

    // The first eight measured beats change nothing; after them the rate eases toward the long average.
    Follow early;
    for (int i = 0; i < static_cast<int>(7.5 * 0.47 * 60.0); ++i) {
        early.step(songAt(i * dt, 0.47, 1.0f), dt, 1.0f);
        check(early.rate() == 1.0f, "fewer than eight measured beats: no follow yet");
    }
    float fatRate = 1.0f;
    float padRate = 1.0f;
    diveTravel(60.0f, 60.0, 0.47, 0.3f, [](double) { return 1.0f; }, &fatRate);
    diveTravel(60.0f, 60.0, 0.47, 0.3f, [](double) { return 0.0f; }, &padRate);
    check(fatRate > 1.1f && fatRate <= 1.0f + Follow::kReach + 1e-6f, "a long fat passage dives a little faster");
    check(padRate < 0.9f && padRate >= 1.0f - Follow::kReach - 1e-6f, "a long thin one a little slower, never stopped");

    // One kick: a tenth of a second at full drive in a steady song. The rate does not move measurably.
    float steadyRate = 1.0f;
    float kickRate = 1.0f;
    float worst = 0.0f;
    {
        Follow steady;
        Follow kicked;
        for (int i = 0; i < 3600; ++i) {
            const double t = i * dt;
            steady.step(songAt(t, 0.47, 0.3f), dt, 1.0f);
            kicked.step(songAt(t, 0.47, t >= 30.0 && t < 30.1 ? 1.0f : 0.3f), dt, 1.0f);
            worst = std::max(worst, std::fabs(kicked.rate() - steady.rate()));
        }
        steadyRate = steady.rate();
        kickRate = kicked.rate();
    }
    check(worst < 0.01f && std::fabs(kickRate - steadyRate) < 0.01f, "a single kick moves the dive by under one percent");

    // Audio drive 0 switches the follow off; losing the beat lets it go.
    Follow off;
    Follow lost;
    for (int i = 0; i < 3600; ++i) {
        off.step(songAt(i * dt, 0.47, 1.0f), dt, 0.0f);
        lost.step(songAt(i * dt, 0.47, 1.0f), dt, 1.0f);
    }
    check(off.rate() == 1.0f, "Audio drive 0 leaves the dive at Dive speed");
    check(lost.rate() > 1.1f, "locked and loud");
    for (int i = 0; i < 1200; ++i) {
        GeodeSoundFrame f = songAt(i * dt, 0.47, 1.0f);
        f.beatConf = 0.0f;
        lost.step(f, dt, 1.0f);
    }
    check(std::fabs(lost.rate() - 1.0f) < 1e-3f, "when the beat is lost the dive returns to Dive speed");

    Follow odd;
    GeodeSoundFrame bad = songAt(0.0, 0.47, NAN);
    bad.beatPhase = NAN;
    bad.beatConf = INFINITY;
    for (int i = 0; i < 600; ++i) odd.step(bad, NAN, NAN);
    check(std::isfinite(odd.rate()), "non-finite sound leaves a finite rate");
}

// The same song travels the same distance at any frame rate, and at any measured beat length the average is
// over eight of those beats: the follow keeps no tempo of its own.
void endlessZoomTravelsTheSameAtAnyFrameRate() {
    const auto song = [](double t) { return 0.35f + 0.3f * static_cast<float>(std::sin(t * 0.21)) + (std::fmod(t, 0.61) < 0.08 ? 0.25f : 0.0f); };
    for (const double beatSeconds : {0.61, 0.33, 1.1}) {
        const double at60 = diveTravel(60.0f, 90.0, beatSeconds, 0.4f, song);
        const double at30 = diveTravel(30.0f, 90.0, beatSeconds, 0.4f, song);
        const double at120 = diveTravel(120.0f, 90.0, beatSeconds, 0.4f, song);
        check(std::fabs(at30 - at60) < 0.002 * at60 && std::fabs(at120 - at60) < 0.002 * at60, "30, 60 and 120 fps travel the same distance");
        const double out = diveTravel(60.0f, 90.0, beatSeconds, -0.4f, song);
        check(std::fabs(out + at60) < 1e-6 * at60, "rising out is falling in, reversed");
    }
    // A slow song's window is eight of its own beats: a step in the music reaches the rate later there.
    const auto step = [](double t) { return t < 20.0 ? 0.0f : 1.0f; };
    float fastSong = 1.0f;
    float slowSong = 1.0f;
    diveTravel(60.0f, 23.0, 0.33, 0.4f, step, &fastSong);
    diveTravel(60.0f, 23.0, 1.1, 0.4f, step, &slowSong);
    check(fastSong > slowSong, "the window is counted in measured beats, not seconds");
}

// The music's absolute magnitude (the frame's drive) scales how much audio moves the picture: the same relative
// swell on a pad (drive 0.1) moves the routes, the motion field and the camera less than on a fat
// track (drive 0.8). The base values, and the silence drift, are untouched.
// No sound, no movement: the picture's clock stops in silence, runs whole while music plays, and eases
// between the two as presence fades.
void theClockStopsInSilence() {
    GeodeSoundFrame f = silence();
    check(geode::sound::motionRate(f) == 0.0f, "silence stops the clock");
    f.presence = 0.04f;
    check(geode::sound::motionRate(f) == 0.0f, "the tail of a fade is a full stop, not a crawl");
    f.presence = 0.25f;
    const float easing = geode::sound::motionRate(f);
    check(easing > 0.0f && easing < 1.0f, "a fading presence eases the clock");
    check(geode::sound::motionRate(playing()) == 1.0f, "music runs the clock whole");
    f.presence = std::nanf("");
    check(geode::sound::motionRate(f) == 0.0f, "a non-finite presence is silence");
}

void driveScalesTheMagnitudeOfMotion() {
    GeodeSoundFrame fat = playing();
    fat.drive = 0.8f;
    GeodeSoundFrame pad = fat;
    pad.drive = 0.1f;
    check(geode::sound::driveGain(fat) > 0.999f, "a loud dense track spends everything");
    check(geode::sound::driveGain(pad) > geode::sound::kDriveGainFloor && geode::sound::driveGain(pad) < 0.4f, "a pad keeps a floor but moves little");
    fat.bassAtt = pad.bassAtt = 2.0f;
    fat.volAtt = pad.volAtt = fat.midAtt = pad.midAtt = fat.trebleAtt = pad.trebleAtt = 1.0f;  // a typical level for the song

    ReactionEngine loudRoutes;
    ReactionEngine quietRoutes;
    for (ReactionEngine* e : {&loudRoutes, &quietRoutes}) {
        e->routes[0] = {true, ReactSource::KickEnv, LfoTarget::Zoom, ReactMode::Follow, 0.5f, ModCurve::Linear, 0.0f, 0.0f};
    }
    fat.kickEnv = pad.kickEnv = 0.8f;
    loudRoutes.tick(fat, 1.0f / 60.0f);
    quietRoutes.tick(pad, 1.0f / 60.0f);
    const float base = SceneParams{}.zoom;
    const float loudMove = loudRoutes.apply(SceneParams{}).zoom - base;
    const float quietMove = quietRoutes.apply(SceneParams{}).zoom - base;
    check(loudMove > 0.0f && quietMove > 0.0f && quietMove < 0.4f * loudMove, "a route's audio part follows the drive");

    MotionField loud;
    MotionField quiet;
    for (int k = 0; k < 600; ++k) {
        loud.step(fat, 1.0f / 60.0f);
        quiet.step(pad, 1.0f / 60.0f);
    }
    check(quiet.state().energyRel < 0.4f * loud.state().energyRel, "the motion field's energy follows the drive");
    check(quiet.state().midRel < 0.4f * loud.state().midRel, "so does the drift's mid clock");
    check(loud.apply(SceneParams{}, false, 1.0f).speed > quiet.apply(SceneParams{}, false, 1.0f).speed, "and the speed it adds");
    // Speed used to follow the loudness whatever the dial said. It is under the Motion dial like every
    // other term: at 0 the music leaves the user's Speed alone.
    SceneParams undriven;
    undriven.motionAmount = 0.0f;
    undriven.speed = 1.3f;
    check(loud.apply(undriven, false, 1.0f).speed == 1.3f && quiet.apply(undriven, false, 1.0f).speed == 1.3f, "Motion at 0: the music does not touch Speed");
    // A steady passage sits on the song's own average: its size, density and trails stay the user's.
    GeodeSoundFrame steady = pad;
    steady.volAtt = steady.bassAtt = steady.midAtt = steady.trebleAtt = 1.0f;
    SceneParams dial;
    dial.motionAmount = 1.0f;
    MotionField level;
    for (int k = 0; k < 600; ++k) level.step(steady, 1.0f / 60.0f);
    const SceneParams held = level.apply(dial, false, 1.0f);
    check(std::fabs(level.state().energySwing) < 1e-3f && std::fabs(level.state().midSwing) < 1e-3f, "a steady quiet passage has no swing");
    check(std::fabs(held.particleSize - dial.particleSize) < 1e-3f && std::fabs(held.density - dial.density) < 1e-3f &&
              std::fabs(held.fluidStirrerSpeed - dial.fluidStirrerSpeed) < 1e-3f,
          "so quiet music does not shrink the particles, the density or the stirrer");
    GeodeSoundFrame swell = steady;
    swell.volAtt = 1.8f;
    MotionField rising;
    for (int k = 0; k < 600; ++k) rising.step(swell, 1.0f / 60.0f);
    check(rising.state().energySwing > 0.0f && rising.apply(dial, false, 1.0f).particleSize > held.particleSize,
          "and a swell above the average still grows them");

    SceneParams p;
    p.camFly = 1.0f;
    CameraRig loudRig;
    CameraRig quietRig;
    float loudSwing = 0.0f;
    float quietSwing = 0.0f;
    fat.midTime = pad.midTime = 3.0f;
    for (int k = 0; k < 600; ++k) {
        fat.midTime = pad.midTime = 0.05f * static_cast<float>(k);
        loudRig.step(p, {}, 1.0f / 60.0f, false, true, &fat);
        quietRig.step(p, {}, 1.0f / 60.0f, false, true, &pad);
        loudSwing = std::max(loudSwing, std::fabs(loudRig.pose().yaw));
        quietSwing = std::max(quietSwing, std::fabs(quietRig.pose().yaw));
    }
    check(loudSwing > 0.0f && quietSwing < 0.5f * loudSwing, "the camera's own moves follow the drive");

    GeodeSoundFrame gone = pad;
    gone.drive = NAN;
    check(geode::sound::driveGain(gone) == 1.0f, "a non-finite drive is neutral, never a stall");
}

// Endless zoom is a stage of its own: it never moves the camera, so Fly speed means the same with it on.
void endlessZoomLeavesTheRigAlone() {
    CameraRig still;
    CameraRig diving;
    SceneParams p;
    SceneParams q = p;
    q.endlessZoom = true;
    q.endlessZoomSpeed = 1.2f;
    const GeodeSoundFrame f = playing();
    for (int i = 0; i < 600; ++i) {
        still.step(p, {}, 1.0f / 60.0f, false, true, &f);
        diving.step(q, {}, 1.0f / 60.0f, false, true, &f);
    }
    check(diving.pose().travel == still.pose().travel && diving.pose().yaw == still.pose().yaw, "Endless zoom does not fly the camera");
}

// Reduced motion slows the dive once, in safety::apply, in both directions.
void endlessZoomReducedMotionScalesTheDiveOnce() {
    SceneParams p;
    p.endlessZoom = true;
    for (const float speed : {1.0f, -1.0f}) {
        p.endlessZoomSpeed = speed;
        const float normal = endless::travel(safety::apply(p, false).endlessZoomSpeed, endless::Follow{});
        const float reduced = endless::travel(safety::apply(p, true).endlessZoomSpeed, endless::Follow{});
        check(normal == speed && std::fabs(reduced - speed * safety::kReducedMotionScale) < 1e-6f, "reduced motion scales the dive exactly once");
    }
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

// #369: a layer and Liquid's next style, each staged through the one ScenePrep, take turns.
void scenePrepSecondariesTakeTurns() {
    ScenePrep prep;
    check(!prep.yieldsTo("layer_style", "born_style"), "with nothing staged, nobody waits");
    prep.retarget("born_style");
    check(prep.yieldsTo("layer_style", "born_style"), "the layer waits while the next style is staged");
    check(!prep.yieldsTo("born_style", "layer_style"), "the next style keeps its own staging");
    check(!prep.yieldsTo("layer_style", ""), "a target nobody asks for any more is taken over");
    check(!prep.yieldsTo("layer_style", "other_style"), "only the other secondary's live staging is waited on");
    check(!prep.yieldsTo("born_style", "born_style"), "a scene never waits for itself");

    // Both ask every frame; each frame the layer asks first. Before the fix each ask retargeted and
    // cleared the other, so neither finished. Now the first to start finishes, then the other runs.
    ScenePrep turns;
    int layerAttempts = 0;
    int bornAttempts = 0;
    bool layerBuilt = false;
    bool bornBuilt = false;
    for (int frame = 0; frame < 40 && !(layerBuilt && bornBuilt); ++frame) {
        auto ask = [&](const std::string& id, const std::string& other, int& attempts, bool& built) {
            if (built || turns.yieldsTo(id, other)) return;
            turns.retarget(id);
            if (turns.next(true) == ScenePrep::Step::Attempt && ++attempts == 2) {
                built = true;
                turns.done();
            }
        };
        ask("layer_style", bornBuilt ? "" : "born_style", layerAttempts, layerBuilt);
        ask("born_style", layerBuilt ? "" : "layer_style", bornAttempts, bornBuilt);
    }
    check(layerBuilt && bornBuilt, "both secondary targets finish staging");
    check(layerAttempts == 2 && bornAttempts == 2, "neither restarts the other's attempts");
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

// ── Customize › Music's ready-made recipes (EventRecipe in ReactionDna), pinned to the old Kotlin table ──

const std::string kShaderStyle = "plasma";
const std::string kFluidStyle = "fluid";

struct RecipeRow {
    ReactSource source;
    LfoTarget target;
    ReactMode mode;
    float amount;
    float attack;
    float release;
};

struct RecipeExpect {
    EventRecipe recipe;
    int count;
    std::array<RecipeRow, kMaxRecipeRoutes> rows;
};

constexpr ReactMode kF = ReactMode::Follow;
constexpr ReactMode kS = ReactMode::Step;
constexpr ReactMode kP = ReactMode::Spin;
using RS = ReactSource;
using LT = LfoTarget;
using ER = EventRecipe;

// Every recipe's routes exactly as dev.geode.render.EventRecipe held them before the port (follow, step
// over `seconds` both ways, spin at 0.5 s both ways).
const std::array<RecipeExpect, kEventRecipeCount> kRecipeExpect{{
    {ER::KickPunch, 2, {{{RS::KickEnv, LT::CamPush, kF, 0.4f, 0.015f, 0.25f}, {RS::KickEnv, LT::Zoom, kF, 0.12f, 0.015f, 0.22f}}}},
    {ER::SnareCrack, 1, {{{RS::SnareEnv, LT::ChromaAb, kF, 0.5f, 0.01f, 0.18f}}}},
    {ER::HatShimmer, 2, {{{RS::HatEnv, LT::ParticleSize, kF, 0.3f, 0.01f, 0.12f}, {RS::HatEnv, LT::Bloom, kF, 0.2f, 0.01f, 0.15f}}}},
    {ER::SubWeight, 2, {{{RS::SubAtt, LT::Warp, kF, 0.35f, 0.06f, 0.4f}, {RS::SubAtt, LT::FluidForce, kF, 0.5f, 0.05f, 0.4f}}}},
    {ER::LoudnessGlow, 2, {{{RS::Loudness, LT::Brightness, kF, 0.15f, 0.1f, 0.5f}, {RS::Loudness, LT::Bloom, kF, 0.25f, 0.1f, 0.5f}}}},
    {ER::SwellBreathe, 2, {{{RS::Intensity, LT::CamFov, kF, 0.25f, 0.4f, 0.8f}, {RS::Intensity, LT::Vignette, kF, -0.3f, 0.4f, 0.8f}}}},
    {ER::BuildupRush, 3, {{{RS::EnergyTrend, LT::Speed, kF, 0.5f, 0.5f, 0.5f}, {RS::EnergyTrend, LT::CamShake, kF, 0.3f, 0.5f, 0.3f},
                          {RS::EnergyTrend, LT::Rotation, kP, 0.3f, 0.5f, 0.5f}}}},
    {ER::SectionColours, 2, {{{RS::HardCut, LT::ColorShift, kS, 0.33f, 1.2f, 1.2f}, {RS::HardCut, LT::PaletteMix, kS, 0.5f, 1.2f, 1.2f}}}},
    {ER::BeatTurn, 1, {{{RS::Kick, LT::CamRoll, kS, 0.15f, 0.08f, 0.08f}}}},
    {ER::TimbreTurn, 1, {{{RS::Kick8, LT::CamYaw, kS, 0.4f, 0.8f, 0.8f}}}},
    {ER::PitchTint, 1, {{{RS::NoteChange, LT::ColorShift, kS, 0.08f, 0.25f, 0.25f}}}},
    {ER::FluxTexture, 2, {{{RS::Flux, LT::Ripple, kF, 0.3f, 0.03f, 0.3f}, {RS::Flux, LT::Twist, kF, 0.2f, 0.03f, 0.3f}}}},
    {ER::StereoSway, 2, {{{RS::StereoPan, LT::CamYaw, kF, 0.35f, 0.3f, 0.6f}, {RS::StereoWidth, LT::CamOrbit, kF, 0.3f, 0.3f, 0.6f}}}},
    {ER::BarHue, 1, {{{RS::Kick4, LT::ColorShift, kS, 0.1f, 0.2f, 0.2f}}}},
    {ER::HitChaos, 2, {{{RS::Onset, LT::Turbulence, kF, 0.3f, 0.01f, 0.3f}, {RS::Onset, LT::CamShake, kF, 0.15f, 0.01f, 0.3f}}}},
    {ER::KickSwap, 2, {{{RS::KickToggle, LT::ColorShift, kF, 0.25f, 0.06f, 0.06f}, {RS::KickToggle, LT::PaletteMix, kF, 0.6f, 0.06f, 0.06f}}}},
    {ER::KickAngle, 2, {{{RS::KickRandom, LT::CamYaw, kF, 0.35f, 0.15f, 0.15f}, {RS::KickRandom, LT::CamPitch, kF, 0.15f, 0.15f, 0.15f}}}},
    {ER::SnareRoll, 1, {{{RS::Snare2, LT::CamRoll, kS, 0.12f, 0.15f, 0.15f}}}},
    {ER::MelodyLift, 1, {{{RS::PitchHeight, LT::CamPitch, kF, 0.3f, 0.3f, 0.6f}}}},
    {ER::NoteHue, 1, {{{RS::PitchClass, LT::ColorShift, kF, 0.5f, 0.15f, 0.4f}}}},
    {ER::GlideTwist, 1, {{{RS::Glide, LT::Twist, kF, 0.4f, 0.05f, 0.3f}}}},
    {ER::VibratoShimmer, 2, {{{RS::Vibrato, LT::Ripple, kF, 0.3f, 0.1f, 0.3f}, {RS::Vibrato, LT::ChromaAb, kF, 0.2f, 0.1f, 0.3f}}}},
    {ER::SlideSmear, 2, {{{RS::Legato, LT::Warp, kF, 0.25f, 0.05f, 0.5f}, {RS::Legato, LT::TrailLength, kF, 0.4f, 0.05f, 0.5f}}}},
    {ER::SilenceFade, 2, {{{RS::Silence, LT::Brightness, kF, -0.4f, 0.3f, 1.5f}, {RS::Silence, LT::Vignette, kF, 0.4f, 0.3f, 1.5f}}}},
    {ER::ReturnFlash, 2, {{{RS::Returned, LT::Bloom, kF, 0.6f, 0.02f, 1.5f}, {RS::Returned, LT::CamPush, kF, 0.5f, 0.02f, 1.5f}}}},
    {ER::BassPull, 1, {{{RS::BassPresence, LT::Zoom, kF, 0.2f, 1.0f, 2.0f}}}},
    {ER::ClockOrbit, 2, {{{RS::MidTime, LT::CamOrbit, kP, 0.15f, 0.5f, 0.5f}, {RS::MidTime, LT::Rotation, kP, 0.1f, 0.5f, 0.5f}}}},
    {ER::BarTurn, 1, {{{RS::Downbeat, LT::CamYaw, kS, 0.25f, 0.6f, 0.6f}}}},
    {ER::ScaleMood, 3, {{{RS::ScaleBrightness, LT::Brightness, kF, 0.15f, 1.0f, 1.0f}, {RS::ScaleTension, LT::Turbulence, kF, 0.3f, 1.0f, 1.0f},
                        {RS::KeyHue, LT::ColorShift, kF, 0.5f, 1.5f, 1.5f}}}},
    {ER::BuildAndDrop, 3, {{{RS::BuildUp, LT::Speed, kF, 0.5f, 0.5f, 0.5f}, {RS::Drop, LT::Bloom, kF, 0.7f, 0.02f, 2.0f},
                           {RS::Drop, LT::CamPush, kF, 0.6f, 0.02f, 1.5f}}}},
}};

ReactRoute recipeRoute(ReactSource s, LfoTarget t, float amount = 0.3f) {
    ReactRoute r;
    r.enabled = true;
    r.source = s;
    r.target = t;
    r.amount = amount;
    return r;
}

RouteArray routesOf(std::initializer_list<ReactRoute> list, int& count) {
    RouteArray out{};
    count = 0;
    for (const ReactRoute& r : list) out[static_cast<size_t>(count++)] = r;
    return out;
}

bool sameRoute(const ReactRoute& a, const ReactRoute& b) {
    return a.enabled == b.enabled && a.source == b.source && a.target == b.target && a.mode == b.mode && a.amount == b.amount &&
           a.curve == b.curve && a.attackSeconds == b.attackSeconds && a.releaseSeconds == b.releaseSeconds;
}

void everyRecipeHoldsItsOldRoutes() {
    for (int i = 0; i < kEventRecipeCount; ++i) {
        const RecipeExpect& e = kRecipeExpect[static_cast<size_t>(i)];
        check(static_cast<int>(e.recipe) == i, "the expected table is in wire order");
        const RecipeRoutes got = recipeRoutes(e.recipe);
        check(got.count == e.count, "each recipe holds its old number of routes");
        for (int k = 0; k < std::min(got.count, e.count); ++k) {
            const ReactRoute& r = got.routes[static_cast<size_t>(k)];
            const RecipeRow& x = e.rows[static_cast<size_t>(k)];
            check(r.enabled && r.curve == ModCurve::Linear, "recipe routes are enabled and linear");
            check(r.source == x.source && r.target == x.target && r.mode == x.mode, "recipe route source, target and mode");
            check(r.amount == x.amount && r.attackSeconds == x.attack && r.releaseSeconds == x.release,
                  "recipe route amount, attack and release");
        }
    }
    check(recipeRoutes(static_cast<EventRecipe>(kEventRecipeCount)).count == 0, "an unknown recipe holds nothing");
}

void everyRecipeFitsTheMatrixAndListensToSomething() {
    std::set<std::string> groups;
    for (int i = 0; i < kEventRecipeCount; ++i) {
        const auto recipe = static_cast<EventRecipe>(i);
        const RecipeRoutes all = recipeRoutes(recipe);
        check(all.count > 0 && all.count <= ReactionEngine::kRoutes, "every recipe has routes and fits the matrix");
        for (int k = 0; k < all.count; ++k) {
            const ReactRoute& r = all.routes[static_cast<size_t>(k)];
            check(r.target != LfoTarget::None, "every recipe route moves something");
            check(r.amount >= -2.0f && r.amount <= 2.0f, "recipe amounts within what native accepts");
            groups.insert(reactSourceInfo(r.source).group);
        }
        check(recipeRoutesFor(recipe, kShaderStyle).count > 0, "every recipe does something on a shader style");
    }
    const std::set<std::string> want = {"LEVELS", "DRUMS", "EVENTS", "MELODY", "CLOCKS", "TEXTURE", "RHYTHM", "HARMONY", "FORM"};
    check(groups == want, "the recipes together listen to every sound group but the EQ");
}

void recipeOnThenOffLeavesTheListAsItWas() {
    int n = 0;
    const RouteArray start = routesOf({recipeRoute(RS::Bass, LT::Zoom)}, n);
    RouteArray on;
    const int onCount = toggleRecipe(ER::KickPunch, start, n, kShaderStyle, on);
    check(onCount == 3, "on appends the recipe's two routes");
    check(sameRoute(on[0], start[0]), "on keeps what was there first");
    check(sameRoute(on[1], recipeRoutes(ER::KickPunch).routes[0]) && sameRoute(on[2], recipeRoutes(ER::KickPunch).routes[1]),
          "on adds the recipe's routes after, in order");
    check(recipeIsOn(ER::KickPunch, on, onCount, kShaderStyle), "the recipe reads as on");
    RouteArray off;
    const int offCount = toggleRecipe(ER::KickPunch, on, onCount, kShaderStyle, off);
    check(!recipeIsOn(ER::KickPunch, off, offCount, kShaderStyle), "the recipe reads as off");
    check(offCount == n && sameRoute(off[0], start[0]), "off leaves the list as it was");
    check(!off[1].enabled, "slots past the count are disabled");
}

void recipeReplacesTheSameSlotInsteadOfStacking() {
    int n = 0;
    const RouteArray start = routesOf({recipeRoute(RS::KickEnv, LT::CamPush, 0.9f)}, n);
    check(!recipeIsOn(ER::KickPunch, start, n, kShaderStyle), "half a recipe is not on");
    RouteArray on;
    const int count = toggleRecipe(ER::KickPunch, start, n, kShaderStyle, on);
    int kickPush = 0;
    for (int i = 0; i < count; ++i) {
        if (on[static_cast<size_t>(i)].source == RS::KickEnv && on[static_cast<size_t>(i)].target == LT::CamPush) {
            kickPush += 1;
            check(on[static_cast<size_t>(i)].amount == 0.4f, "the recipe's amount replaces the old one");
        }
    }
    check(count == 2 && kickPush == 1, "one route per source and target");
    // A recipe whose slots are all taken by other amounts is on: off then takes those routes out.
    int m = 0;
    const RouteArray taken = routesOf({recipeRoute(RS::SnareEnv, LT::ChromaAb, 1.5f), recipeRoute(RS::Bass, LT::Zoom)}, m);
    check(recipeIsOn(ER::SnareCrack, taken, m, kShaderStyle), "on means every slot is held, whatever the amount");
    RouteArray off;
    const int offCount = toggleRecipe(ER::SnareCrack, taken, m, kShaderStyle, off);
    check(offCount == 1 && off[0].source == RS::Bass, "off takes the slot's route out, whoever set it");
}

void recipeOnlyAddsWhatTheStyleCanShow() {
    const RecipeRoutes shader = recipeRoutesFor(ER::SubWeight, kShaderStyle);
    check(shader.count == 1 && shader.routes[0].target == LT::Warp, "fluid splat force has nothing to push on a shader style");
    const RecipeRoutes fluid = recipeRoutesFor(ER::SubWeight, kFluidStyle);
    check(fluid.count == 2 && fluid.routes[1].target == LT::FluidForce, "a fluid style takes both");
    RouteArray on;
    const int count = toggleRecipe(ER::SubWeight, RouteArray{}, 0, kShaderStyle, on);
    check(count == 1 && on[0].target == LT::Warp, "toggling on adds only what the style shows");
    check(recipeIsOn(ER::SubWeight, on, count, kShaderStyle), "and then reads as on");
    check(!targetReaches(LT::Speed, "milkdrop") && targetReaches(LT::Speed, kShaderStyle), "MilkDrop presets pace themselves");
    check(targetReaches(LT::PaletteMix, kShaderStyle) && !targetReaches(LT::PaletteMix, kFluidStyle), "palette blend is a shader look");
    check(targetReaches(LT::CamYaw, kFluidStyle) && targetReaches(LT::Bloom, "milkdrop"), "camera and composite targets are universal");
    check(!targetReaches(LT::ParticleSize, kShaderStyle) && targetReaches(LT::ParticleSize, kFluidStyle), "particle size");
    // A shader style's trail is the feedback pass's sustain; a fluid has none of its own to lengthen.
    check(targetReaches(LT::TrailLength, kShaderStyle) && !targetReaches(LT::TrailLength, kFluidStyle) &&
              !targetReaches(LT::Turbulence, kFluidStyle),
          "trail and turbulence scopes");
}

void aFullMatrixRefusesARecipeRatherThanDroppingRoutes() {
    RouteArray full{};
    for (int i = 0; i < ReactionEngine::kRoutes; ++i) full[static_cast<size_t>(i)] = recipeRoute(RS::High, LT::Warp);
    check(!recipeFits(ER::KickPunch, full, ReactionEngine::kRoutes, kShaderStyle), "a full matrix has no room");
    RouteArray out;
    const int count = toggleRecipe(ER::KickPunch, full, ReactionEngine::kRoutes, kShaderStyle, out);
    bool same = count == ReactionEngine::kRoutes;
    for (int i = 0; i < count; ++i) same = same && sameRoute(out[static_cast<size_t>(i)], full[static_cast<size_t>(i)]);
    check(same, "a refused recipe leaves the list as it was");
    // One slot short of full: a two-route recipe does not fit, one that replaces a held slot does.
    RouteArray almost = full;
    almost[0] = recipeRoute(RS::KickEnv, LT::CamPush);
    const int eleven = ReactionEngine::kRoutes - 1;
    check(!recipeFits(ER::LoudnessGlow, almost, eleven, kShaderStyle), "two new routes do not fit in one free slot");
    check(recipeFits(ER::KickPunch, almost, eleven, kShaderStyle), "a recipe that reuses a held slot needs only one");
    check(!recipeIsOn(ER::KickPunch, RouteArray{}, 0, kShaderStyle) && recipeFits(ER::KickPunch, RouteArray{}, 0, kShaderStyle),
          "an empty list has room and nothing on");
}

// #355: a hand mask only counts while the user has a list of their own. A stale or unreadable saved list
// used to push HAND_ALL with the DNA playing, so the director claimed no source and played no route.
void handMaskCountsOnlyWithAUserList() {
    check(hand::global() == 0u, "the process starts with nothing the hand's");
    hand::setGlobal(hand::kAll, false);
    check(hand::global() == 0u, "HAND_ALL with no user list is no mask");
    check(hand::sources(hand::global()).none(), "so the director keeps every source");
    hand::setGlobal(hand::kAll, true);
    check(hand::global() == hand::kAll, "with a user list the mask stands");
    check(hand::sources(hand::global()).all(), "and HAND_ALL claims every source");
    hand::setGlobal(hand::bit(Moment::Kick) | (1u << 20), true);
    check(hand::global() == hand::bit(Moment::Kick), "one moment stands, and bits past the moments are dropped");
    check(hand::sources(hand::global()).any() && !hand::sources(hand::global()).all(), "it claims only its own sources");
    hand::setGlobal(hand::kAll, false);
    check(hand::global() == 0u, "and dropping the list clears it again");
}

}  // namespace

// A steady 4/4 clock at `bpm`: beatCount and beatPhase advance with time, locked (beatConf 1).
GeodeSoundFrame clockAt(double beats, float bpm, float conf = 1.0f) {
    GeodeSoundFrame f = silence();
    f.bpm = bpm;
    f.beatConf = conf;
    f.beatCount = static_cast<float>(std::fmod(std::floor(beats), 4096.0));
    f.beatPhase = static_cast<float>(beats - std::floor(beats));
    return f;
}

void beatTimedLfoLocksToTheMusicClock() {
    for (const float n : {4.0f, 8.0f, 16.0f, 32.0f}) {
        LfoEngine e;
        e.configs[0] = {true, ModSource::Lfo, LfoTarget::Sway, LfoWave::Saw, 2.0f, 1.0f, ModPolarity::Bipolar, ModCurve::Linear, n};
        const double dt = 1.0 / 60.0;
        double beats = 100.0;  // an arbitrary start, still on an n-beat multiple or not
        float prev = 0.0f;
        int wraps = 0;
        for (int i = 0; i < 60 * 120; ++i) {  // 120 s at 120 BPM = 240 beats
            beats += dt * 120.0 / 60.0;
            const float v = e.tick(static_cast<float>(dt), clockAt(beats, 120.0f))[0];
            if (i > 0 && v < prev - 1.0f) ++wraps;
            prev = v;
            const double pos = std::fmod(beats, static_cast<double>(n)) / n;  // saw = 2 pos - 1
            if (i > 600) check(std::fabs(v - (2.0 * pos - 1.0)) < 0.02 || pos < 0.02 || pos > 0.98, "synced saw sits at the music clock's phase");
        }
        check(wraps == static_cast<int>(240.0 / n) || wraps == static_cast<int>(240.0 / n) + 1 || wraps == static_cast<int>(240.0 / n) - 1,
              "a synced wave completes one cycle per n beats");
    }
}

void beatTimedLfoIsAtZeroOnMultiples() {
    LfoEngine e;
    e.configs[0] = {true, ModSource::Lfo, LfoTarget::Sway, LfoWave::Sine, 2.0f, 1.0f, ModPolarity::Bipolar, ModCurve::Linear, 4.0f};
    for (const double b : {0.0, 4.0, 8.0, 40.0}) {
        e = LfoEngine{};
        e.configs[0] = {true, ModSource::Lfo, LfoTarget::Sway, LfoWave::Sine, 2.0f, 1.0f, ModPolarity::Bipolar, ModCurve::Linear, 4.0f};
        const float v = e.tick(1.0f / 60.0f, clockAt(b, 120.0f))[0];
        check(std::fabs(v) < 1e-3f, "a 4-beat sine is at phase 0 on beat multiples of 4");
        const float q = e.tick(1.0f / 60.0f, clockAt(b + 1.0, 120.0f))[0];
        check(std::fabs(q - 1.0f) < 1e-2f, "a 4-beat sine peaks one beat in");
    }
}

void beatTimedLfoSurvivesTempoChangeAndSilence() {
    LfoEngine e;
    e.configs[0] = {true, ModSource::Lfo, LfoTarget::Sway, LfoWave::Triangle, 2.0f, 1.0f, ModPolarity::Bipolar, ModCurve::Linear, 8.0f};
    const double dt = 1.0 / 60.0;
    double beats = 16.0;
    float bpm = 100.0f;
    float prev = e.tick(static_cast<float>(dt), clockAt(beats, bpm))[0];
    float worst = 0.0f;
    for (int i = 0; i < 60 * 40; ++i) {
        if (i == 600) bpm = 150.0f;  // tempo change keeps lock: the clock itself speeds up
        const bool silent = i >= 1200 && i < 1500;
        if (!silent) beats += dt * bpm / 60.0;
        GeodeSoundFrame f = silent ? silence() : clockAt(beats, bpm);
        if (silent) f.bpm = 0.0f;
        const float v = e.tick(static_cast<float>(dt), f)[0];
        worst = std::max(worst, std::fabs(v - prev));
        prev = v;
        if (i == 1499) beats += 1.7;  // the beat returns somewhere else: eased, not a jump
    }
    // Triangle at 8 beats, 150 BPM: slope is at most 2 * 4 / (8 / 2.5 s) / 60 per frame; allow headroom.
    check(worst < 0.15f, "no discontinuity across tempo change, silence and the beat returning");
}

void randomLfoStepsOnTheBeatAndGlideIsContinuous() {
    LfoEngine a, g;
    a.configs[0] = {true, ModSource::Lfo, LfoTarget::Sway, LfoWave::Random, 2.0f, 1.0f, ModPolarity::Bipolar, ModCurve::Linear, 4.0f};
    g.configs[0] = {true, ModSource::Lfo, LfoTarget::Sway, LfoWave::RandomGlide, 2.0f, 1.0f, ModPolarity::Bipolar, ModCurve::Linear, 4.0f};
    GeodeSoundFrame f0 = clockAt(0.0, 120.0f);
    a.tick(1.0f / 60.0f, f0);
    g.tick(1.0f / 60.0f, f0);
    const double dt = 1.0 / 60.0;
    double beats = 0.0;
    float la = a.tick(0.0f, f0)[0], lg = g.tick(0.0f, f0)[0];
    int steps = 0;
    std::set<int> targets;
    float worstGlide = 0.0f;
    for (int i = 0; i < 60 * 32; ++i) {  // 64 beats = 16 cycles
        beats += dt * 2.0;
        const GeodeSoundFrame f = clockAt(beats, 120.0f);
        const float va = a.tick(static_cast<float>(dt), f)[0];
        const float vg = g.tick(static_cast<float>(dt), f)[0];
        if (std::fabs(va - la) > 1e-6f) {
            ++steps;
            const double nearest = std::round(beats / 4.0) * 4.0;
            check(std::fabs(beats - nearest) < 0.2, "sample & hold steps on the cycle's beat");
        }
        worstGlide = std::max(worstGlide, std::fabs(vg - lg));
        la = va;
        lg = vg;
        if (std::fmod(beats, 4.0) > 3.9) targets.insert(static_cast<int>(vg * 1000.0f));
    }
    check(steps >= 15 && steps <= 17, "sample & hold steps once per cycle");
    check(worstGlide < 0.1f, "random glide never jumps");
    check(targets.size() >= 12, "random glide picks a new target each cycle");
    LfoEngine g2;
    g2.configs[0] = g.configs[0];
    g2.tick(1.0f / 60.0f, f0);
    float again = 0.0f;
    for (int i = 0; i < 100; ++i) again = g2.tick(static_cast<float>(dt), clockAt(2.0 + i * dt * 2.0, 120.0f))[0];
    check(std::isfinite(again), "random glide is deterministic and finite");
}

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
    backwardTravelUnwindsAndTheTurnIsContinuous();
    reverseDoesNotChangeForwardLooks();
    flatStylesDiveBothWays();
    reducedMotionSlowsTravel();
    oddFrameTimesStayFiniteAndBounded();
    zeroDtFreezesTheCameraAndHugeDtIsOneClampedStep();
    stillStylesKeepTheBankOutOfTheFlatRoll();
    hardCutsJumpWithoutATweenAndKeepTheirGap();
    silenceAndTheMotionDialStillTheMusicMoves();
    traumaShakeIsBoundedAndFinite();
    motionFieldVariantsDiffer();
    flowTimeIsFlowPhaseAtSpeedOne();
    flowTimeRunsAtSpeedAndNeverJumps();
    followerScalesASoundClockWithoutJumps();
    motionFieldDriftSpinIsBoundedAndContinuous();
    motionFieldReducedMotionScalesRatesOnce();
    motionFieldDriftWrapNeverPopsAStyle();
    sourceCatalogueIsCompleteAndUnique();
    endlessZoomIsOneSeamlessTunnel();
    sustainHoldsLongerAtLongerTrails();
    endlessZoomPhaseWrapsForEverInBothDirections();
    endlessZoomIgnoresHitsAndFollowsOnlyTheLongRun();
    endlessZoomTravelsTheSameAtAnyFrameRate();
    theClockStopsInSilence();
    driveScalesTheMagnitudeOfMotion();
    endlessZoomLeavesTheRigAlone();
    endlessZoomReducedMotionScalesTheDiveOnce();
    buildBudgetBoundsAFrame();
    scenePrepRetargetsAndCaps();
    scenePrepSecondariesTakeTurns();
    stagedBuildFinishesInBoundedFrames();
    everyRecipeHoldsItsOldRoutes();
    everyRecipeFitsTheMatrixAndListensToSomething();
    recipeOnThenOffLeavesTheListAsItWas();
    recipeReplacesTheSameSlotInsteadOfStacking();
    recipeOnlyAddsWhatTheStyleCanShow();
    aFullMatrixRefusesARecipeRatherThanDroppingRoutes();
    handMaskCountsOnlyWithAUserList();
    beatTimedLfoLocksToTheMusicClock();
    beatTimedLfoIsAtZeroOnMultiples();
    beatTimedLfoSurvivesTempoChangeAndSilence();
    randomLfoStepsOnTheBeatAndGlideIsContinuous();
    if (failures == 0) std::printf("viz_math_test: all passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
