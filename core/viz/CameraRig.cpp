#include "viz/CameraRig.hpp"

#include <algorithm>
#include <cmath>

#include "viz/VisualSafety.hpp"

namespace geode::viz {

namespace {

constexpr float kTwoPi = 6.2831853f;
constexpr float kW = kTwoPi / CameraRig::kPathPeriod;
constexpr float kLookAhead = 0.75f;

using Vec3 = std::array<float, 3>;

Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 add(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3 scale(const Vec3& a, float k) { return {a[0] * k, a[1] * k, a[2] * k}; }
float dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec3 normalize(const Vec3& a) {
    const float l = std::sqrt(dot(a, a));
    return l > 1e-6f ? scale(a, 1.0f / l) : Vec3{0.0f, 0.0f, 1.0f};
}
// Rodrigues rotation of v about unit axis k.
Vec3 rotate(const Vec3& v, const Vec3& k, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return add(add(scale(v, c), scale(cross(k, v), s)), scale(k, dot(k, v) * (1.0f - c)));
}

// Two incommensurate sines: a cheap, smooth handheld wobble, deterministic in time.
float wobble(float t, float a, float b) { return 0.6f * std::sin(t * a) + 0.4f * std::sin(t * b + 1.7f); }

}  // namespace

void CameraRig::Spring::step(float target, float dt, float seconds) {
    const float omega = 2.0f / std::max(seconds, 1e-3f);
    const float k = omega * dt;
    const float e = std::exp(-k);
    const float d = x - target;
    const float t = (v + omega * d) * dt;
    x = target + (d + t) * e;
    v = (v - omega * t) * e;
}

void CameraRig::reset() {
    yaw_ = pitch_ = roll_ = dolly_ = fov_ = speed_ = orbit_ = Spring{};
    travel_ = 0.0f;
    time_ = 0.0f;
    primed_ = false;
    pose_ = CameraPose{};
    trauma_ = 0.0f;
    traumaClock_ = 0.0f;
    sinceCut_ = kCutGapSeconds;
    sinceReframe_ = kReframeGapSeconds;
    sinceNoteLook_ = 1.0f;
    noteLook_ = 0.0f;
    dollyKick_ = 0.0f;
    lissPhase_ = 0.0f;
    cuts_ = 0;
    seed_ = 0x2545F491u;
}

// 0..1, deterministic per rig, so an export draws the same cuts as the screen.
float CameraRig::nextRandom() {
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return static_cast<float>(seed_ >> 8) / 16777216.0f;
}

Vec3 CameraRig::pathPoint(int path, float orbit, float s) {
    const float a = 0.4f + 1.6f * std::clamp(orbit, 0.0f, 1.0f);
    switch (path) {
        case 1: {  // spiral: a corkscrew around the travel axis
            const float th = 52.0f * kW * s;
            return {a * std::cos(th), a * std::sin(th), s};
        }
        case 2: {  // orbit: circle the origin, z is not travel
            const float th = 24.0f * kW * s;
            const float r = 2.6f + 1.4f * a;
            return {r * std::sin(th), 0.35f * a * std::sin(13.0f * kW * s), -r * std::cos(th)};
        }
        case 3:  // hover: a slow figure-eight on the spot
            return {0.3f * a * std::sin(17.0f * kW * s), 0.2f * a * std::sin(34.0f * kW * s), 0.0f};
        default:  // glide: a smooth wandering spline down +z
            return {a * (std::sin(34.0f * kW * s) + 0.5f * std::sin(21.0f * kW * s + 1.0f)),
                    0.6f * a * std::sin(28.0f * kW * s + 2.0f), s};
    }
}

void CameraRig::step(const SceneParams& p, const LookInput& look, float dt, bool reducedMotion, bool bankFlat,
                     const GeodeSoundFrame* sound) {
    // Same frame-time rule as viz/Reactions: non-finite is no time at all.
    dt = std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.1f) : 0.0f;
    const float g = reducedMotion ? safety::kReducedMotionScale : 1.0f;
    const float soft = 0.08f + 0.72f * std::clamp(p.camSmooth, 0.0f, 1.0f);
    const float lookYaw = p.camYaw * kMaxYaw + (p.camTouch || p.camGyro ? look.yaw * kTouchLook : 0.0f);
    const float lookPitch = p.camPitch * kMaxPitch + (p.camTouch || p.camGyro ? look.pitch * kTouchLook : 0.0f);
    float fovTarget = std::clamp(p.camFov, 0.5f, 2.0f);
    const float speedTarget = std::clamp(p.camFly, 0.0f, 2.5f) * kBaseSpeed * g;

