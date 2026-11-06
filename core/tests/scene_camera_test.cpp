// Host tests for core/scene/camera (#512): the free 6-DoF scene camera, its layered drivers and the
// comfort dial. Built and run by CI ("Native host tests" in .github/workflows/android.yml) with the
// system compiler; no GL, no device. Every check drives SceneCamera through its public interface:
// params, a frame input and dt go in, a pose comes out.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "scene/camera/SceneCamera.hpp"
#include "viz/CameraRig.hpp"

using namespace geode::scene::camera;
using geode::signal::ParamInput;
using geode::viz::CameraPose;
using geode::viz::CameraRig;
using geode::viz::SceneParams;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void set(SceneCamera& c, CameraParam id, float v) { c.params().input(id, ParamInput::set(v)); }

// A camera that stands still in the world: hover path, no travel, no orbit, no shake. Its base
// orientation is the identity, so every turn the test sees is the camera's own.
SceneParams still() {
    SceneParams p;
    p.camPath = 3;
    p.camFly = 0.0f;
    p.camOrbit = 0.0f;
    p.camShake = 0.0f;
    return p;
}

FrameInput frame(const SceneParams& p, float dt) {
    FrameInput in;
    in.params = &p;
    in.dt = dt;
    return in;
}

float upness(const Pose& p) { return dot(upOf(p.orient), {0.0f, 1.0f, 0.0f}); }

float rollOf(const Pose& p) {
    float r = 0.0f;
    rollFromLevel(p.orient, r);
    return r;
}

bool same(const std::array<float, 3>& a, const std::array<float, 3>& b) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2]; }

bool samePose(const CameraPose& a, const CameraPose& b) {
    return same(a.pos, b.pos) && same(a.right, b.right) && same(a.up, b.up) && same(a.fwd, b.fwd) && a.focal == b.focal &&
           a.travel == b.travel && a.dolly == b.dolly && a.yaw == b.yaw && a.pitch == b.pitch && a.roll == b.roll &&
           a.flat == b.flat && a.tilt == b.tilt;
}

// The highest turn speed seen over `seconds` at `fps`, in rad/s.
float maxTurnSpeed(SceneCamera& c, const SceneParams& p, float seconds, float fps, bool reducedMotion = false) {
    const float dt = 1.0f / fps;
    FrameInput in = frame(p, dt);
    in.reducedMotion = reducedMotion;
    c.step(in);
    Quat last = c.framePose().orient;
    float top = 0.0f;
    for (int i = 0; i < static_cast<int>(seconds * fps); ++i) {
        c.step(in);
        top = std::max(top, angleBetween(last, c.framePose().orient) / dt);
        last = c.framePose().orient;
    }
    return top;
}

// ---- Rails: the default reproduces today's flight ---------------------------------------------------

void railsIsTodaysRig() {
    SceneParams p;  // the defaults: glide path, flying
    p.camTouch = true;
    p.camShake = 0.4f;
    p.camRoll = 0.3f;
    SceneCamera cam;
    CameraRig rig;
    check(cam.mode() == Mode::Rails, "every style starts on rails");
    GeodeSoundFrame sound{};
    sound.presence = 1.0f;
    sound.intensity = 0.8f;
    bool identical = true;
    for (int i = 0; i < 1800; ++i) {
        const float dt = (i % 3 == 0) ? 1.0f / 30.0f : 1.0f / 90.0f;
        sound.midTime = 0.02f * static_cast<float>(i);
        sound.kick = (i % 20 == 0) ? 1.0f : 0.0f;
        sound.hardCut = (i == 900) ? 1.0f : 0.0f;
        CameraRig::LookInput look{0.6f * std::sin(0.01f * static_cast<float>(i)), -0.4f};
        rig.step(p, look, dt, false, true, &sound);
        FrameInput in = frame(p, dt);
        in.look = look;
        in.lookActive = true;
        in.sound = &sound;
        cam.step(in);
        identical = identical && samePose(cam.pose(), rig.pose());
    }
    check(identical, "Rails with no keyframes draws the rig's pose bit for bit");
    check(rig.cuts() == 1 && cam.cuts() == 1, "the rig's own cut passes through the comfort stage");

    // Reduced motion on rails is still today's reduced-motion flight.
    SceneCamera calm;
    CameraRig calmRig;
    bool same2 = true;
    for (int i = 0; i < 300; ++i) {
        calmRig.step(p, {}, 1.0f / 60.0f, true, true, nullptr);
        FrameInput in = frame(p, 1.0f / 60.0f);
        in.reducedMotion = true;
        calm.step(in);
        same2 = same2 && samePose(calm.pose(), calmRig.pose());
    }
    check(same2, "reduced motion on rails keeps today's gentle flight");
}

