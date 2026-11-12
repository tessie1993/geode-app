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

    bool operator==(const CameraPose&) const = default;
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
    // World units at camPush 1. 0.9 was about one second of travel on a fly-through, too little to see.
    static constexpr float kDollyReach = 2.5f;
    static constexpr float kBaseFocal = 1.6f;
    static constexpr float kShakeRadians = 0.025f;
    static constexpr float kFlatTilt = 0.35f;      // 2D plane tilt is gentler than the 3D look
    static constexpr float kFlatOctaves = 0.35f;   // octaves / s a flat style dives at camFly 1, camFlat 1
    // glide, spiral, orbit, hover, drive, fall, rise, weave, corkscrew, swoop, strafe, pendulum
    static constexpr int kPathCount = 12;
    // Reversing turns the view around over this much speed (world units / s), so the swing is
    // continuous through a stop instead of a snap.
    static constexpr float kTurnAroundSpeed = 0.25f * kBaseSpeed;
    // ...and the view follows that swing through its own spring, so a quick reversal (a snappy camSmooth,
    // a fast camFly) still turns at a bounded rate, never in a few frames.
    static constexpr float kTurnAroundSeconds = 0.6f;
    static constexpr float kFallPitch = 0.3f;      // radians the nose tips down (fall) or up (rise)
    // False on the paths whose z is a closed loop rather than travel (orbit, hover, pendulum): no dive,
    // no bank, no turn-around.
    static bool travels(int path);

    struct LookInput {
        float yaw = 0.0f;    // -1..1, touch anchor or gyro
        float pitch = 0.0f;
    };

    // How hard the music moves below play, from the auto-director (Director::moves); 1 everywhere is as styled.
    struct Moves {
        // orbit, lean and reframe reach nothing since the music stopped turning the view (owner
        // 2026-11-12, no style spins to the music); the director still writes them.
        float orbit = 1.0f;    // was the mid-clock Lissajous look and the roll sway
        float lean = 1.0f;     // was the stereo pan, melody height and note look
        float dolly = 1.0f;    // surge push, return pull, the rise creep
        float breath = 1.0f;   // the bass FOV breath
        float shake = 1.0f;    // kick/snare trauma
        float reframe = 1.0f;  // was the new framing every 16th kick
        float cut = 1.0f;      // hard cuts: 0 never, 2 twice as often
    };
    void setMoves(const Moves& m) { moves_ = m; }

    void reset();
    // reducedMotion scales orbit, roll, bank, shake and the sound moves, the
    // vestibular terms the rig makes itself; the look still follows a finger.
    // Forward travel (camFly) comes in already scaled by
    // safety::apply, which owns that scale for every render path.
    // Fly speed below zero travels backward; the view swings around to face the way it goes.
    // bankFlat false keeps the path's banking out of the flat picture's roll
    // (a style that does not turn); the 3D basis still banks.
    // sound: this frame's GeodeSoundFrame, or null for no music moves (tests, a renderer with no audio).
    void step(const SceneParams& p, const LookInput& look, float dt, bool reducedMotion, bool bankFlat = true,
              const GeodeSoundFrame* sound = nullptr);

    // Hard cuts closer together than this are ignored; projectM's default minimum preset time is 20 s.
    static constexpr float kCutGapSeconds = 15.0f;

    static constexpr float kTraumaRadians = 0.04f;
    // Cuts so far: a test hook, and how a scene could tell the view jumped.
    int cuts() const { return cuts_; }
    const CameraPose& pose() const { return pose_; }

    // Octaves per second a flat (2D) style dives for these params: camFlat x camFly, so the sign of
    // Fly speed zooms in (forward) or out (backward), and 0 on the paths that do not travel (travels).
    // The renderer feeds it to the endless zoom (EndlessZoom.hpp), which loops without a pop either way. Forward travel arrives already scaled for reduced motion by safety::apply.
    static float flatDive(const SceneParams& p);

    // The path centre line at travel s, before look/dolly; public for tests. Strafe's eye stands off
    // to the side of this line, looking in at it.
    static std::array<float, 3> pathPoint(int path, float orbit, float s);

private:
    struct Spring {
        float x = 0.0f;
        float v = 0.0f;
        void reset(float to) { x = to; v = 0.0f; }
        // Critically damped; exact for constant target, stable for any dt.
        void step(float target, float dt, float seconds);
    };

    Spring yaw_, pitch_, roll_, dolly_, fov_, speed_, orbit_, facing_;
    float travel_ = 0.0f;
    float time_ = 0.0f;
    bool primed_ = false;
    CameraPose pose_;

    // The music's own moves (SoundMoves in CameraRig.cpp).
    Moves moves_;
    float trauma_ = 0.0f;
    float traumaClock_ = 0.0f;
    float sinceCut_ = kCutGapSeconds;
    float dollyKick_ = 0.0f;
    int cuts_ = 0;
    unsigned int seed_ = 0x2545F491u;
    float nextRandom();
};

}  // namespace geode::viz