    // ---- SoundMoves: what the music does to the camera by itself ----------------------------
    // Sources: MilkDrop's zoom on bass_att and its slow rot on time; TouchDesigner's lag-shaped
    // levels; Eiserloh (GDC 2016) for trauma shake. Every offset is eased by the springs below and
    // scaled by amp, so quiet passages move little, silence fades it all out, and the Motion dial
    // and reduced motion turn it down like the rest of the motion layer.
    float soundYaw = 0.0f;
    float soundPitch = 0.0f;
    float soundRoll = 0.0f;
    float soundDolly = 0.0f;
    bool cut = false;
    if (sound != nullptr) {
        const GeodeSoundFrame& s = *sound;
        auto val = [](float v) { return std::isfinite(v) ? v : 0.0f; };
        const float intensity = std::clamp(val(s.intensity), 0.0f, 1.0f);
        const float amp = std::clamp(p.motionAmount, 0.0f, 1.0f) * g * std::clamp(val(s.presence), 0.0f, 1.0f) *
                          (0.3f + 0.7f * std::pow(intensity, 1.5f));
        sinceCut_ += dt;
        sinceReframe_ += dt;
        sinceNoteLook_ += dt;

        // A hard cut: a new place on the path and a new orbit phase, with no tween (the springs are
        // snapped below) and the shake and dolly kick cleared, at most once per kCutGapSeconds.
        const Moves& m = moves_;
        // The Chaos macro shortens the gap between cuts (cut 2 = half) or stops them (cut 0).
        const float cutGap = m.cut > 0.05f ? kCutGapSeconds / m.cut : INFINITY;
        if (val(s.hardCut) > 0.0f && sinceCut_ >= cutGap && amp > 0.0f) {
            cut = true;
            sinceCut_ = 0.0f;
            sinceReframe_ = 0.0f;
            travel_ = std::fmod(travel_ + 40.0f + 160.0f * nextRandom(), kTravelWrap);
            lissPhase_ = std::fmod(lissPhase_ + 1.0f + 4.0f * nextRandom(), kTwoPi * 8.0f);
            trauma_ = 0.0f;
            dollyKick_ = 0.0f;
            ++cuts_;
        }
        // Every sixteenth kick, a new framing: the orbit's phase moves by up to 1.5 rad and the
        // springs ease there. Held off after a cut and spaced, so it reads as phrasing.
        if (val(s.kick16) > 0.0f && m.reframe > 0.0f && sinceReframe_ >= kReframeGapSeconds && sinceCut_ >= kReframeHoldSeconds) {
            sinceReframe_ = 0.0f;
            lissPhase_ += 1.5f * std::min(m.reframe, 2.0f) * (2.0f * std::clamp(val(s.kickRandom), 0.0f, 1.0f) - 1.0f);
        }

        // The orbit on the mid clock: a Lissajous look path whose rates never line up, so it never
        // repeats, and whose speed follows the mids. A slow roll sway on the loudness clock, for
        // styles that turn.
        const float c = std::fmod(val(s.midTime), kTwoPi * 100.0f) + lissPhase_;
        soundYaw += 0.15f * amp * m.orbit * std::sin(0.6f * c);
        soundPitch += 0.09f * amp * m.orbit * (0.75f * std::sin(0.4f * c) + 0.25f * std::sin(1.324f * c));
        if (bankFlat) {
            const float k = std::fmod(val(s.curvedTime), kTwoPi * 100.0f);
            soundRoll += 0.05f * amp * m.orbit * (0.6f * std::sin(0.31f * k) + 0.4f * std::sin(0.73f * k));
        }
        // Lean with the stereo field; rise with the melody while there is one.
        soundYaw += 0.08f * amp * m.lean * std::clamp(val(s.stereoPan), -1.0f, 1.0f);
        soundPitch += 0.1f * amp * m.lean * (std::clamp(val(s.pitchHeight), 0.0f, 1.0f) - 0.5f) * std::clamp(val(s.pitchConfidence), 0.0f, 1.0f);
        // A new note looks somewhere a little different, at most twice a second.
        if (val(s.noteChange) > 0.0f && sinceNoteLook_ >= 0.5f) {
            sinceNoteLook_ = 0.0f;
            noteLook_ = 0.05f * (2.0f * std::clamp(val(s.noteRandom), 0.0f, 1.0f) - 1.0f);
        }
        soundYaw += noteLook_ * amp * m.lean;

        // A surge pushes in; sound coming back after silence pulls back out; both settle over ~2 s.
        // A rise (energyTrend over 0.5) creeps in until a big moment releases it.
        if (val(s.surge) > 0.0f) dollyKick_ = std::max(dollyKick_, 0.5f);
        if (val(s.returned) > 0.0f) dollyKick_ = std::min(dollyKick_, -0.4f);
        dollyKick_ *= std::exp(-dt / 2.0f);
        const float creep = 0.3f * std::clamp(2.0f * (val(s.energyTrend) - 0.5f), 0.0f, 1.0f);
        soundDolly = amp * m.dolly * (dollyKick_ + creep);

        // The bass breathes the lens: up to 6% narrower on a strong bass, which reads as a punch in.
        fovTarget *= 1.0f - 0.06f * amp * m.breath * std::clamp(val(s.bassAtt) - 1.0f, 0.0f, 1.0f);

        // Trauma: hits pile it up, it bleeds off over a second, and the shake grows with its square,
        // so single hits stay subtle and a run of them builds. Busy highs quicken the noise, never
        // its size.
        trauma_ = std::min(1.0f, trauma_ + m.shake * amp * (0.3f * std::clamp(val(s.kick), 0.0f, 1.0f) + 0.2f * std::clamp(val(s.snare), 0.0f, 1.0f)));
        trauma_ = std::max(0.0f, trauma_ - 1.2f * dt);
        traumaClock_ = std::fmod(traumaClock_ + dt * (8.0f + 5.0f * std::clamp(val(s.flux), 0.0f, 2.0f)), kPathPeriod);
    }
    const float lookYawTarget = lookYaw + soundYaw;
    const float lookPitchTarget = lookPitch + soundPitch;
    const float rollTarget = p.camRoll * kMaxRoll * g + soundRoll;
    const float dollyTarget = std::clamp(p.camPush + soundDolly, -1.0f, 1.0f);
    fovTarget = std::clamp(fovTarget, 0.5f, 2.0f);
    if (!primed_ || cut) {
        yaw_.reset(lookYawTarget);
        pitch_.reset(lookPitchTarget);
        roll_.reset(rollTarget);
        dolly_.reset(dollyTarget);
        fov_.reset(fovTarget);
        if (!primed_) speed_.reset(speedTarget);
        orbit_.reset(p.camOrbit * g);
        primed_ = true;
    }
    yaw_.step(lookYawTarget, dt, soft);
    pitch_.step(lookPitchTarget, dt, soft);
    roll_.step(rollTarget, dt, soft * 1.5f);
    dolly_.step(dollyTarget, dt, soft * 0.6f);
    fov_.step(fovTarget, dt, soft);
    // Speed eases slower than the look: a surge should feel like thrust.
    speed_.step(speedTarget, dt, soft * 2.0f);
    orbit_.step(std::clamp(p.camOrbit, 0.0f, 1.0f) * g, dt, soft * 3.0f);

