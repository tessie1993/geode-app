#pragma once
#include <array>

#include "sound/Stages.hpp"

namespace geode::viz {

// The phone in the hand as a force on the picture: which way is down, how hard it was shaken, how
// fast it was twisted. Tilt sets gravity, so particles, fluid dye and spawned Layers shapes fall
// toward the low side; a shake throws them about; a twist flings the world into a spin that runs
// down by friction; and the camera follows the tilt a little late, with a spring's overshoot, so the
// scene reads as having depth (parallax).
//
// Built like the sound engine (core/sound), MilkDrop's and TouchDesigner's architecture applied to a
// motion sensor instead of PCM:
//
//   write() per sensor event ─┬► accel ─► low-pass (gravity) ─► minus it: linear accel ─► slice peak
//                             ├► rotation ─► down vector,  twist rate × dt ─► slice turn
//   step(dt) once per frame ──┴► slice: Lag (gravity, tilt, shake), Trigger (a shake), Speed/Slope
//                                (turn ÷ dt is the twist rate; the spin integrates it), a spring (lean)
//
// TouchDesigner's time-slicing is the point: every sensor sample is seen as it arrives (50 Hz, between
// frames that may come at 30 or 120), and each frame reduces the slice since the last one, so a sharp
// shake that peaked between two frames still lands, and the twist is the angle actually turned, not a
// rate sampled at whatever moment the frame happened to draw. Every stage takes dt, so the same hand
// gives the same values at any frame rate. Lag and Trigger are the sound engine's own (sound/Stages).
//
// The shell only reads the sensors and writes their raw values here (Kotlin's PhoneMotionSensor: the
// game rotation vector plus the accelerometer, remapped for the display's rotation); everything else
// happens here, so the live view, the wallpaper and the dream all move the same way (ADR 0003). An
// export has no sensor: nothing is written, so every output stays at rest.
//
// The research and the numbers behind the filters are in docs/PHONE_MOTION.md.
//
// Pure: no GL, no allocation, no locks. Not thread-safe; the Renderer guards it like its SoundEngine.
class PhoneMotion {
public:
    // Screen coordinates throughout: x right, y up, z out of the glass.
    // The rotation vector, turned into which way the ground is and how fast the phone turns in its plane.
    void writeRotation(float downX, float downY, float downZ, float twistRate, float dtSeconds);
    // The accelerometer, m/s^2, gravity included (lying still, face up, it reads about (0, 0, 9.81)).
    void writeAccel(float ax, float ay, float az, float dtSeconds);

    // What one frame hands the effects and the reaction matrix.
    struct Frame {
        float gravityX = 0.0f;      // the in-plane pull, screen space, 0..1 long, times the amount
        float gravityY = 0.0f;
        float shake = 0.0f;         // 0..1: Lag of the slice's peak, instant rise, kShakeFallSeconds fall
        float shakeHit = 0.0f;      // > 0 on the frame a shake fired (its strength, 0..1), as a Trigger
        int shakeCount = 0;         // shakes so far, wrapping at kCountWrap (Count CHOP): a scatter seed
        float spin = 0.0f;          // the world's spin angle, radians, -pi..pi, back to 0 at rest
        float spinRate = 0.0f;      // rad/s
        float parallaxX = 0.0f;     // the camera's lagged lean, -1..1, overshoots a little
        float parallaxY = 0.0f;
        // The reaction matrix's Motion sources, raw (not scaled by the amount): see ReactSource.
        float tiltX = 0.0f;         // -1 left side down .. 1 right side down
        float tiltY = 0.0f;         // -1 top tipped away .. 1 top tipped toward you, from the held pose
        float twist = 0.0f;         // -1..1, the twist rate over kTwistFull
        bool live = false;          // a sensor wrote within kLiveSeconds
    };

    static constexpr float kStandardGravity = 9.80665f;
    static constexpr float kLiveSeconds = 0.5f;          // no sample for this long: no sensor, ease to rest
    static constexpr float kAccelGravitySeconds = 0.2f;  // the docs' low-pass that splits gravity from motion
    static constexpr float kRotationFreshSeconds = 0.3f; // a rotation vector this recent outranks the accelerometer
    static constexpr float kFlatDeadZone = 0.08f;        // |in-plane g| below this is lying flat: no pull
    static constexpr float kGravitySeconds = 0.12f;      // the Lag on down, against hand tremor
    static constexpr float kHeldPoseSeconds = 6.0f;      // tiltY's baseline: the pose you hold it in
    static constexpr float kParallaxSeconds = 0.35f;     // the camera's lag behind the tilt
    static constexpr float kParallaxDamping = 0.6f;      // < 1: the lean overshoots and settles
    static constexpr float kShakeFloor = 4.0f;           // m/s^2 of linear acceleration before it counts
    static constexpr float kShakeFull = 14.0f;           // m/s^2 that reads as a full shake
    static constexpr float kShakeRiseSeconds = 0.0f;
    static constexpr float kShakeFallSeconds = 0.6f;
    static constexpr float kShakeOn = 0.5f;              // Trigger threshold, re-armed below half of it
    static constexpr float kShakeRefractory = 0.3f;      // one shake is one hit, however many jolts it has
    static constexpr float kTwistDeadZone = 2.5f;        // rad/s: turning the phone in the hand is not a flick
    static constexpr float kTwistFull = 8.0f;            // rad/s that reads as twist 1
    static constexpr float kTwistSeconds = 0.05f;        // the Lag on the twist source
    static constexpr float kSpinGain = 0.9f;             // world rad/s gained per rad/s of flick past the dead zone, per second
    static constexpr float kSpinFrictionSeconds = 2.5f;
    static constexpr float kMaxSpinRate = 3.0f;          // rad/s, at amount 1
    static constexpr float kSettleBelowRate = 0.4f;      // rad/s under which the world starts to right itself
    static constexpr float kSettleSeconds = 1.2f;
    static constexpr int kCountWrap = 4096;

    void reset();
    // Reduces the slice written since the last step. amount: Customize › Camera › Phone motion, 0..1;
    // 0 leaves the built-in physics at rest but keeps the Motion sources live for routes.
    // reducedMotion drops the spin and halves the lean.
    void step(float amount, float dt, bool reducedMotion);
    const Frame& frame() const { return f_; }
    // The four reaction sources in ReactSource order (TiltX, TiltY, Shake, Twist).
    std::array<float, 4> sources() const { return {f_.tiltX, f_.tiltY, f_.shake, f_.twist}; }

private:
    void writeDown(float x, float y, float dtSeconds);

    Frame f_;
    // The slice: written per sample, reduced and cleared by step().
    float sliceShake_ = 0.0f;   // peak linear acceleration, m/s^2
    float sliceTurn_ = 0.0f;    // radians turned in the screen's plane
    float sliceSeconds_ = 0.0f; // sensor time covered
    float downTargetX_ = 0.0f, downTargetY_ = 0.0f;
    float sinceSample_ = 1e9f;
    float sinceRotation_ = 1e9f;
    // Per-sample state.
    std::array<float, 3> accelGravity_{0.0f, 0.0f, 0.0f};
    bool accelPrimed_ = false;
    float lastAccelAngle_ = 0.0f;
    bool accelAngleValid_ = false;
    // Per-frame state.
    float downX_ = 0.0f, downY_ = 0.0f;
    float heldY_ = 0.0f;
    bool primed_ = false;
    float leanX_ = 0.0f, leanVX_ = 0.0f;
    float leanY_ = 0.0f, leanVY_ = 0.0f;
    sound::Trigger shakeTrigger_;
};

}  // namespace geode::viz
