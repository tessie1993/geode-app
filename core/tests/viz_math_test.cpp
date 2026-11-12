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

#include "viz/BandTrim.hpp"
#include "viz/BuildReport.hpp"
#include "viz/CameraRig.hpp"
#include "viz/Catalogue.hpp"
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
    const float beatsPerSecond = 141.0f / 60.0f;
    for (int i = 0; i < 600; ++i) {  // 10 s at 141 bpm: 23.5 wraps
        const float t = static_cast<float>(i) * dt;
        f.beatPhase = beatsPerSecond * t - std::floor(beatsPerSecond * t);
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
    check(std::fabs(travelled - 9.0f * beatsPerSecond) < 0.1f, "and turns once per beat (21.15 beats in the checked 9 s)");
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
    check(s.value(ReactSource::Transient) == 0.0f, "no transient without one");
    f.transient = 0.2f;
    s.step(f, 1.0f / 60.0f);
    check(s.value(ReactSource::Transient) >= 0.5f, "a transient is an event like a hit");
}

void sourcesReadTheirFieldsInRange() {
    static_assert(kReactSourceCount == 138, "138 sources: append only, they are saved by name and ordinal");
    ReactSignals s;
    GeodeSoundFrame f = silence();
    f.bass = 1.0f;
    f.low = 0.7f;
    f.stereoPan = -0.4f;
    f.eq[9] = 1.0f;
    f.eq[15] = 3.0f;
    f.fadeGain = 0.25f;
    f.transientEnv = 0.6f;
    f.transientSharpness = 0.8f;
    f.bassTime = 0.0f;
    s.step(f, 0.1f);
    f.bassTime = 0.1f;  // one second of clock per second
    s.step(f, 0.1f);
    check(std::fabs(s.value(ReactSource::Bass) - 0.5f) < 1e-5f, "a typical relative level reads 0.5");
    check(std::fabs(s.value(ReactSource::Low) - 0.7f) < 1e-5f, "a shaped level reads as it is");
    check(std::fabs(s.value(ReactSource::StereoPan) + 0.4f) < 1e-5f, "pan keeps its sign");
    check(std::fabs(s.value(ReactSource::FadeGain) - 0.25f) < 1e-5f, "the player's fade gain reads as it is");
    check(std::fabs(s.value(ReactSource::TransientEnv) - 0.6f) < 1e-5f && std::fabs(s.value(ReactSource::TransientSharpness) - 0.8f) < 1e-5f,
          "the transient's envelope and sharpness read as they are");
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

// The bar counts (the owner: "react to transients but also to longer things like 8 beats or 16"): counted on the
// music's own downbeats, the bar in the phrase read from the phrase phase, still while no beat is heard. Eight
// bars of a locked beat at two seconds a bar (no tempo is meant: any bar length counts the same).
void barCountsFollowTheMusicsBars() {
    ReactSignals s;
    GeodeSoundFrame f = silence();
    f.beatConf = 0.8f;
    for (int i = 0; i < 120; ++i) s.step(f, 1.0f / 60.0f);
    check(s.value(ReactSource::Bar4Hold) == 0.0f && s.value(ReactSource::BarFlip) == 0.0f && s.value(ReactSource::Bar2Pulse) == 0.0f,
          "no downbeat, no count");
    std::array<float, 8> barHolds{};
    std::array<float, 8> bar4Holds{};
    std::array<bool, 8> bar4Pulsed{};
    std::array<bool, 8> bar2Pulsed{};
    for (int bar = 0; bar < 8; ++bar) {
        const float bar4Before = s.value(ReactSource::Bar4Pulse);
        const float bar2Before = s.value(ReactSource::Bar2Pulse);
        f.downbeat = 0.8f;
        f.phrase = bar == 0 ? 0.8f : 0.0f;
        f.phrasePhase = static_cast<float>(bar) / 8.0f + 0.002f;
        f.barCount = static_cast<float>(37 + bar);
        f.phraseIndex = 5.0f;
        s.step(f, 1.0f / 60.0f);
        barHolds[static_cast<size_t>(bar)] = s.value(ReactSource::BarHold);
        bar4Holds[static_cast<size_t>(bar)] = s.value(ReactSource::Bar4Hold);
        bar4Pulsed[static_cast<size_t>(bar)] = bar4Before < 0.5f && s.value(ReactSource::Bar4Pulse) >= 0.5f;
        bar2Pulsed[static_cast<size_t>(bar)] = bar2Before < 0.5f && s.value(ReactSource::Bar2Pulse) >= 0.5f;
        f.downbeat = 0.0f;
        f.phrase = 0.0f;
        for (int i = 0; i < 119; ++i) s.step(f, 1.0f / 60.0f);
    }
    for (int bar = 0; bar < 8; ++bar) {
        const size_t b = static_cast<size_t>(bar);
        check(bar4Pulsed[b] == (bar % 4 == 0), "every 4 bars pulses on bars 1 and 5 of the phrase");
        check(bar2Pulsed[b] == (bar % 2 == 0), "every 2 bars pulses on the odd bars");
        check(barHolds[b] >= 0.0f && barHolds[b] < 1.0f, "a hold stays in 0..1");
        if (bar > 0) check(std::fabs(barHolds[b] - barHolds[b - 1]) > 0.2f, "every bar's hold lands somewhere new");
        if (bar % 4 != 0) check(bar4Holds[b] == bar4Holds[b - 1], "every 4 bars' hold keeps its value in between");
    }
    check(bar4Holds[4] != bar4Holds[0], "every 4 bars' hold steps on the fifth bar");
    check(s.value(ReactSource::BarFlip) == 0.0f && s.value(ReactSource::Bar2Flip) == 0.0f && s.value(ReactSource::Bar4Flip) == 0.0f &&
              s.value(ReactSource::PhraseFlip) == 1.0f,
          "flips toggle once per count: 8, 4 and 2 flips back to 0, one phrase to 1");
    // A downbeat the tracker is not sure of, or no beat at all, counts nothing: the holds stay still.
    const float held = s.value(ReactSource::Bar4Hold);
    f.beatConf = 0.05f;
    f.downbeat = 1.0f;
    f.phrasePhase = 0.001f;
    f.barCount = 45.0f;
    s.step(f, 1.0f / 60.0f);
    check(s.value(ReactSource::Bar4Hold) == held && s.value(ReactSource::PhraseFlip) == 1.0f, "an untrusted beat counts no bar");
    f = silence();
    for (int i = 0; i < 600; ++i) s.step(f, 1.0f / 60.0f);
    check(s.value(ReactSource::Bar4Hold) == held && s.value(ReactSource::Bar4Pulse) < 0.01f, "silence: the counts stay still");
}

// The melody line: a rising tune reads a positive contour, a falling one negative, a note marks each new note,
// presence follows whether a pitch sounds; the director's feeling reads through as it was heard.
void melodyLineAndFeelingRead() {
    ReactSignals s;
    GeodeSoundFrame f = silence();
    for (int i = 0; i < 60; ++i) {
        f.pitch = 60.0f + 7.0f * static_cast<float>(i) / 60.0f;  // up a fifth over a second
        s.step(f, 1.0f / 60.0f);
    }
    check(s.value(ReactSource::MelodyContour) > 0.3f, "a rising line reads a positive contour");
    check(s.value(ReactSource::MelodyPresence) > 0.9f, "a sounding tune reads present");
    for (int i = 0; i < 90; ++i) {
        f.pitch = 67.0f - 7.0f * static_cast<float>(i) / 90.0f;
        s.step(f, 1.0f / 60.0f);
    }
    check(s.value(ReactSource::MelodyContour) < -0.2f, "a falling line reads a negative contour");
    f.pitch = 0.0f;
    for (int i = 0; i < 300; ++i) s.step(f, 1.0f / 60.0f);
    check(std::fabs(s.value(ReactSource::MelodyContour)) < 0.01f && s.value(ReactSource::MelodyPresence) < 0.01f,
          "no tune: the contour rests and presence falls");
    f.pitch = 48.0f;  // a new line an octave below starts fresh, not as a fall
    s.step(f, 1.0f / 60.0f);
    check(std::fabs(s.value(ReactSource::MelodyContour)) < 0.01f, "a new line after a rest reads no leap");
    f.noteChange = 0.3f;
    s.step(f, 1.0f / 60.0f);
    check(s.value(ReactSource::MelodyNote) >= 0.5f, "a new note is an event like a hit");
    f.noteChange = 0.0f;
    f.reattack = 0.4f;
    for (int i = 0; i < 60; ++i) s.step(f, 1.0f / 60.0f);
    check(s.value(ReactSource::MelodyNote) >= 0.5f, "the same note struck again is a new note too");

    geode::sound::Emotion::State feel;
    feel.valence = -0.6f;
    feel.tension = 0.7f;
    feel.degreeAccent = 0.5f;
    s.step(f, 1.0f / 60.0f, &feel);
    check(std::fabs(s.value(ReactSource::Valence) + 0.6f) < 1e-6f && std::fabs(s.value(ReactSource::FeltTension) - 0.7f) < 1e-6f,
          "the mood and its tension read as heard");
    check(s.value(ReactSource::DegreeAccent) >= 0.5f, "a scale step change is an event");
    s.step(f, 1.0f / 60.0f);
    check(s.value(ReactSource::Valence) == 0.0f && s.value(ReactSource::FeltTension) == 0.0f, "without a feeling heard they rest");
}

// A settled Jump waits for its glide before the next edge counts: hats four a second cannot flicker it. Its output
// can turn back only when its held value changes, so it turns back no more often than once a glide.
void settledJumpWaitsForItsGlide() {
    ReactionEngine e;
    e.reset();
    e.routes[0] = {true, ReactSource::Hat, LfoTarget::Warp, ReactMode::JumpSettled, 1.0f, ModCurve::Linear, 0.2f, 0.4f};
    GeodeSoundFrame f = silence();
    float last = 0.0f;
    float lastStep = 0.0f;
    int turns = 0;
    for (int i = 0; i < 180; ++i) {  // three seconds, a hat every quarter second
        f.hat = i % 15 == 0 ? 1.0f : 0.0f;
        e.tick(f, 1.0f / 60.0f);
        const float step = e.output(0) - last;
        if (step * lastStep < 0.0f) ++turns;
        if (step != 0.0f) lastStep = step;
        last = e.output(0);
    }
    check(std::fabs(e.output(0)) > 0.0f, "a settled jump still jumps");
    check(turns <= 5, "twelve hats in three seconds turn a 0.6 s settled jump at most five times");
}

// The tension's rise: tightening reads up, resolving reads down.
void tensionRiseFollowsTheChords() {
    ReactSignals s;
    GeodeSoundFrame f = silence();
    f.tension = 0.2f;
    for (int i = 0; i < 1800; ++i) s.step(f, 1.0f / 60.0f);
    check(std::fabs(s.value(ReactSource::TensionRise)) < 0.01f, "steady tension neither rises nor eases");
    for (int i = 0; i < 120; ++i) {
        f.tension = 0.2f + 0.5f * static_cast<float>(i) / 120.0f;
        s.step(f, 1.0f / 60.0f);
    }
    check(s.value(ReactSource::TensionRise) > 0.5f, "tension climbing over two seconds reads rising");
    f.tension = 0.1f;
    for (int i = 0; i < 60; ++i) s.step(f, 1.0f / 60.0f);
    check(s.value(ReactSource::TensionRise) < -0.3f, "a chord resolving reads easing");
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
    for (int path : {0, 1, 4, 5, 6, 7, 8, 9}) {
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

// Strafe looks in at its track from the side; reversing carries the eye round the track point through
// the turn-around spring, so it ends looking in from the other side, never with a jump.
void strafeLooksInAndCranesRoundOnReverse() {
    CameraRig rig;
    SceneParams p;
    p.camPath = 10;
    p.camFly = 1.0f;
    for (int i = 0; i < 600; ++i) rig.step(p, {}, 1.0f / 60.0f, false);
    check(rig.pose().fwd[0] > 0.8f && rig.pose().pos[0] < -1.5f, "strafe stands off to one side and looks in");
    p.camFly = -1.0f;
    CameraPose prev = rig.pose();
    float maxStep = 0.0f;
    float maxTurn = 0.0f;
    for (int i = 0; i < 600; ++i) {
        rig.step(p, {}, 1.0f / 60.0f, false);
        const CameraPose& c = rig.pose();
        maxStep = std::fmax(maxStep, std::sqrt((c.pos[0] - prev.pos[0]) * (c.pos[0] - prev.pos[0]) +
                                               (c.pos[1] - prev.pos[1]) * (c.pos[1] - prev.pos[1]) +
                                               (c.pos[2] - prev.pos[2]) * (c.pos[2] - prev.pos[2])));
        maxTurn = std::fmax(maxTurn, angleBetween(c.fwd, prev.fwd));
        prev = c;
    }
    check(maxStep < 0.3f, "strafe's reverse cranes the eye round without a jump");
    check(maxTurn < 0.35f, "strafe's reverse turns the view smoothly");
    check(rig.pose().fwd[0] < -0.8f && rig.pose().pos[0] > 1.5f, "backward, strafe looks in from the other side");
}

// Every path flies through the travel wrap with no jump, a finite unit basis on every step, and the
// corkscrew's roll continuous through it.
void everyPathCrossesTheTravelWrapSmoothly() {
    for (int path = 0; path < CameraRig::kPathCount; ++path) {
        CameraRig rig;
        SceneParams p;
        p.camPath = path;
        p.camFly = 2.5f;
        rig.step(p, {}, 0.1f, false);
        CameraPose prev = rig.pose();
        float maxStep = 0.0f;
        float maxUpTurn = 0.0f;
        bool wrapped = false;
        bool unit = true;
        for (int i = 0; i < 21000; ++i) {
            rig.step(p, {}, 0.1f, false);
            const CameraPose& c = rig.pose();
            wrapped = wrapped || c.travel < prev.travel;
            // z is the travel and wraps with it, onto a world that tiles along z, so z is compared modulo the wrap.
            float dz = c.pos[2] - prev.pos[2];
            if (std::fabs(dz) > 0.5f * CameraRig::kTravelWrap) dz -= std::copysign(CameraRig::kTravelWrap, dz);
            maxStep = std::fmax(maxStep, std::sqrt((c.pos[0] - prev.pos[0]) * (c.pos[0] - prev.pos[0]) +
                                                   (c.pos[1] - prev.pos[1]) * (c.pos[1] - prev.pos[1]) + dz * dz));
            maxUpTurn = std::fmax(maxUpTurn, angleBetween(c.up, prev.up));
            auto dot = [](const std::array<float, 3>& a, const std::array<float, 3>& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
            unit = unit && std::isfinite(c.pos[0] + c.pos[1] + c.pos[2]) && std::fabs(dot(c.fwd, c.fwd) - 1.0f) < 1e-3f && std::fabs(dot(c.up, c.up) - 1.0f) < 1e-3f &&
                   std::fabs(dot(c.fwd, c.up)) < 1e-3f && std::fabs(dot(c.fwd, c.right)) < 1e-3f;
            prev = c;
        }
        check(wrapped, "the run crosses the travel wrap");
        check(unit, "every path keeps a finite, unit, orthogonal basis");
        check(maxStep < 1.0f, "no path jumps, at the travel wrap or anywhere");
        check(maxUpTurn < 0.5f, "the up vector never snaps, the corkscrew's roll included");
    }
}

// The corkscrew rolls over with its turn; reduced motion only leans it.
void corkscrewRollsAndReducedMotionLeans() {
    float fullMax = 0.0f;
    float reducedMax = 0.0f;
    for (bool reduced : {false, true}) {
        CameraRig rig;
        SceneParams p;
        p.camPath = 8;
        p.camFly = 1.0f;
        for (int i = 0; i < 1200; ++i) {
            rig.step(safety::apply(p, reduced), {}, 1.0f / 60.0f, reduced);
            float& m = reduced ? reducedMax : fullMax;
            m = std::fmax(m, std::fabs(rig.pose().roll));
        }
    }
    check(fullMax > 2.5f, "the corkscrew rolls over");
    check(reducedMax < 1.2f, "reduced motion never rolls the corkscrew over");
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
    p.camPath = 11;
    check(CameraRig::flatDive(p) == 0.0f, "pendulum does not dive");
    // Fly speed is still -1 here, so the travelling paths zoom out exactly as glide did.
    for (int path : {7, 8, 9, 10}) {
        p.camPath = path;
        check(CameraRig::flatDive(p) == out, "weave, corkscrew, swoop and strafe dive like the other travelling paths");
    }
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
    "chrys_bloom", "chrys_fall", "lucid_mandala", "milkdrop", "fluid", "silk", "bars", "spiral", "kifs",
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
    check(beatZooms * 2 <= kStyleCount, "most styles do not zoom on the kick (only the pulse and lens personalities do)");
}

// A zoom personality (Pulse, Lens) zooms on the beat it hears, by a fixed amount, beside its push or bulge.
void zoomPersonalitiesZoomOnTheBeat() {
    int seen = 0;
    for (const std::string& id : catalogue::allIds()) {
        for (int seed = 0; seed < 4; ++seed) {
            const StyleDna dna = buildStyleDna(id, seed, StyleTraits{0.0f, true});
            const auto zooms = [&dna](ReactSource beat) {
                for (int r = 0; r < dna.count; ++r) {
                    const ReactRoute& route = dna.routes[static_cast<size_t>(r)];
                    if (route.source == beat && route.target == LfoTarget::Zoom && route.amount == 0.2f) return true;
                }
                return false;
            };
            for (const Personality p : {dna.lead, dna.second}) {
                if (p != Personality::Pulse && p != Personality::Lens) continue;
                // Its own move is the first route on its target (the body's fisheye, if any, comes later).
                const LfoTarget own = p == Personality::Pulse ? LfoTarget::CamPush : LfoTarget::Fisheye;
                int first = -1;
                for (int r = 0; r < dna.count && first < 0; ++r) {
                    if (dna.routes[static_cast<size_t>(r)].target == own) first = r;
                }
                check(first >= 0 && zooms(dna.routes[static_cast<size_t>(first)].source), "a zoom personality zooms on its beat");
                ++seen;
            }
        }
    }
    check(seen > 20, "zoom personalities were checked");
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

// The owner: "make all styles use transient detection", inside "no hardcoded reactions". So every style's DNA
// (what plays until the user edits it, and what the user sees and edits in the matrix) holds a route from the
// transient detector, whatever its traits and seed, within the cap, and the layer comes last: without it the
// routes before it are what they were.
void everyStyleHearsTransients() {
    // The transient layer is the groove's last; the bar-count and melody layers follow it (everyStyleHearsTheSongsTime).
    const auto heard = [](const StyleDna& dna) {
        for (int r = 0; r < dna.count; ++r) {
            const ReactSource s = dna.routes[static_cast<size_t>(r)].source;
            if (s == ReactSource::Transient || s == ReactSource::TransientEnv) return r == dna.count - 3;
        }
        return false;
    };
    int styles = 0;
    for (const std::string& id : catalogue::allIds()) {
        ++styles;
        for (int seed = 0; seed < 4; ++seed) {
            for (const StyleTraits t : {StyleTraits{0.0f, false}, StyleTraits{0.0f, true}, StyleTraits{1.0f, true}, StyleTraits{-0.7f, false}}) {
                const StyleDna dna = buildStyleDna(id, seed, t);
                check(heard(dna), "every style's groove ends on one transient route");
                check(dna.count <= StyleDna::kMaxRoutes, "the transient layer keeps the DNA within its cap");
            }
        }
    }
    check(styles > 100, "the catalogue's styles were checked");
    std::set<int> targets;
    for (int i = 0; i < kStyleCount; ++i) {
        const StyleDna dna = buildStyleDna(kStyleIds[i], 0, traitsAt(i));
        targets.insert(static_cast<int>(dna.routes[static_cast<size_t>(dna.count - 3)].target));
    }
    check(targets.size() >= 3, "the transient layer's target is the style's own, not one for every style");
}

// The owner: react to transients "but also to longer things like 8 beats or 16, or melody lines". Every style's DNA
// ends on one route on the song's long time (every 4 bars or every phrase) and one on its tune, ordinary routes the
// user sees and edits. Neither moves a colour or turns anything, and the groove before them keeps its room.
void everyStyleHearsTheSongsTime() {
    const auto longTime = [](ReactSource s) {
        return s == ReactSource::Bar4Hold || s == ReactSource::PhraseHold || s == ReactSource::Bar4Pulse || s == ReactSource::Phrase;
    };
    const auto tune = [](ReactSource s) {
        return s == ReactSource::MelodyContour || s == ReactSource::PitchHeight || s == ReactSource::MelodyNote ||
               s == ReactSource::MelodyPresence;
    };
    const auto calm = [](LfoTarget t) {
        return !isLuminanceTarget(t) && t != LfoTarget::Rotation && t != LfoTarget::CamRoll && t != LfoTarget::CamOrbit &&
               t != LfoTarget::Twist && t != LfoTarget::Sway;
    };
    std::set<int> longSources;
    std::set<int> longTargets;
    std::set<int> tuneSources;
    std::set<int> tuneTargets;
    for (const std::string& id : catalogue::allIds()) {
        for (int seed = 0; seed < 4; ++seed) {
            for (const StyleTraits t : {StyleTraits{0.0f, false}, StyleTraits{0.0f, true}, StyleTraits{1.0f, true}, StyleTraits{-0.7f, false}}) {
                const StyleDna dna = buildStyleDna(id, seed, t);
                check(dna.count >= 3 && dna.count <= StyleDna::kMaxRoutes, "the DNA stays within its cap");
                const ReactRoute& bars = dna.routes[static_cast<size_t>(dna.count - 2)];
                const ReactRoute& line = dna.routes[static_cast<size_t>(dna.count - 1)];
                check(bars.enabled && longTime(bars.source), "every style has a route on every 4 bars or every phrase");
                check(line.enabled && tune(line.source), "every style has a route on the melody");
                check(calm(bars.target) && calm(line.target), "the long and tune routes move no colour and turn nothing");
                check(bars.attackSeconds >= 1.0f && bars.releaseSeconds >= 1.0f, "the long route glides over a second or more");
                check(dna.count - 3 <= StyleDna::kGrooveRoutes, "the groove keeps the room it had");
                longSources.insert(static_cast<int>(bars.source));
                longTargets.insert(static_cast<int>(bars.target));
                tuneSources.insert(static_cast<int>(line.source));
                tuneTargets.insert(static_cast<int>(line.target));
            }
        }
    }
    check(longSources.size() >= 3 && longTargets.size() >= 2, "the long route's source and target are drawn per style");
    check(tuneSources.size() >= 3 && tuneTargets.size() >= 3, "the tune route's source and target are drawn per style");
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
        hues.insert(static_cast<int>(std::lround(m.state().hue * 100.0f)));
    }
    check(hues.size() >= 3, "styles follow different hue drivers");
}

// The bar wave rides the music's own bar while the beat is sure, the style's clock while it is not, and eases
// between them without a jump. The clocks stand still here, so the clock's wave reads 0.
void motionFieldBarWaveFollowsTheHeardBar() {
    MotionField m;
    GeodeSoundFrame f = playing();
    f.beatConf = 0.9f;
    f.barPhase = 0.25f;  // a quarter into the bar: the wave's crest
    for (int k = 0; k < 300; ++k) m.step(f, 1.0f / 60.0f);
    check(m.state().barOsc > 0.95f, "a sure beat puts the bar wave on the heard bar");
    float last = m.state().barOsc;
    float jump = 0.0f;
    f.beatConf = 0.0f;
    for (int k = 0; k < 300; ++k) {
        m.step(f, 1.0f / 60.0f);
        jump = std::max(jump, std::fabs(m.state().barOsc - last));
        last = m.state().barOsc;
    }
    check(std::fabs(m.state().barOsc - 0.5f) < 0.02f, "with no beat heard the wave is the clock's again, not a stand-in bar");
    check(jump < 0.05f, "losing the beat eases the wave over, never jumps it");
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

// The Dive category's settings the tunnel checks run over: the plain tunnel and each control at its ends,
// on a square, a phone and a wide frame.
std::vector<endless::tunnel::Look> diveLooks() {
    std::vector<endless::tunnel::Look> out;
    const auto with = [&out](float aspect, auto edit) {
        SceneParams p;
        edit(p);
        out.push_back(endless::tunnel::lookFor(p, aspect));
    };
    for (float aspect : {1.0f, 0.46f, 2.2f}) {
        with(aspect, [](SceneParams&) {});
        with(aspect, [](SceneParams& p) { p.diveSpacing = 0.5f; });
        with(aspect, [](SceneParams& p) { p.diveSpacing = 2.0f; p.diveBlend = 1.0f; });
        with(aspect, [](SceneParams& p) { p.diveTwist = 1.0f; });
        with(aspect, [](SceneParams& p) { p.diveTwist = -0.37f; p.diveAimX = 0.6f; p.diveAimY = -0.6f; });
        with(aspect, [](SceneParams& p) { p.diveAimX = -0.6f; p.diveAimY = 0.45f; p.diveShape = 0.0f; });
        with(aspect, [](SceneParams& p) { p.diveShape = 1.0f; p.diveBlend = 0.0f; });
        with(aspect, [](SceneParams& p) { p.diveBlend = 1.0f; p.diveShape = 0.0f; p.diveLens = 1.5f; });
    }
    return out;
}

// Endless zoom is the picture nested inside itself. Every copy only ever grows, the frame is always
// covered, and the picture just before the phase wraps is the picture just after it, at every setting.
void endlessZoomIsOneSeamlessTunnel() {
    namespace t = endless::tunnel;
    for (const t::Look& l : diveLooks()) {
        for (int yi = 0; yi <= 12; ++yi) {
            for (int xi = 0; xi <= 12; ++xi) {
                const float x = static_cast<float>(xi) / 6.0f - 1.0f;
                const float y = static_cast<float>(yi) / 6.0f - 1.0f;
                check(t::share(l, 0.0f, l.inner, x, y) == 0.0f, "the newest copy starts shut");
                check(t::cover(l, 0.999999f, -l.base + 1, x, y) > 1.0f - 1e-5f, "the copy before the last covers the frame at the wrap");
                for (int k = l.inner; k > -l.base; --k) {
                    check(std::fabs(t::share(l, 0.999999f, k, x, y) - t::share(l, 0.0f, k - 1, x, y)) < 3e-3f, "no pop at the phase wrap");
                }
            }
        }
        for (int k = l.inner; k >= -l.base; --k) {
            check(t::scale(l, 0.6f, k) > t::scale(l, 0.4f, k), "diving in, every copy grows");
            if (k > -l.base) {
                check(std::fabs(t::scale(l, 0.3f, k) * std::exp2(l.step) - t::scale(l, 0.3f, k - 1)) < 1e-4f * t::scale(l, 0.3f, k - 1),
                      "the copies sit Ring spacing apart");
            }
        }
        check(t::scale(l, 0.0f, 0) == 1.0f, "phase 0 is the plain picture");
        check(t::scale(l, 0.0f, l.inner) <= std::exp2(-t::kInnerOctaves) + 1e-6f, "the newest copy opens five octaves in");
    }
    // What a point shows changes smoothly as the dive runs: a copy's dissolve crossing it, never a cut.
    const t::Look plain = t::lookFor(SceneParams{}, 1.0f);
    const float pts[][2] = {{0.0f, 0.0f}, {0.03f, 0.01f}, {0.2f, -0.1f}, {0.5f, 0.5f}, {-0.7f, 0.3f}, {0.95f, 0.0f}, {1.0f, 1.0f}};
    for (const auto& pt : pts) {
        float worst = 0.0f;
        for (int i = 1; i <= 2000; ++i) {
            const float a = static_cast<float>(i - 1) / 2000.0f * 0.99999f;
            const float b = static_cast<float>(i) / 2000.0f * 0.99999f;
            for (int k = plain.inner; k >= -plain.base; --k) {
                worst = std::max(worst, std::fabs(t::share(plain, b, k, pt[0], pt[1]) - t::share(plain, a, k, pt[0], pt[1])));
            }
        }
        check(worst < 0.01f, "no copy's share of a pixel jumps");
    }
}

// The ridges the first tunnel drew (a shadow outside each copy's edge, a feather over a mirrored
// border): a flat picture now comes out exactly flat at every phase and setting, a copy draws nothing
// outside its own edge, and by default it dissolves across a third of its radius, not at a line.
void endlessZoomDrawsNoRidges() {
    namespace t = endless::tunnel;
    for (const t::Look& l : diveLooks()) {
        for (int i = 0; i <= 96; ++i) {
            const float ph = static_cast<float>(i) / 96.0f * 0.99999f;
            for (int yi = 0; yi <= 16; ++yi) {
                for (int xi = 0; xi <= 16; ++xi) {
                    const float x = static_cast<float>(xi) / 8.0f - 1.0f;
                    const float y = static_cast<float>(yi) / 8.0f - 1.0f;
                    float total = 0.0f;
                    for (int k = l.inner; k >= -l.base; --k) {
                        const float part = t::share(l, ph, k, x, y);
                        check(part >= 0.0f && part <= 1.0f, "a copy's share is a share");
                        total += part;
                        float qx = 0.0f;
                        float qy = 0.0f;
                        t::into(l, ph, k, x, y, qx, qy);
                        if (k > -l.base && t::rim(l, qx, qy) >= 1.0f) check(part == 0.0f, "a copy draws nothing outside its own edge");
                    }
                    check(std::fabs(total - 1.0f) < 1e-4f, "a flat picture stays flat: no shadow, ring or gap");
                    check(t::light(l, x, y) == 1.0f, "nothing darkens the frame at Depth fade 0");
                }
            }
        }
    }
    const t::Look plain = t::lookFor(SceneParams{}, 1.0f);
    check(plain.blend * (1.0f - std::exp2(-plain.step)) >= 0.33f, "by default a copy dissolves across a third of its radius");
    // One percent further from the vanishing point never changes a copy's share by more than a few percent.
    for (int i = 0; i <= 96; ++i) {
        const float ph = static_cast<float>(i) / 96.0f * 0.99999f;
        for (float r = 0.002f; r < 0.99f; r *= 1.01f) {
            for (int k = plain.inner; k >= -plain.base; --k) {
                check(std::fabs(t::share(plain, ph, k, r * 1.01f, 0.0f) - t::share(plain, ph, k, r, 0.0f)) < 0.06f,
                      "no line across the default tunnel");
            }
        }
    }
    // A shrunk copy's filter widens without a step.
    check(t::footprint(1.0f) == 0.0f && t::footprint(2.0f) == 0.0f, "a full-size copy is one tap");
    for (float s = 1.0f; s > 0.01f; s *= 0.99f) {
        check(t::footprint(s * 0.99f) >= t::footprint(s) && t::footprint(s * 0.99f) - t::footprint(s) < 0.02f * (1.0f + t::footprint(s)),
              "the footprint grows smoothly");
    }
    check(t::footprint(0.001f) == 12.0f, "and is capped");
    // Depth fade: dark at the vanishing point, full light at the frame, never a step.
    SceneParams foggy;
    foggy.diveFog = 1.0f;
    foggy.diveAimX = 0.3f;
    const t::Look fog = t::lookFor(foggy, 1.0f);
    check(t::light(fog, 1.0f, 0.0f) == 1.0f && t::light(fog, -1.0f, 0.0f) == 1.0f, "the frame's edge keeps full light");
    float last = 0.0f;
    for (float x = 0.3f; x <= 1.0f; x += 0.01f) {
        const float v = t::light(fog, x, 0.0f);
        check(v >= last - 1e-6f && v - last < 0.1f, "Depth fade rises smoothly from the vanishing point");
        last = v;
    }
}

// The Dive controls as the stage reads them: clamped to the schema, finite whatever comes in.
void diveControlsAreClamped() {
    namespace t = endless::tunnel;
    const t::Look plain = t::lookFor(SceneParams{}, 1.0f);
    check(plain.step == 1.0f && plain.inner == 5 && plain.twist == 0.0f && plain.cx == 0.0f && plain.cy == 0.0f && plain.shape == 4.0f &&
              plain.fog == 0.0f && plain.lens == 0.0f,
          "the defaults are the plain tunnel");
    SceneParams wild;
    wild.diveSpacing = 99.0f;
    wild.diveTwist = -99.0f;
    wild.diveAimX = 99.0f;
    wild.diveAimY = -99.0f;
    wild.diveShape = 99.0f;
    wild.diveBlend = 99.0f;
    wild.diveFog = 99.0f;
    wild.diveLens = 99.0f;
    const t::Look w = t::lookFor(wild, 1.0f);
    check(w.step == 2.0f && w.twist == -t::kQuarterTurn && w.cx == t::kMaxAim && w.cy == -t::kMaxAim && w.shape == 8.0f && w.blend == 1.0f &&
              w.fog == 1.5f && w.lens == 1.5f,
          "each control stops at its range");
    SceneParams bad;
    bad.diveSpacing = bad.diveTwist = bad.diveAimX = bad.diveAimY = bad.diveShape = bad.diveBlend = bad.diveFog = bad.diveLens = NAN;
    const t::Look n = t::lookFor(bad, NAN);
    check(n.step == 1.0f && n.twist == 0.0f && n.cx == 0.0f && n.shape == 4.0f && n.blend == 0.75f && n.fog == 0.0f && n.lens == 0.0f &&
              n.aspect == 1.0f && n.base >= 1,
          "non-finite input is the plain tunnel");
    check(SceneParams::spec(ParamSlot::diveAimX).max == t::kMaxAim && SceneParams::spec(ParamSlot::diveAimY).min == -t::kMaxAim,
          "the aim's reach is the schema's");
    // Ring spacing changes how often a copy passes, never how fast the picture grows.
    check(endless::copiesPerSecond(0.6f, 2.0f) == 0.3f && endless::copiesPerSecond(0.6f, 0.5f) == 1.2f, "octaves a second to copies a second");
    check(endless::copiesPerSecond(0.3f, NAN) == 0.3f && endless::copiesPerSecond(0.3f, 0.0f) == 0.6f, "spacing is clamped");
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

    // Music follow: 0 lets go, 2 doubles the reach, and it is still the slow hand.
    Follow none;
    Follow twice;
    for (int i = 0; i < 3600; ++i) {
        none.step(songAt(i * dt, 0.47, 1.0f), dt, 1.0f, 0.0f);
        twice.step(songAt(i * dt, 0.47, 1.0f), dt, 1.0f, 2.0f);
    }
    check(none.rate() == 1.0f, "Music follow 0 leaves the dive at Dive speed");
    check(twice.rate() > fatRate + 0.1f && twice.rate() <= 1.0f + 2.0f * Follow::kReach + 1e-6f, "Music follow 2 doubles the reach");

    Follow odd;
    GeodeSoundFrame bad = songAt(0.0, 0.47, NAN);
    bad.beatPhase = NAN;
    bad.beatConf = INFINITY;
    for (int i = 0; i < 600; ++i) odd.step(bad, NAN, NAN, NAN);
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

// A style whose program never builds (Flame, Opal and Hollows on the owner's phone, 2026-11-12) must
// neither freeze the frame nor be rebuilt every frame: a stall gives up, a failure is remembered.
void scenePrepGivesUpAndRemembersFailures() {
    ScenePrep prep;
    prep.retarget("flame_veil");
    check(prep.next(false, 100.0, 3) == ScenePrep::Step::Wait, "a busy stager waits");
    check(prep.next(false, 100.0 + ScenePrep::kStallSeconds * 0.9, 3) == ScenePrep::Step::Wait,
          "under the stall time it still waits");
    check(prep.next(false, 100.0 + ScenePrep::kStallSeconds * 0.95, 4) == ScenePrep::Step::Wait,
          "finished work restarts the stall clock");
    check(prep.next(false, 100.0 + ScenePrep::kStallSeconds * 1.5, 4) == ScenePrep::Step::Wait,
          "the clock counts from the last finished work");
    check(prep.next(false, 100.0 + ScenePrep::kStallSeconds * 2.0, 4) == ScenePrep::Step::GiveUp,
          "no work finished for the stall time: give up instead of waiting for ever");
    check(prep.attempts() == 0, "giving up is not an attempt");

    check(!prep.failed("flame_veil"), "nothing has failed until the renderer says so");
    prep.fail("flame_veil");
    prep.fail("flame_veil");
    prep.done();
    check(prep.failed("flame_veil") && !prep.active(), "a failed target is remembered past done()");
    check(!prep.failed("glass_orb"), "only the failed target is remembered");
    prep.retarget("glass_orb");
    check(prep.next(false, 500.0, 0) == ScenePrep::Step::Wait, "a new target starts its own stall clock");
    check(prep.next(true, 500.0 + ScenePrep::kStallSeconds * 3.0, 0) == ScenePrep::Step::Attempt,
          "an idle stager attempts however long ago the last wait was");
    prep.forgive();
    check(!prep.failed("flame_veil"), "forgive() lets a failed target build again");
}

// A style that did not build says which, at what stage and the driver's first lines, and says it
// again when it is picked again.
void buildReportNamesStyleStageAndLog() {
    namespace report = build_report;
    check(report::stageOf("missing shader asset 'opal_drop_frag.glsl'") == report::Stage::Asset, "a missing file is the asset stage");
    check(report::stageOf("unknown shader include 'lib_hollow'") == report::Stage::Asset, "a refused include is the asset stage");
    check(report::stageOf("Link failed: too many uniforms") == report::Stage::Link, "a link log is the link stage");
    check(report::stageOf("ERROR: 0:95: 'gFlamePal' : syntax error") == report::Stage::Compile, "anything else is the compile stage");

    const std::string compile = report::of("flame_veil", {"", "ERROR: 0:95: 'x' : syntax error\r\n\nERROR: 2\nERROR: 3\nERROR: 4\nERROR: 5\n", ""});
    check(compile == "flame_veil did not load (compile)\nERROR: 0:95: 'x' : syntax error\nERROR: 2\nERROR: 3\nERROR: 4",
          "the style, the stage and the log's first lines, blank ones skipped");
    const std::string longLine = report::text("opal_drop", report::Stage::Link, "Link failed: " + std::string(900, 'z'));
    check(longLine.size() == std::string("opal_drop did not load (link)\n").size() + report::kMaxLineChars, "a long line is cut");
    check(report::of("hollow_roots", {}) == "hollow_roots did not load (compile)\nthe driver gave no message", "a silent failure still names the style");
    check(report::of("hollow_roots", {"missing shader asset 'hollow_roots_frag.glsl'"}) ==
              "hollow_roots did not load (asset)\nmissing shader asset 'hollow_roots_frag.glsl'",
          "a missing file is named");
    check(report::text("flame_orb", report::Stage::Stall, "") == "flame_orb did not load (stall)\nits shaders did not finish compiling",
          "a stall says so");

    ScenePrep prep;
    prep.fail("flame_veil", compile);
    prep.fail("flame_veil", "another");
    check(prep.report("flame_veil") == compile, "the first report of a failed style is kept");
    check(prep.report("glass_orb").empty() && !prep.reported(""), "a style that did not fail has none");
    check(prep.reported(compile) && !prep.reported("Silk unavailable on this GPU"), "only a failed style's report is one");
    prep.forgive();
    check(!prep.reported(compile), "forgive() forgets the reports");
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
            case ScenePrep::Step::GiveUp:
                check(false, "a stager that compiles a program each frame never gives up");
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
        for (int i = 0; i < 60 * 120; ++i) {  // 120 s at 93 BPM = 186 beats
            beats += dt * 93.0 / 60.0;
            const float v = e.tick(static_cast<float>(dt), clockAt(beats, 93.0f))[0];
            if (i > 0 && v < prev - 1.0f) ++wraps;
            prev = v;
            const double pos = std::fmod(beats, static_cast<double>(n)) / n;  // saw = 2 pos - 1
            if (i > 600) check(std::fabs(v - (2.0 * pos - 1.0)) < 0.02 || pos < 0.02 || pos > 0.98, "synced saw sits at the music clock's phase");
        }
        check(std::abs(wraps - static_cast<int>(186.0 / n)) <= 1,
              "a synced wave completes one cycle per n beats");
    }
}

void beatTimedLfoIsAtZeroOnMultiples() {
    LfoEngine e;
    e.configs[0] = {true, ModSource::Lfo, LfoTarget::Sway, LfoWave::Sine, 2.0f, 1.0f, ModPolarity::Bipolar, ModCurve::Linear, 4.0f};
    for (const double b : {0.0, 4.0, 8.0, 40.0}) {
        e = LfoEngine{};
        e.configs[0] = {true, ModSource::Lfo, LfoTarget::Sway, LfoWave::Sine, 2.0f, 1.0f, ModPolarity::Bipolar, ModCurve::Linear, 4.0f};
        const float v = e.tick(1.0f / 60.0f, clockAt(b, 77.0f))[0];
        check(std::fabs(v) < 1e-3f, "a 4-beat sine is at phase 0 on beat multiples of 4");
        const float q = e.tick(1.0f / 60.0f, clockAt(b + 1.0, 77.0f))[0];
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

// No tempo heard yet: a beat-timed slot assumes none. It free-runs on its own rate in seconds
// (3 s here: four turns in 12 s), where a stand-in tempo would turn it at some other speed.
void beatTimedLfoWithNoTempoRunsOnItsOwnSeconds() {
    LfoEngine e;
    e.configs[0] = {true, ModSource::Lfo, LfoTarget::Sway, LfoWave::Saw, 3.0f, 1.0f, ModPolarity::Bipolar, ModCurve::Linear, 4.0f};
    const GeodeSoundFrame quiet = silence();
    check(quiet.bpm == 0.0f, "silence carries no tempo");
    float prev = e.tick(1.0f / 60.0f, quiet)[0];
    int wraps = 0;
    for (int i = 0; i < 60 * 12 - 1; ++i) {
        const float v = e.tick(1.0f / 60.0f, quiet)[0];
        if (v < prev - 1.0f) wraps++;
        prev = v;
    }
    check(wraps == 3 || wraps == 4, "a beat-timed wave with no tempo heard turns once per its own rate in seconds");
    check(std::fabs(prev - 1.0f) < 0.05f || std::fabs(prev + 1.0f) < 0.05f, "... and is at the end of its fourth turn after 12 s");
}

void randomLfoStepsOnTheBeatAndGlideIsContinuous() {
    LfoEngine a, g;
    a.configs[0] = {true, ModSource::Lfo, LfoTarget::Sway, LfoWave::Random, 2.0f, 1.0f, ModPolarity::Bipolar, ModCurve::Linear, 4.0f};
    g.configs[0] = {true, ModSource::Lfo, LfoTarget::Sway, LfoWave::RandomGlide, 2.0f, 1.0f, ModPolarity::Bipolar, ModCurve::Linear, 4.0f};
    const double beatsPerSecond = 174.0 / 60.0;
    GeodeSoundFrame f0 = clockAt(0.0, 174.0f);
    a.tick(1.0f / 60.0f, f0);
    g.tick(1.0f / 60.0f, f0);
    const double dt = 1.0 / 60.0;
    double beats = 0.0;
    float la = a.tick(0.0f, f0)[0], lg = g.tick(0.0f, f0)[0];
    int steps = 0;
    std::set<int> targets;
    float worstGlide = 0.0f;
    for (int i = 0; i < 60 * 32; ++i) {  // 92.8 beats = 23.2 cycles
        beats += dt * beatsPerSecond;
        const GeodeSoundFrame f = clockAt(beats, 174.0f);
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
    check(steps >= 22 && steps <= 24, "sample & hold steps once per cycle");
    check(worstGlide < 0.1f, "random glide never jumps");
    check(targets.size() >= 12, "random glide picks a new target each cycle");
    LfoEngine g2;
    g2.configs[0] = g.configs[0];
    g2.tick(1.0f / 60.0f, f0);
    float again = 0.0f;
    for (int i = 0; i < 100; ++i) again = g2.tick(static_cast<float>(dt), clockAt(2.0 + i * dt * beatsPerSecond, 174.0f))[0];
    check(std::isfinite(again), "random glide is deterministic and finite");
}

// Bass, Mid and Treble gain reach every band read a style makes: level, att, shaped, presence and flux.
void bandTrimsReachEveryBandRead() {
    GeodeSoundFrame f{};
    f.bass = f.mid = f.treble = 1.2f;
    f.bassAtt = f.midAtt = f.trebleAtt = 0.9f;
    f.low = f.middle = f.high = 0.6f;
    f.bassPresence = f.midPresence = f.treblePresence = 0.4f;
    f.fluxLow = f.fluxMid = f.fluxHigh = 1.5f;
    f.kickEnv = 0.7f;
    f.bassTime = 33.0f;
    SceneParams p;
    const GeodeSoundFrame same = bandTrimmed(f, p);
    check(same.midPresence == f.midPresence && same.fluxHigh == f.fluxHigh, "band trims at 1 leave the frame alone");
    p.bassGain = 2.0f;
    p.midGain = 0.5f;
    p.trebGain = 0.0f;
    const GeodeSoundFrame t = bandTrimmed(f, p);
    check(t.bass == 2.4f && t.bassAtt == 1.8f && t.low == 1.0f && t.bassPresence == 0.8f && t.fluxLow == 3.0f, "Bass gain trims every bass read");
    check(t.mid == 0.6f && t.midAtt == 0.45f && t.middle == 0.3f && t.midPresence == 0.2f && t.fluxMid == 0.75f, "Mid gain trims every mid read");
    check(t.treble == 0.0f && t.high == 0.0f && t.treblePresence == 0.0f && t.fluxHigh == 0.0f, "Treble gain trims every treble read");
    check(t.kickEnv == f.kickEnv && t.bassTime == f.bassTime, "the trims leave drums and clocks alone");
}

int main() {
    bandTrimsReachEveryBandRead();
    foldIsBoundedAndContinuous();
    followRouteTracksSource();
    circularHelpersTakeTheShortWay();
    hueRouteWrapsTheShortWay();
    phaseRouteStaysMonotonic();
    stepRouteJumpsOncePerEdge();
    luminanceRoutesAreSlewLimited();
    triggersHoldAndSilenceIsQuiet();
    sourcesReadTheirFieldsInRange();
    barCountsFollowTheMusicsBars();
    melodyLineAndFeelingRead();
    settledJumpWaitsForItsGlide();
    tensionRiseFollowsTheChords();
    deterministicForExport();
    jumpHoldsANewSeededValuePerHit();
    toggleFlipsBetweenZeroAndAmount();
    dnaIsDeterministicAndSeeded();
    stylesGetDifferentPersonalities();
    zoomPersonalitiesZoomOnTheBeat();
    kickAndSnareSidesAnswerDifferently();
    dnaSourcesVaryAcrossStyles();
    dnaRespectsStyleTraits();
    dnaLeavesRoomAndStaysInRange();
    everyStyleHearsTransients();
    everyStyleHearsTheSongsTime();
    cameraTravelsAndStaysOrthonormal();
    cameraSpringsInsteadOfSnapping();
    cameraPathRepeatsAcrossTheWrap();
    backwardTravelUnwindsAndTheTurnIsContinuous();
    strafeLooksInAndCranesRoundOnReverse();
    everyPathCrossesTheTravelWrapSmoothly();
    corkscrewRollsAndReducedMotionLeans();
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
    motionFieldBarWaveFollowsTheHeardBar();
    flowTimeIsFlowPhaseAtSpeedOne();
    flowTimeRunsAtSpeedAndNeverJumps();
    followerScalesASoundClockWithoutJumps();
    motionFieldDriftSpinIsBoundedAndContinuous();
    motionFieldReducedMotionScalesRatesOnce();
    motionFieldDriftWrapNeverPopsAStyle();
    sourceCatalogueIsCompleteAndUnique();
    endlessZoomIsOneSeamlessTunnel();
    endlessZoomDrawsNoRidges();
    diveControlsAreClamped();
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
    scenePrepGivesUpAndRemembersFailures();
    buildReportNamesStyleStageAndLog();
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
    beatTimedLfoWithNoTempoRunsOnItsOwnSeconds();
    randomLfoStepsOnTheBeatAndGlideIsContinuous();
    if (failures == 0) std::printf("viz_math_test: all passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