    time_ = std::fmod(time_ + dt, kPathPeriod);
    travel_ = std::fmod(travel_ + std::max(speed_.x, 0.0f) * dt, kTravelWrap);
    const int path = std::clamp(p.camPath, 0, 3);

    const Vec3 here = pathPoint(path, orbit_.x, travel_);
    const Vec3 ahead = pathPoint(path, orbit_.x, travel_ + kLookAhead);
    Vec3 fwd = path == 2 ? normalize(scale(here, -1.0f)) : (path == 3 ? Vec3{0.0f, 0.0f, 1.0f} : normalize(sub(ahead, here)));
    const Vec3 worldUp{0.0f, 1.0f, 0.0f};
    Vec3 right = normalize(cross(worldUp, fwd));
    Vec3 up = cross(fwd, right);

    // Bank into the curve: the path's sideways acceleration tilts the horizon.
    float bank = 0.0f;
    if (path == 0 || path == 1) {
        const Vec3 further = pathPoint(path, orbit_.x, travel_ + 2.0f * kLookAhead);
        const Vec3 turn = sub(normalize(sub(further, ahead)), fwd);
        bank = std::clamp(-dot(turn, right) * 1.2f, -0.5f, 0.5f) * g;
    }
    const float shake = std::clamp(p.camShake, 0.0f, 1.0f) * kShakeRadians * g;
    // The trauma shake goes on the view only, after the springs, so it never feeds back into the pose.
    const float trauma = trauma_ * trauma_ * kTraumaRadians;
    const float sYaw = shake * wobble(time_, 7.3f, 11.9f) + trauma * wobble(traumaClock_, 1.0f, 1.618f);
    const float sPitch = shake * wobble(time_, 8.9f, 13.1f) + trauma * wobble(traumaClock_, 1.27f, 2.09f);
    const float yaw = yaw_.x + sYaw;
    const float pitch = pitch_.x + sPitch;
    const float roll = roll_.x + bank;

    fwd = rotate(fwd, up, yaw);
    right = rotate(right, up, yaw);
    fwd = rotate(fwd, right, -pitch);
    up = rotate(up, right, -pitch);
    right = rotate(right, fwd, roll);
    up = rotate(up, fwd, roll);

    pose_.pos = add(here, scale(fwd, dolly_.x * kDollyReach));
    pose_.fwd = fwd;
    pose_.right = right;
    pose_.up = up;
    pose_.focal = kBaseFocal / fov_.x;
    pose_.travel = travel_;
    pose_.dolly = dolly_.x;
    pose_.yaw = yaw;
    pose_.pitch = pitch;
    pose_.roll = roll;
    // Flat projection: the path's lateral wander becomes a gentle pan, the
    // dolly and FOV become zoom, and the look tilts the image plane.
    pose_.flat = {0.05f * here[0], 0.05f * here[1], (1.0f + 0.35f * dolly_.x) / std::max(fov_.x, 0.25f), bankFlat ? roll : roll_.x};
    pose_.tilt = {std::clamp(yaw, -1.0f, 1.0f) * kFlatTilt, std::clamp(pitch, -1.0f, 1.0f) * kFlatTilt};
}

}  // namespace geode::viz
