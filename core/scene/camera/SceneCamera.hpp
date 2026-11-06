#pragma once

#include <cstddef>

#include "api/geode_api.h"
#include "scene/camera/CameraParams.hpp"
#include "scene/camera/Comfort.hpp"
#include "scene/camera/Drivers.hpp"
#include "signal/params/ParamStore.hpp"
#include "viz/CameraRig.hpp"
#include "viz/Params.hpp"

// geode::scene::camera: the one free camera every surface steps (live, export, wallpaper, dream, Cast:
// each owns a viz::Renderer, and each Renderer owns one of these). #512, spec #511, building #302.
//
// A frame, in order:
//   1. base     viz::CameraRig, today's rails flight, stepped from the frame's final SceneParams and the
//               sound frame. In SixDof the camera's integrated turn rates, held look and strafe/rise are
//               laid on top of it in its own frame.
//   2. takeover touch or gyro (Takeover): follows the finger, glides back to the base on release.
//   3. keyframes a Studio pose track at programme time (KeyframeTrack), weighted by
//               camera.keyframeWeight: at 1 it is exactly what the track says.
//   4. rails    Mode::Rails only: the layers above the base may turn the view at most today's
//               look limits from it (yaw ±0.6, pitch ±0.45, roll ±0.8), never flip it, and never move
//               it off the base's forward flight.
//   5. comfort  ComfortStage: speed cap, flip budget, horizon recovery, cut spacing by level, then the
//               always-on safety clamp (turn speed, cut rate). The app's reduced motion forces Calm.
//   6. out      the same viz::CameraPose fields as before (lib_camera's uCamPos, uCamBasis, uCamFocal,
//               uCamTravel, uCamDolly, uCamLook and the composite's flat projection), so no shader
//               and no style changes.
//
// Rails is the default for every style until the catalogue's rails row (#473) flips a ported family to
// SixDof. In Rails, with no keyframes, the pose is the rig's exactly (bit for bit) unless the safety clamp
// had to act, so every 3D style flies as it did before this camera.
//
// Params are core/signal's (cameraSchema()): modulation for a frame is written into params() before
// step() and is cleared by it, so a route never outlives its frame. Pure: no GL, no locks, no allocation
// after construction. One thread (the renderer's).

namespace geode::scene::camera {

enum class Mode : int { Rails = 0, SixDof = 1 };

struct FrameInput {
    const viz::SceneParams* params = nullptr;  // the frame's final params; null steps nothing
    viz::CameraRig::LookInput look{};          // touch and gyro, -1..1 per axis
    bool lookActive = false;                   // a finger is down or the gyro is on
    float dt = 0.0f;
    bool reducedMotion = false;                // the app's switch: forces Calm
    bool bankFlat = true;                      // viz::CameraRig::step's bankFlat
    const GeodeSoundFrame* sound = nullptr;
};

class SceneCamera {
public:
    SceneCamera();

    void reset();
    void step(const FrameInput& in);

    // The rails hook (#473). Leaving SixDof drops the integrated turns and slides.
    void setMode(Mode mode);
    Mode mode() const { return mode_; }

    // The user's level (camera.comfort's base). Any value outside the three is Normal.
    void setComfort(Comfort level);
    // The level the last frame ran at: Calm whenever reduced motion was on.
    Comfort comfort() const { return frameComfort_; }
    static Comfort effectiveComfort(Comfort chosen, bool reducedMotion) { return reducedMotion ? Comfort::Calm : chosen; }

    signal::ParamStore& params() { return store_; }
    const signal::ParamStore& params() const { return store_; }

    // How hard the music moves the base flight (viz/Macros, the director); forwarded to the rig.
    void setMoves(const viz::CameraRig::Moves& m) { rig_.setMoves(m); }

    // Studio keyframes. The track is copied; nothing in the app sets one yet (no Studio UI).
    void setKeyframes(const PoseKey* keys, std::size_t count) { keys_.set(keys, count); }
    void clearKeyframes() { keys_.clear(); }
    // Programme time for the track; a cut key is taken when this passes it.
    void setProgrammeTime(double seconds) { programmeTime_ = seconds; }

    // What the scenes read: a stable address for the renderer's lifetime (Scene::setCamera keeps it).
    const viz::CameraPose& pose() const { return pose_; }
    // The same frame as a 6-DoF pose.
    const Pose& framePose() const { return frame_; }
    const viz::CameraRig& rig() const { return rig_; }
    // Cuts the camera took (the comfort stage's count, after spacing).
    int cuts() const { return comfort_.cutsTaken(); }
    int recentFlips() const { return comfort_.recentFlips(); }

private:
    signal::ParamStore store_;
    viz::CameraRig rig_;
    Takeover takeover_;
    KeyframeTrack keys_;
    ComfortStage comfort_;
    Mode mode_ = Mode::Rails;
    Comfort frameComfort_ = Comfort::Normal;
    bool primed_ = false;
    int rigCuts_ = 0;
    double programmeTime_ = 0.0;
    double lastProgrammeTime_ = 0.0;
    // SixDof's integrated turns (radians, wrapped) and slide (world units).
    float spinYaw_ = 0.0f;
    float spinPitch_ = 0.0f;
    float spinRoll_ = 0.0f;
    Vec3 slide_{0.0f, 0.0f, 0.0f};
    viz::CameraPose pose_{};
    Pose frame_{};
};

}  // namespace geode::scene::camera
