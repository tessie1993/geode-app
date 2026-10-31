#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_dive

// Dive Orbit: the gyroscope cells flown straight while the gaze swings slowly
// side to side and a little up and down, looking round the gimbals as they
// pass like a diver turning to watch them. Burnished brass rings in a warm
// haze.
//
// ---- audio ------------------------------------------------------------------
//
// Mids (sndShaped(SND_MIDDLE)) drive the rings (on a loudness-set base rate, as in Dive
// Gyro). The passage's swell (SND_ENERGY_TREND) widens the look-around: a loud passage
// turns the head further. Palette switching as in lib_dive. Brightness
// follows SND_ENERGY_TREND mildly.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock, the rings and the swing. Complexity: uMorph
// adds a fourth ring. Chaos: uWarp/uTurbulence bend the rings. Energy:
// uIntensity scales the mids' push. Colour: diveColor, warmed. Space: the
// rig's look rides on top of the swing.

void main() {
    vec2 uv = view();
    diveSetup(DIVE_GYRO);
    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    gDiveSwing = 0.5 + 0.25 * swell;

    vec3 ro;
    vec3 rd;
    diveCamera(DIVE_CAM_ORBIT, uv, ro, rd);

    float used;
    float hitT = diveMarch(ro, rd, used);
    vec3 brass = vec3(1.0, 0.76, 0.42);
    vec3 col = mix(diveBackdrop(rd), brass * 0.06, 0.4);
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = diveNormal(p, diveEps(hitT));
        vec3 base = mix(diveMaterial(gDiveHitMat, gDiveHitId, 0.0), brass, 0.45) * 0.55;
        col = diveShade(n, rd, hitT, used, base, 64.0);
        vec3 refl = reflect(rd, n);
        col += brass * diveColor(0.4 + 0.4 * refl.y) * 0.3 * pow(1.0 - max(dot(n, -rd), 0.0), 2.0) * exp(-0.055 * hitT);
    }

    col *= 0.85 + 0.2 * swell;
    fragColor = vec4(grade(col), 1.0);
}
