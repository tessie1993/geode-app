#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_dive

// Dive Gyro: a straight dive between cells of gyroscopes - at the centre of
// every cell three nested rings turn inside each other like a gimbal, each
// cell with its own phase, tilt and direction - polished metal catching the
// headlamp and reflecting the coloured haze.
//
// ---- audio ------------------------------------------------------------------
//
// Mids (uMidSmooth) drive the rings: they push every gimbal ahead while the
// mids climb, on top of a base rate that loudness sets (uFlowPhase, which
// only ever advances, so the rings never stutter backwards). Palette switching
// as in lib_dive. Brightness follows uSwell mildly.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock and the rings. Complexity: uMorph adds a
// fourth ring and grows the core. Chaos: uWarp/uTurbulence bend the rings.
// Energy: uIntensity scales the mids' push. Colour: diveColor. Space: the rig.

void main() {
    vec2 uv = view();
    diveSetup(DIVE_GYRO);

    vec3 ro;
    vec3 rd;
    diveCamera(DIVE_CAM_STRAIGHT, uv, ro, rd);

    float used;
    float hitT = diveMarch(ro, rd, used);
    vec3 col = diveBackdrop(rd);
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = diveNormal(p, diveEps(hitT));
        vec3 base = diveMaterial(gDiveHitMat, gDiveHitId, 0.0) * 0.6;
        col = diveShade(n, rd, hitT, used, base, 48.0);
        // Metal: the haze, reflected.
        vec3 refl = reflect(rd, n);
        float fres = 0.25 + 0.75 * pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
        col += diveColor(0.3 + 0.5 * refl.y + gDiveHitId) * 0.35 * fres * exp(-0.055 * hitT);
    }

    col *= 0.85 + 0.2 * clamp(uSwell, 0.0, 1.5);
    fragColor = vec4(grade(col), 1.0);
}