void railsClampsWhatIsLaidOver() {
    // A keyframe asking for a half turn, a pitch past the limit and an upside-down roll.
    PoseKey key;
    key.pose.orient = fromLook({1.5f, 1.2f, kPi});
    key.pose.pos = {40.0f, 3.0f, -7.0f};
    SceneParams p = still();
    SceneCamera cam;
    cam.setKeyframes(&key, 1);
    for (int i = 0; i < 240; ++i) cam.step(frame(p, 1.0f / 60.0f));
    const Quat rigQ = fromBasis(cam.rig().pose().right, cam.rig().pose().up, cam.rig().pose().fwd);
    const Look rel = toLook(mul(conj(rigQ), cam.framePose().orient));
    check(std::fabs(rel.yaw) <= CameraRig::kMaxYaw + 1e-3f, "rails: yaw at most 0.6 from the flight");
    check(std::fabs(rel.pitch) <= CameraRig::kMaxPitch + 1e-3f, "rails: pitch at most 0.45 from the flight");
    check(std::fabs(rel.roll) <= CameraRig::kMaxRoll + 1e-3f, "rails: roll at most 0.8 from the flight");
    check(upness(cam.framePose()) > 0.0f, "rails: never upside down");
    const std::array<float, 3> d = sub(cam.framePose().pos, cam.rig().pose().pos);
    check(length(d) < 1e-5f, "rails: the position is the rig's forward flight");
}

// ---- Comfort ----------------------------------------------------------------------------------------

void comfortCapsTurnSpeed() {
    const SceneParams p = still();
    const float limit[3] = {kPi / 4.0f, kPi / 2.0f, kSafetyMaxTurn};
    const char* names[3] = {"Calm turns at most 45 deg/s", "Normal turns at most 90 deg/s", "Wild turns as fast as the safety clamp allows"};
    for (int level = 0; level < 3; ++level) {
        SceneCamera cam;
        cam.setMode(Mode::SixDof);
        cam.setComfort(static_cast<Comfort>(level));
        set(cam, kYawRate, 2.0f * kPi);  // a full turn a second asked for
        const float top = maxTurnSpeed(cam, p, 4.0f, 60.0f);
        check(top <= limit[level] * 1.002f + 1e-3f, names[level]);
        if (level < 2) check(top >= limit[level] * 0.95f, "the cap is reached, not undershot");
    }
    SceneCamera wild;
    wild.setMode(Mode::SixDof);
    wild.setComfort(Comfort::Wild);
    set(wild, kYawRate, 2.0f * kPi);
    check(maxTurnSpeed(wild, p, 2.0f, 60.0f) > 5.0f, "Wild has no comfort cap: a full turn a second plays");
    // The safety clamp is always on: even Wild cannot turn faster than 720 deg/s.
    SceneCamera blast;
    blast.setMode(Mode::SixDof);
    blast.setComfort(Comfort::Wild);
    set(blast, kTakeoverFollow, 0.02f);
    set(blast, kTakeoverReach, 0.5f * kPi);  // a swing of half a turn each way
    FrameInput in = frame(p, 1.0f / 60.0f);
    blast.step(in);
    Quat last = blast.framePose().orient;
    float top = 0.0f;
    for (int i = 0; i < 240; ++i) {
        in.lookActive = true;
        in.look = {(i / 10) % 2 ? 1.0f : -1.0f, 0.0f};
        blast.step(in);
        top = std::max(top, angleBetween(last, blast.framePose().orient) * 60.0f);
        last = blast.framePose().orient;
    }
    check(top <= kSafetyMaxTurn * 1.002f, "the safety clamp caps turn speed on every level");
}

