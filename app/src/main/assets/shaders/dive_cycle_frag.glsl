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

// Dive Cycle: the straight dive, through all five scenes in turn - lattice,
// jewel chambers, mushroom forest, gyroscopes, mandala - thirteen seconds
// each (DIVE_SCENE_SECONDS on the Speed clock). Between scenes the picture
// closes to dark through an iris and opens on the next one, so a scene never
// cuts and never flashes. The scene is the same for every pixel of a frame,
// so the march branches coherently and costs one scene per step.
//
// ---- audio ------------------------------------------------------------------
//
// Each scene keeps its own reaction: the kick swells the girders and pulses
// the caps, mids turn the gyroscopes, the beat swells the mandala's petals.
// The palette switches on section changes and every 16 onsets, crossfading
// (lib_dive). Brightness follows uSwell mildly.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock AND the cycle. Complexity: uMorph adds each
// scene's extra layer. Chaos: uWarp/uTurbulence bend the scenes. Energy:
// uIntensity scales the reactions. Colour: diveColor. Space: the rig.

void main() {
    vec2 uv = view();
    diveSetup(int(mod(diveSegment(), 5.0)));

    vec3 ro;
    vec3 rd;
    diveCamera(DIVE_CAM_STRAIGHT, uv, ro, rd);

    float used;
    float hitT = diveMarch(ro, rd, used);
    vec3 col = diveBackdrop(rd);
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = diveNormal(p, diveEps(hitT));
        col = diveShade(n, rd, hitT, used, diveMaterial(gDiveHitMat, gDiveHitId, hitT), 32.0);
    }

    col *= diveIris();
    col *= 0.85 + 0.2 * clamp(uSwell, 0.0, 1.5);
    fragColor = vec4(grade(col), 1.0);
}
