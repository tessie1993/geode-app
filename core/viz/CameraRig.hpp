#pragma once
#include <array>

#include "api/geode_api.h"
#include "viz/Params.hpp"

namespace geode::viz {

// The viewer, shared by every render path: live view, wallpaper, dream and
// export all step one of these from the same final SceneParams, so an export
// flies the same path the screen did.
//
// Music reaches it two ways. Reaction routes and LFOs write SceneParams
// (camFly, camPush, camYaw, ...) and the rig springs toward them, so a route
// can jump while the view glides. And the rig reads the sound frame itself for
// the moves a route cannot make: a cut (a jump with no tween), a trauma shake
// that piles up over hits, an orbit that runs on a music clock. See
// SoundMoves in CameraRig.cpp; every one fades with the music (presence) and
// grows with the song's intensity, scaled by the Motion dial.
//
// Pure: no GL, no allocation, no locks.
struct CameraPose {
    std::array<float, 3> pos{0.0f, 0.0f, 0.0f};
    std::array<float, 3> right{1.0f, 0.0f, 0.0f};
    std::array<float, 3> up{0.0f, 1.0f, 0.0f};
    std::array<float, 3> fwd{0.0f, 0.0f, 1.0f};
    float focal = 1.6f;    // 1 / tan(fov / 2) for a unit-height screen
    float travel = 0.0f;   // distance along the path, wraps at kTravelWrap
    float dolly = 0.0f;    // smoothed camPush, -1..1
    float yaw = 0.0f;      // smoothed look offsets, radians
    float pitch = 0.0f;
    float roll = 0.0f;
    // The flat-image projection of the same pose, for the families that draw
    // a 2D picture (composite pass): pan, zoom multiplier, tilt and roll.
    std::array<float, 4> flat{0.0f, 0.0f, 1.0f, 0.0f};  // panX, panY, zoom, roll
    std::array<float, 2> tilt{0.0f, 0.0f};              // yaw, pitch for the plane
};

class CameraRig {
public:
    // The path functions use whole multiples of 2*pi/kPathPeriod, so the path
    // repeats exactly and travel can wrap without a jump. A style that tiles
    // its world along z with a power-of-two period wraps seamlessly too.
    static constexpr float kPathPeriod = 1024.0f;
    static constexpr float kTravelWrap = 8192.0f;
    static constexpr float kBaseSpeed = 1.6f;      // world units / s at camFly 1
    static constexpr float kMaxYaw = 0.6f;         // radians at camYaw 1
    static constexpr float kMaxPitch = 0.45f;
    static constexpr float kMaxRoll = 0.8f;
    static constexpr float kTouchLook = 0.9f;      // radians at the screen edge
    static constexpr float kDollyReach = 0.9f;     // world units at camPush 1
    static constexpr float kBaseFocal = 1.6f;
    static constexpr float kShakeRadians = 0.025f;
    static constexpr float kFlatTilt = 0.35f;      // 2D plane tilt is gentler than the 3D look

    struct LookInput {
        float yaw = 0.0f;    // -1..1, touch anchor or gyro
        float pitch = 0.0f;
    };

    // How hard the music moves below play, from Customize › Macros (viz/Macros); 1 everywhere is as styled.
    struct Moves {
        float orbit = 1.0f;    // the mid-clock Lissajous look and the roll sway
        float lean = 1.0f;     // stereo pan, melody height and the note look
        float dolly = 1.0f;    // surge push, return pull, the rise creep
        float breath = 1.0f;   // the bass FOV breath
        float shake = 1.0f;    // kick/snare trauma
        float reframe = 1.0f;  // the new framing every 16th kick: 0 never
        float cut = 1.0f;      // hard cuts: 0 never, 2 twice as often
    };
    void setMoves(const Moves& m) { moves_ = m; }

    void reset();
    // reducedMotion scales travel, orbit, roll and shake, the vestibular
    // terms; the look still follows a finger.
    // bankFlat false keeps the path's banking out of the flat picture's roll
    // (a style that does not turn); the 3D basis still banks.
    // sound: this frame's GeodeSoundFrame, or null for no music moves (tests, a renderer with no audio).
    void step(const SceneParams& p, const LookInput& look, float dt, bool reducedMotion, bool bankFlat = true,
              const GeodeSoundFrame* sound = nullptr);

    // Hard cuts closer together than this are ignored; projectM's default minimum preset time is 20 s.
    static constexpr float kCutGapSeconds = 15.0f;
    // After a cut, the counted reframe holds off, so two structural moves never land together.
    static constexpr float kReframeHoldSeconds = 4.0f;
    static constexpr float kReframeGapSeconds = 8.0f;
    static constexpr float kTraumaRadians = 0.04f;
    // Cuts so far: a test hook, and how a scene could tell the view jumped.
    int cuts() const { return cuts_; }
    const CameraPose& pose() const { return pose_; }

    // The path centre line at travel s, before look/dolly; public for tests.
    static std::array<float, 3> pathPoint(int path, float orbit, float s);

private:
    struct Spring {
        float x = 0.0f;
        float v = 0.0f;
        void reset(float to) { x = to; v = 0.0f; }
        // Critically damped; exact for constant target, stable for any dt.
        void step(float target, float dt, float seconds);
    };

    Spring yaw_, pitch_, roll_, dolly_, fov_, speed_, orbit_;
    float travel_ = 0.0f;
    float time_ = 0.0f;
    bool primed_ = false;
    CameraPose pose_;

    // The music's own moves (SoundMoves in CameraRig.cpp).
    Moves moves_;
    float trauma_ = 0.0f;
    float traumaClock_ = 0.0f;
    float sinceCut_ = kCutGapSeconds;
    float sinceReframe_ = kReframeGapSeconds;
    float sinceNoteLook_ = 1.0f;
    float noteLook_ = 0.0f;
    float dollyKick_ = 0.0f;
    float lissPhase_ = 0.0f;
    int cuts_ = 0;
    unsigned int seed_ = 0x2545F491u;
    float nextRandom();
};

}  // namespace geode::viz