int flipsOver(Comfort level, float pitchRate, float seconds, bool& everInverted) {
    const SceneParams p = still();
    SceneCamera cam;
    cam.setMode(Mode::SixDof);
    cam.setComfort(level);
    set(cam, kPitchRate, pitchRate);
    int flips = 0;
    everInverted = false;
    float lastUp = 1.0f;
    for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i) {
        cam.step(frame(p, 1.0f / 60.0f));
        const float u = upness(cam.framePose());
        if (lastUp >= -0.02f && u < -0.02f) ++flips;
        everInverted = everInverted || u < -0.02f;
        lastUp = u;
    }
    return flips;
}

void flipBudget() {
    bool inverted = false;
    const int wild = flipsOver(Comfort::Wild, kPi, 60.0f, inverted);
    check(wild >= 20 && inverted, "Wild loops: a held pitch rate flips the camera over and over");
    const int normal = flipsOver(Comfort::Normal, 1.2f, 60.0f, inverted);
    check(normal >= 1 && normal <= 2, "Normal allows at most 2 flips a minute");
    const int normalLong = flipsOver(Comfort::Normal, 1.2f, 130.0f, inverted);
    check(normalLong <= 6 && normalLong >= 3, "Normal's budget refills a minute later");
    const int calm = flipsOver(Comfort::Calm, 0.6f, 60.0f, inverted);
    check(calm == 0 && !inverted, "Calm never flips");
}

// Rolls the camera by `rate` for `seconds`, lets go, and returns its roll `after` seconds later.
float rollAfterRelease(Comfort level, float rate, float seconds, float after, bool hold, float& peak) {
    const SceneParams p = still();
    SceneCamera cam;
    cam.setMode(Mode::SixDof);
    cam.setComfort(level);
    if (hold) set(cam, kHoldRoll, 1.0f);
    set(cam, kRollRate, rate);
    for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i) cam.step(frame(p, 1.0f / 60.0f));
    peak = rollOf(cam.framePose());
    set(cam, kRollRate, 0.0f);
    for (int i = 0; i < static_cast<int>(after * 60.0f); ++i) cam.step(frame(p, 1.0f / 60.0f));
    return rollOf(cam.framePose());
}

void horizonRecovery() {
    float peak = 0.0f;
    const float calm = rollAfterRelease(Comfort::Calm, 0.4f, 1.0f, 1.8f, false, peak);
    check(std::fabs(peak) > 0.35f && std::fabs(calm) < 0.05f, "Calm levels the horizon in about 1.5 s");
    const float normal = rollAfterRelease(Comfort::Normal, 1.0f, 1.0f, 3.3f, false, peak);
    check(std::fabs(peak) > 0.9f && std::fabs(normal) < 0.06f, "Normal levels the horizon in about 3 s");
    const float early = rollAfterRelease(Comfort::Normal, 1.0f, 1.0f, 1.0f, false, peak);
    check(std::fabs(early) > 0.2f, "Normal's recovery is an ease, not a snap");
    const float held = rollAfterRelease(Comfort::Normal, 1.0f, 1.0f, 3.3f, true, peak);
    check(std::fabs(held - peak) < 1e-3f, "a route that holds the roll keeps it");
    const float wildFlip = rollAfterRelease(Comfort::Wild, 3.0f, 1.0f, 3.5f, false, peak);
    check(std::fabs(wildFlip) < 0.2f, "after an upside-down roll the horizon comes back upright");
}

