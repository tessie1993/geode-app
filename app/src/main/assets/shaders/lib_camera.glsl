// The shared camera rig: one viewer for every style and every render path.
//
// core/viz/CameraRig.cpp steps the pose on the CPU once per frame from the
// final SceneParams (camFly, camPath, camPush, camYaw, ... which reaction
// routes and LFOs modulate) and springs toward them, so everything here is
// already smoothed. Live view, wallpaper, dream and export all run the same
// rig, so an exported video flies the path the screen did.
//
// A style that reads uCamBasis is treated as drawing the camera itself; the
// composite pass then leaves its picture alone. Every other style gets the
// rig's flat projection (pan, zoom, tilt, roll) in the composite instead, so
// nothing has to opt in to move.
//
// A style includes this after lib_scene_uniforms.

/** World-space eye on the rig's path. z grows with travel and wraps at 8192; tile the world along z with a power-of-two period and the wrap is invisible. */
uniform vec3 uCamPos;
/** Columns: right, up, forward. Already includes look (yaw/pitch/roll), banking and shake. */
uniform mat3 uCamBasis;
/** 1 / tan(fov / 2) for a unit-height screen; the FOV dial and drop surges move it. */
uniform float uCamFocal;
/** Distance travelled along the path, world units, wraps at 8192. */
uniform float uCamTravel;
/** The smoothed dolly offset, -1..1 (kick punches and drop pushes land here). */
uniform float uCamDolly;

/** One full turn per 1024 world units: multiply by an integer for a spin rate that survives the travel wrap. */
#define CAM_SPIN_UNIT (6.2831853 / 1024.0)
/** How much faster an object-centred style turns than its own spin rate says. An integer. */
#define CAM_AROUND_GAIN 8.0

/** The view ray through screen point `uv` (aspect-corrected, y up, as view() returns before zoom). */
vec3 camRayDir(vec2 uv) {
    return normalize(uCamBasis * vec3(uv, uCamFocal));
}

/** Fly-through: the eye rides the rig's path. For tunnels, fields and anything you travel through. */
void camFly(vec2 uv, out vec3 ro, out vec3 rd) {
    ro = uCamPos;
    rd = camRayDir(uv);
    // Grow and layers (lib_grow): the few units round the eye are the front layer, the way ahead the back.
    growSetup(ro, 3.0, false);
    gGrowBasis = uCamBasis;
}

/**
 * Object-centred: the eye sits `radius` behind the rig's forward direction,
 * looking back at the origin, and turns about +y at `spin` radians per world
 * unit of travel (use an integer multiple of CAM_SPIN_UNIT). For a style built
 * around one subject. The dolly pulls the eye in.
 */
void camAround(vec2 uv, float radius, float spin, out vec3 ro, out vec3 rd) {
    // A whole multiple, so the turn still closes at the travel wrap. Without it Fly speed turned the
    // subject at about half a degree a second at its default, too slow to see the slider act.
    float a = uCamTravel * spin * CAM_AROUND_GAIN;
    mat3 turn = mat3(cos(a), 0.0, -sin(a), 0.0, 1.0, 0.0, sin(a), 0.0, cos(a));
    mat3 b = turn * uCamBasis;
    ro = -b[2] * radius * (1.0 - 0.35 * uCamDolly);
    rd = normalize(b * vec3(uv, uCamFocal));
    // Grow and layers (lib_grow): the subject at the middle is the front layer, what lies beyond it the back.
    growSetup(ro, 1.5 * radius, true);
    gGrowBasis = b;
}
