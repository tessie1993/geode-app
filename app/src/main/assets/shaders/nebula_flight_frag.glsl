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
//#include lib_nebula

// Nebula Flight: flying through the inside of a gas cloud. Banks of glowing
// gas loom up, part around the camera and slide past; thin edges glow in the
// palette's hues, dense cores darken and take a lit rim on the side facing a
// distant star; through the gaps, stars.
//
// Volume-marched, flown on the rig's path (camFly). The cloud is one fbm of
// 3D value noise thresholded into banks and voids. Its scale is exactly 1/2
// and its octaves double exactly, so the noise lattice wraps with the rig's
// 8192-unit travel (see lib_nebula) and the flight never jumps. A bubble
// around the eye keeps the camera from flying blind into a bank. One density
// tap toward the light shades each sample (nebLightTap); the step grows with
// distance (nebVolDt), so the near cloud is fine and the far cloud cheap.
//
// ---- audio ------------------------------------------------------------------
//
// The KEY COLOURS THE CLOUD: the detected key (uKeyHue, gated by
// uKeyStrength) slides the whole gas palette, so a change of key re-tints
// the flight over a few seconds (the key hue is eased on the CPU). Mids
// (uMidSmooth) widen the spread of hues between thin and dense gas, so busy
// passages colour the cloud more variously. Bass (uBassSmooth) thickens the
// banks. The hats twinkle the stars. Overall level follows uSwell gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the cloud's slow churn and the twinkle (the flight is
// the rig's). Complexity: uMorph adds a noise octave (nebVolOctaves, at most
// four). Chaos: uWarp/uTurbulence swirl the cloud through nebWarp3; uTwist
// via view(). Colour: every hue is pal(). Energy: uIntensity scales the key's
// pull, the mid spread and the bass thickening. Space: the rig (camFly).

#define FL_FAR 26.0
#define FL_SIGMA 2.2
/** Hue changes along the path with a 64-unit period, which divides the rig's 8192 wrap. */
#define FL_Z_FREQ 0.09817477

const vec3 FL_LIGHT = vec3(0.32, 0.8, 0.5);

float gT;
float gThresh;

float density(vec3 p, float oct) {
    vec3 q = nebWarp3(p, gT) * 0.5 + vec3(0.0, gT * 0.01, 0.0);
    float n = nebFbm3(q, oct);
    return smoothstep(gThresh, gThresh + 0.3, n);
}

void main() {
    vec2 uv = view();
    gT = nebTime();
    float e = nebEnergy();
    float swell = clamp(uSwell, 0.0, 1.5);
    float bass = clamp(uBassSmooth, 0.0, 1.3) * e;
    float mid = clamp(uMidSmooth, 0.0, 1.3) * e;
    float hats = clamp(uHits.z, 0.0, 1.0);
    float oct = nebVolOctaves(3.0);
    float key = nebKeyShift(0.3 * clamp(e, 0.0, 1.5));
    gThresh = 0.47 - 0.06 * bass;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    vec3 L = normalize(FL_LIGHT);
    float spread = 0.25 + 0.2 * mid;

    vec4 acc = vec4(0.0, 0.0, 0.0, 1.0);
    float tt = 0.1 + nebDither() * 0.12;
    for (int i = 0; i < NEB_VOL_MAX; i++) {
        if (float(i) >= uSteps) break;
        if (tt > FL_FAR) break;
        vec3 p = ro + rd * tt;
        float dt = nebVolDt(tt, 0.1, 0.03);
        float dens = density(p, oct) * smoothstep(0.4, 2.2, tt);
        if (dens > 0.002) {
            float dl = density(p + L * 0.6, 2.0);
            float lit = nebLightTap(dl, 3.0);
            float hue = 0.1 + key + 0.12 * sin(p.z * FL_Z_FREQ + 1.3) + spread * dens;
            vec3 gas = nebEmission(hue, 0.0);
            // Thin gas glows; dense gas scatters the light it gets.
            float edge = 4.0 * dens * (1.0 - dens);
            vec3 emit = gas * (0.1 * edge + 0.02) + mix(gas, vec3(1.0, 0.95, 0.88), 0.4) * lit * 0.22 * dens;
            emit *= exp(-tt * 0.04);
            nebVolStep(acc, emit, dens * FL_SIGMA, dt);
            if (acc.a < NEB_TRANS_MIN) break;
        }
        tt += dt;
    }

    // Space beyond: a faint glow toward the light, and stars.
    vec2 sky = nebSky(rd);
    vec3 bg = nebEmission(0.6 + key, 0.0) * (0.02 + 0.06 * pow(max(dot(rd, L), 0.0), 4.0));
    bg += nebStars(sky, 6.0, 3, vec2(0.0), 1.0, 0.22, hats, gT);

    vec3 col = acc.rgb + acc.a * bg;
    col *= 0.85 + 0.15 * swell;
    col *= 0.78 + 0.22 * smoothstep(2.2, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