void cutSpacing() {
    // A track that cuts every half second between two far-apart shots.
    std::vector<PoseKey> keys;
    for (int i = 0; i <= 40; ++i) {
        PoseKey k;
        k.time = 0.5 * i;
        k.cut = i > 0;
        k.pose.pos = (i % 2) ? std::array<float, 3>{50.0f, 0.0f, 0.0f} : std::array<float, 3>{0.0f, 0.0f, 0.0f};
        k.pose.orient = fromLook({(i % 2) ? 1.0f : 0.0f, 0.0f, 0.0f});
        keys.push_back(k);
    }
    const SceneParams p = still();
    for (int level = 0; level < 3; ++level) {
        SceneCamera cam;
        cam.setMode(Mode::SixDof);
        cam.setComfort(static_cast<Comfort>(level));
        cam.setKeyframes(keys.data(), keys.size());
        double t = 0.0;
        double lastCutAt = -10.0;
        double minGap = 1e9;
        int cuts = 0;
        for (int i = 0; i < 20 * 60; ++i) {
            t += 1.0 / 60.0;
            cam.setProgrammeTime(t);
            cam.step(frame(p, 1.0f / 60.0f));
            if (cam.cuts() != cuts) {
                cuts = cam.cuts();
                minGap = std::min(minGap, t - lastCutAt);
                lastCutAt = t;
            }
        }
        if (level < 2) {
            check(minGap >= 2.0 - 1e-6 && cuts >= 8 && cuts <= 10, "Calm and Normal cut at most once every 2 s");
        } else {
            check(minGap >= kSafetyCutGap - 1e-6 && cuts >= 30, "Wild cuts as the track says, down to the safety gap");
        }
    }
}

void takeoverGlidesBack() {
    const SceneParams p = still();
    SceneCamera cam;
    cam.setMode(Mode::SixDof);
    cam.setComfort(Comfort::Wild);  // no cap: the glide itself is under test
    FrameInput in = frame(p, 1.0f / 60.0f);
    in.lookActive = true;
    in.look = {1.0f, 0.0f};
    for (int i = 0; i < 120; ++i) cam.step(in);
    const float held = angleBetween(cam.framePose().orient, Quat{});
    check(std::fabs(held - 0.9f) < 0.02f, "a full drag turns the view by the takeover reach");
    in.lookActive = false;
    in.look = {};
    float last = held;
    bool monotone = true;
    float atHalf = 0.0f;
    for (int i = 0; i < 300; ++i) {
        cam.step(in);
        const float a = angleBetween(cam.framePose().orient, Quat{});
        monotone = monotone && a <= last + 1e-5f;
        last = a;
        if (i == 30) atHalf = a;
    }
    check(atHalf > 0.1f && atHalf < held, "release glides, it does not snap");
    check(monotone, "the glide back never overshoots past the flight (critically damped)");
    check(last < 0.01f, "the view is back on its own flight after the glide");
}

void keyframesOverride() {
    PoseKey key;
    key.pose.pos = {3.0f, -2.0f, 11.0f};
    key.pose.orient = fromLook({0.7f, -0.2f, 0.0f});
    key.pose.focal = 2.1f;
    const SceneParams p = still();
    SceneCamera cam;
    cam.setMode(Mode::SixDof);
    cam.setComfort(Comfort::Normal);
    set(cam, kYawRate, 1.0f);  // a route turning the base
    cam.setKeyframes(&key, 1);
    FrameInput in = frame(p, 1.0f / 60.0f);
    in.lookActive = true;  // and a finger on the screen
    in.look = {-1.0f, 0.5f};
    for (int i = 0; i < 180; ++i) cam.step(in);
    check(angleBetween(cam.framePose().orient, key.pose.orient) < 1e-3f, "keyframes override routes and touch (orientation)");
    check(length(sub(cam.framePose().pos, key.pose.pos)) < 1e-4f, "keyframes override routes and touch (position)");
    check(std::fabs(cam.pose().focal - 2.1f) < 1e-5f, "keyframes set the focal length");
    const std::array<float, 3> f = cam.pose().fwd;
    const std::array<float, 3> want = fwdOf(key.pose.orient);
    check(length(sub(f, want)) < 2e-3f, "the scenes' basis is the keyframe's");

    // Two keys: the track eases between them.
    PoseKey two[2];
    two[0].time = 0.0;
    two[1].time = 2.0;
    two[1].pose.pos = {10.0f, 0.0f, 0.0f};
    SceneCamera eased;
    eased.setMode(Mode::SixDof);
    eased.setKeyframes(two, 2);
    eased.setProgrammeTime(1.0);
    eased.step(frame(p, 1.0f / 60.0f));
    check(std::fabs(eased.framePose().pos[0] - 5.0f) < 1e-4f, "halfway between keys is halfway along");
}

