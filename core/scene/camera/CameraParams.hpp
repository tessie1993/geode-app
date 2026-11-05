#pragma once

#include "signal/params/ParamSchema.hpp"

// The scene camera's params, declared through Group A's schema (core/signal/params), so every one is a
// route target with a base and this frame's modulation, clamped once by its range. Rows are in the
// order of CameraParam, so a CameraParam is the row's ParamId.
//
// Speeds (the *Rate rows, strafe and rise) are integrated by SceneCamera over dt, never applied as
// offsets, so changing a speed (by hand or by a route) never makes the picture jump: it only changes
// how fast the camera turns or slides from where it is.

namespace geode::scene::camera {

enum class Comfort : int { Calm = 0, Normal = 1, Wild = 2 };

// Which surface a renderer draws (geode_viz_set_surface_kind). The order is Group E's frame::Surface
// (Live, Wallpaper, Dream, Export), so the two can merge into one.
enum class Surface : int { Live = 0, Wallpaper = 1, Dream = 2, Export = 3 };

// The comfort a surface starts on: Calm on the ambient surfaces (the wallpaper and the dream, watched for
// hours from the corner of an eye), Normal on the others. An unknown kind is Live.
constexpr Comfort defaultComfort(Surface s) {
    return (s == Surface::Wallpaper || s == Surface::Dream) ? Comfort::Calm : Comfort::Normal;
}

// The level a renderer runs at: one chosen explicitly (geode_viz_set_camera_comfort, 0..2) wins over the
// surface's default; chosen < 0 (or out of range) means nobody chose. Reduced motion still forces Calm on
// top of this (SceneCamera::effectiveComfort).
constexpr Comfort comfortFor(Surface s, int chosen) {
    return (chosen >= 0 && chosen <= 2) ? static_cast<Comfort>(chosen) : defaultComfort(s);
}

enum CameraParam : signal::ParamId {
    kComfort = 0,       // Int 0..2: Calm, Normal (default), Wild. Reduced motion forces Calm.
    kYawRate,           // rad/s about the camera's up, integrated (SixDof only)
    kPitchRate,         // rad/s about its right, integrated: a held pitch rate loops
    kRollRate,          // rad/s about its forward, integrated: a held roll rate barrel-rolls
    kLookYaw,           // radians, a turn held on top of the base flight (SixDof only)
    kLookPitch,
    kLookRoll,          // a held roll: horizon recovery levels to it, not past it
    kHoldRoll,          // Bool: a route holds the roll, so the horizon is not recovered
    kStrafe,            // world units/s along the camera's right, integrated (SixDof only)
    kRise,              // world units/s along its up, integrated (SixDof only)
    kFocal,             // multiplier on the base focal length (narrower > 1)
    kTakeoverReach,     // radians a full touch or gyro deflection turns the view
    kTakeoverFollow,    // seconds the view takes to follow a finger
    kTakeoverGlide,     // seconds the view takes to glide back to its own flight on release
    kKeyframeWeight,    // 0..1: how much a Studio pose track overrides the layers below
    kCameraParamCount,
};

// The schema, one row per CameraParam. Ids are stable ASCII ("camera.yawRate", ...): save keys and the
// wire names route targets are generated from.
signal::ParamSchema cameraSchema();

}  // namespace geode::scene::camera
