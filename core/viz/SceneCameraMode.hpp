#pragma once

#include <string>

#include "scene/camera/SceneCamera.hpp"
#include "viz/Catalogue.hpp"

namespace geode::viz {

// The Scene camera's Mode for a style, from the catalogue's rails flag (#473). A style a port listed
// as 6-DoF flies free; a marched style still on rails is clamped to today's rig limits; a style that
// does not march (2D shaders, the simulations, MilkDrop) has no Scene camera of its own and keeps Rails,
// which is today's rig bit for bit. One function so the renderer and scene_camera_test agree.
inline scene::camera::Mode sceneCameraMode(const std::string& styleId) {
    return catalogue::cameraMode(styleId) == catalogue::CameraMode::SixDof ? scene::camera::Mode::SixDof
                                                                             : scene::camera::Mode::Rails;
}

}  // namespace geode::viz
