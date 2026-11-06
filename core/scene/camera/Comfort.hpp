#pragma once

#include <array>

#include "scene/camera/CameraParams.hpp"
#include "scene/camera/Drivers.hpp"

// The comfort stage, run on the blended pose after every driver, then the always-on motion clamp.
//
//   level    turn speed   flips          horizon back    cuts
//   Calm     <= 45 deg/s  none           ~1.5 s          at most 1 per 2 s
//   Normal   <= 90 deg/s  <= 2 a minute  ~3 s            at most 1 per 2 s
//   Wild     no cap       no cap         ~3 s            only the safety clamp's
//
// Then, on every level and in every mode, the safety clamp: the view never turns faster than
// kSafetyMaxTurn and never cuts more often than once per kSafetyCutGap, so no setting can strobe the
// picture through the camera (the flash limiter's companion for motion, #511 story 36).
//
// A flip is the camera going upside down: its up vector below the horizon (up . worldUp < 0). Where no
// flip is allowed the roll is held short of a quarter turn, which keeps the up vector above the horizon.
// Horizon recovery eases any roll beyond the reference (the base flight's own roll plus a held look
// roll) back out, unless a route holds the roll (a roll or pitch rate, camera.holdRoll, a keyframe).
//
// A cut is a pose jump with no tween. An allowed cut snaps; one that comes too soon after the last is
// played as motion instead: the turn under the speed cap and the position eased over kCutGlideSeconds.

namespace geode::scene::camera {

struct ComfortLimits {
    float maxTurn;          // rad/s; the safety clamp's for Wild
    int flipsPerMinute;     // < 0: no budget
    float recoverySeconds;  // ~95 % of an excess roll gone in this long
    float cutGap;           // seconds between allowed cuts
};

constexpr float kSafetyMaxTurn = 4.0f * kPi;  // 720 deg/s: half a second for a whole turn
constexpr float kSafetyCutGap = 0.5f;
constexpr float kCutGlideSeconds = 0.6f;
constexpr float kFlipWindowSeconds = 60.0f;
// Where no flip is allowed: the roll is held inside this (~81 deg) and the forward this far (80 deg) from
// the horizon, so the up vector keeps a margin above it even after a capped step.
constexpr float kNoFlipRoll = 0.5f * kPi - 0.15f;
constexpr float kNoFlipElevation = 1.3962634f;

ComfortLimits limitsFor(Comfort level);

class ComfortStage {
public:
    struct Input {
        Pose target;           // the blended drivers' pose
        float referenceRoll = 0.0f;  // the roll from level horizon recovery returns to
        bool rollHeld = false;       // a route holds the roll: no recovery this frame
        bool cut = false;            // a driver jumped this frame
        bool primeOnly = false;      // take the target as it is and start from it (a reset, a mode change)
        // Rails: the base flight keeps today's law; only the cut spacing and the safety clamp act.
        bool safetyOnly = false;
    };
    struct Output {
        Pose pose;
        bool untouched = false;  // the stage changed nothing: pose == target
        bool cutTaken = false;
        bool flipped = false;    // a new flip started this frame
    };

    void reset();
    Output step(const Input& in, Comfort level, float dt);

    // Flips counted in the last minute (Normal's budget); a test hook.
    int recentFlips() const;
    int cutsTaken() const { return cutsTaken_; }

private:
    bool primed_ = false;
    Pose last_{};
    double clock_ = 0.0;
    float sinceCut_ = 1e6f;
    int cutsTaken_ = 0;
    // Horizon recovery: the drivers' roll beyond the reference last frame, and the output's.
    float driverExcess_ = 0.0f;
    float outputExcess_ = 0.0f;
    bool excessKnown_ = false;
    bool inverted_ = false;
    std::array<double, 8> flipTimes_{};
    int flipCount_ = 0;  // entries used in flipTimes_ (a ring once full)
    int flipHead_ = 0;
    Vec3 posGlide_{0.0f, 0.0f, 0.0f};

    void recordFlip();
    void prime(const Pose& p);
};

}  // namespace geode::scene::camera