void reducedMotionForcesCalm() {
    const SceneParams p = still();
    SceneCamera cam;
    cam.setMode(Mode::SixDof);
    cam.setComfort(Comfort::Wild);
    set(cam, kYawRate, 2.0f * kPi);
    const float top = maxTurnSpeed(cam, p, 3.0f, 60.0f, true);
    check(cam.comfort() == Comfort::Calm, "reduced motion forces Calm");
    check(top <= kPi / 4.0f * 1.002f + 1e-3f, "under reduced motion even Wild turns at most 45 deg/s");
    check(SceneCamera::effectiveComfort(Comfort::Normal, false) == Comfort::Normal, "without it the chosen level stands");
}

void speedsAreIntegrated() {
    const SceneParams p = still();
    SceneCamera cam;
    cam.setMode(Mode::SixDof);
    cam.setComfort(Comfort::Wild);
    set(cam, kYawRate, 1.0f);
    for (int i = 0; i < 60; ++i) cam.step(frame(p, 1.0f / 60.0f));
    const Quat before = cam.framePose().orient;
    set(cam, kYawRate, 0.0f);  // stopping the turn leaves the view where it is
    cam.step(frame(p, 1.0f / 60.0f));
    check(angleBetween(before, cam.framePose().orient) < 1e-5f, "a speed change never jumps the picture");
    check(std::fabs(angleBetween(before, Quat{}) - 1.0f) < 0.02f, "a second at 1 rad/s turned one radian");
}

// The same inputs stepped at 30, 60 and 120 fps reach the same pose.
Pose run(float fps, Mode mode) {
    SceneParams p;  // flying the glide path
    p.camFly = 1.0f;
    SceneCamera cam;
    cam.setMode(mode);
    cam.setComfort(Comfort::Normal);
    const int n = static_cast<int>(std::lround(6.0f * fps));
    const float dt = 1.0f / fps;
    for (int i = 0; i < n; ++i) {
        const int sec = static_cast<int>(static_cast<float>(i) / fps + 1e-4f);
        set(cam, kYawRate, sec < 3 ? 0.5f : 0.0f);
        set(cam, kPitchRate, sec < 2 ? 0.3f : 0.0f);
        FrameInput in = frame(p, dt);
        in.lookActive = sec >= 1 && sec < 3;
        in.look = {0.5f, -0.3f};
        cam.step(in);
    }
    return cam.framePose();
}

void frameRateIndependent() {
    for (Mode mode : {Mode::Rails, Mode::SixDof}) {
        const Pose a = run(30.0f, mode);
        const Pose b = run(60.0f, mode);
        const Pose c = run(120.0f, mode);
        const bool turn = angleBetween(a.orient, b.orient) < 0.02f && angleBetween(b.orient, c.orient) < 0.02f;
        const bool place = length(sub(a.pos, b.pos)) < 0.05f && length(sub(b.pos, c.pos)) < 0.05f;
        check(turn && place, mode == Mode::Rails ? "rails: the same pose at 30, 60 and 120 fps" : "6-DoF: the same pose at 30, 60 and 120 fps");
    }
}

