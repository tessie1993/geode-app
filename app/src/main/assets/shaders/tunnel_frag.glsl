#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uAudioTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_react
//#include lib_palette
//#include lib_scene_grade

// Spectrum tunnel: ring brightness from bands, flown on the shared camera rig.
//
// The tube is a cylinder down the world's z axis through the eye, intersected
// in closed form - no march, the same cost as the 2D version. The rig's ray
// (camFly) does the rest: its travel (uCamTravel) is the distance down the
// tube, and its look, banking, shake and FOV bend where the vanishing point
// sits on screen, so the composite leaves this picture alone.
//
// What reacts to what:
//   - travel speed is the Camera tab's fly speed; a kick punch on the rig's
//     dolly lurches the eye down the tube with it.
//   - ring brightness rides the analyser band under each ring, the wave
//     overlay the treble, as before.
//   - kick (uHits.x) launches a ripple down the tube: a front that leaves the
//     eye on the hit and travels away as the envelope falls, shifting ring
//     positions as it passes. It moves rings, never their brightness.

/** Tube radius in world units. With TUN_DEPTH_SCALE it reproduces the old 0.3 / radius depth at the rig's base focal. */
#define TUN_RADIUS 0.1
/**
 * Tube depth units per world unit. 2 keeps the old ring rate at the rig's
 * default speed. Every depth-periodic term below uses a frequency that
 * divides (8192 * 2), the rig's travel wrap in depth units, so the wrap
 * lands on the same rings and the same band lookup.
 */
#define TUN_DEPTH_SCALE 2.0
/** How far down the tube (depth units) the kick ripple has travelled by the time its envelope is spent. */
#define TUN_RIPPLE_REACH 7.0

void main() {
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);

    float angle = atan(rd.y, rd.x) / 6.2831853 + 0.5;
    // Where this ray meets the wall, measured along the tube from the eye.
    float wall = TUN_RADIUS * rd.z / (length(rd.xy) + 0.0001) * TUN_DEPTH_SCALE;
    float depth = (uCamTravel + 0.9 * uCamDolly) * TUN_DEPTH_SCALE + wall;

    // The kick ripple: 1 at the hit, so the front starts at the eye and runs
    // away down the tube as the envelope decays.
    float kick = clamp(uHits.x, 0.0, 1.0);
    float front = (1.0 - kick) * TUN_RIPPLE_REACH;
    float dFront = wall - front;
    float ripple = kick * 0.35 * sin(dFront * 6.0) * exp(-abs(dFront) * 1.2);

    float band = aband(fract(angle + depth * 0.0625));
    float rings = smoothstep(0.35, 0.9, fract(depth * 2.0 + ripple));
    float wave = awave(fract(angle * 2.0));
    vec3 col = pal(depth * 0.125) * (0.15 + band * 1.6) * rings;
    col += pal(0.6) * wave * uTreble * 0.6;
    col *= 1.0 - smoothstep(0.35, 1.8, length(uv) * uZoom);
    fragColor = vec4(grade(col), 1.0);
}
