#include "scene/camera/CameraParams.hpp"

#include <utility>
#include <vector>

#include "scene/camera/CameraMath.hpp"

namespace geode::scene::camera {

signal::ParamSchema cameraSchema() {
    using signal::ParamDef;
    using signal::ParamKind;
    // Order is CameraParam's; the static_assert below keeps the count honest.
    std::vector<ParamDef> rows{
        {"camera.comfort", ParamKind::Int, 1.0f, 0.0f, 2.0f},
        {"camera.yawRate", ParamKind::Float, 0.0f, -kTwoPi, kTwoPi},
        {"camera.pitchRate", ParamKind::Float, 0.0f, -kTwoPi, kTwoPi},
        {"camera.rollRate", ParamKind::Float, 0.0f, -kTwoPi, kTwoPi},
        {"camera.lookYaw", ParamKind::Circular, 0.0f, -kPi, kPi},
        {"camera.lookPitch", ParamKind::Circular, 0.0f, -kPi, kPi},
        {"camera.lookRoll", ParamKind::Circular, 0.0f, -kPi, kPi},
        {"camera.holdRoll", ParamKind::Bool, 0.0f, 0.0f, 1.0f},
        {"camera.strafe", ParamKind::Float, 0.0f, -4.0f, 4.0f},
        {"camera.rise", ParamKind::Float, 0.0f, -4.0f, 4.0f},
        {"camera.focal", ParamKind::Float, 1.0f, 0.5f, 2.0f},
        // viz::CameraRig::kTouchLook: a full drag turns the view as far as it does today.
        {"camera.takeoverReach", ParamKind::Float, 0.9f, 0.0f, kPi},
        {"camera.takeoverFollow", ParamKind::Float, 0.15f, 0.02f, 2.0f},
        {"camera.takeoverGlide", ParamKind::Float, 1.2f, 0.1f, 8.0f},
        {"camera.keyframeWeight", ParamKind::Float, 1.0f, 0.0f, 1.0f},
    };
    return signal::ParamSchema(std::move(rows));
}

static_assert(kCameraParamCount == 15, "cameraSchema() has one row per CameraParam");

}  // namespace geode::scene::camera