void wildFreedom() {
    // A quaternion pose can face straight back and straight up, and stay finite through it all.
    const SceneParams p = still();
    SceneCamera cam;
    cam.setMode(Mode::SixDof);
    cam.setComfort(Comfort::Wild);
    set(cam, kLookYaw, kPi * 0.999f);
    for (int i = 0; i < 120; ++i) cam.step(frame(p, 1.0f / 60.0f));
    check(cam.pose().fwd[2] < -0.99f, "the camera can turn round");
    set(cam, kLookYaw, 0.0f);
    set(cam, kLookPitch, 0.5f * kPi);
    for (int i = 0; i < 120; ++i) cam.step(frame(p, 1.0f / 60.0f));
    check(cam.pose().fwd[1] > 0.99f, "the camera can look straight up");
    const CameraPose& c = cam.pose();
    check(std::isfinite(c.right[0]) && std::isfinite(c.up[1]) && std::isfinite(c.yaw) && std::isfinite(c.flat[3]),
          "looking straight up stays finite");
    check(std::fabs(dot(c.right, c.up)) < 1e-4f && std::fabs(dot(c.up, c.fwd)) < 1e-4f, "the basis stays orthonormal");
}

void surfaceKindPicksTheDefault() {
    check(defaultComfort(Surface::Wallpaper) == Comfort::Calm && defaultComfort(Surface::Dream) == Comfort::Calm,
          "the wallpaper and the dream start on Calm");
    check(defaultComfort(Surface::Live) == Comfort::Normal && defaultComfort(Surface::Export) == Comfort::Normal,
          "live and export start on Normal");
    check(comfortFor(Surface::Dream, -1) == Comfort::Calm && comfortFor(Surface::Live, -1) == Comfort::Normal,
          "with no explicit choice the surface's default plays");
    check(comfortFor(Surface::Dream, 2) == Comfort::Wild && comfortFor(Surface::Live, 0) == Comfort::Calm,
          "an explicit choice wins over the surface's default");
    check(comfortFor(static_cast<Surface>(7), 9) == Comfort::Normal, "an unknown kind and level fall back to Live's Normal");
    check(static_cast<int>(Surface::Live) == 0 && static_cast<int>(Surface::Wallpaper) == 1 && static_cast<int>(Surface::Dream) == 2 &&
              static_cast<int>(Surface::Export) == 3,
          "surface kinds keep frame::Surface's order (the ABI ordinals)");
    check(SceneCamera::effectiveComfort(comfortFor(Surface::Live, 2), true) == Comfort::Calm, "reduced motion still forces Calm");
}

void mathRoundTrips() {
    const Look l{0.4f, -0.3f, 0.2f};
    const Look back = toLook(fromLook(l));
    check(std::fabs(back.yaw - l.yaw) < 1e-5f && std::fabs(back.pitch - l.pitch) < 1e-5f && std::fabs(back.roll - l.roll) < 1e-5f,
          "toLook inverts fromLook");
    const Quat q = qnormalize(fromLook({2.5f, 1.1f, -2.9f}));
    const Quat r = fromBasis(rightOf(q), upOf(q), fwdOf(q));
    check(angleBetween(q, r) < 1e-4f, "fromBasis inverts the basis");
    float roll = 0.0f;
    check(rollFromLevel(fromLook({0.3f, 0.2f, 0.5f}), roll) && std::fabs(roll - 0.5f) < 1e-4f, "rollFromLevel reads the look roll");
    check(cameraSchema().idsUnique() && cameraSchema().size() == kCameraParamCount, "one camera param per row, ids unique");
}

}  // namespace

int main() {
    mathRoundTrips();
    surfaceKindPicksTheDefault();
    railsIsTodaysRig();
    railsClampsWhatIsLaidOver();
    comfortCapsTurnSpeed();
    flipBudget();
    horizonRecovery();
    cutSpacing();
    takeoverGlidesBack();
    keyframesOverride();
    reducedMotionForcesCalm();
    speedsAreIntegrated();
    frameRateIndependent();
    wildFreedom();
    if (failures == 0) std::printf("scene_camera_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
