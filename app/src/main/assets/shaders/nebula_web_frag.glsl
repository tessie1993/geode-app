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
//#include lib_nebula

// Nebula Web: flying through the cosmic web, the largest structure there is
// - thin glowing filaments of gas strung between bright knots where galaxy
// clusters gather, around great dark voids. Filaments come out of the dark
// ahead, thicken as they pass, and knots slide by like lanterns.
//
// Volume-marched, flown on the rig's path (camFly). The web is nebWeb3: the
// distance to the edges and vertices of a cubic lattice, warped by slow
// sines and unioned with a second lattice turned about the travel axis, so
// the grid reads as an irregular web. That field is a true distance, so the
// march skips the empty voids in long strides and spends its steps on the
// filaments; it needs no noise to find them, which is what makes a long
// fly-through affordable. One octave of value noise clumps the filaments
// along their length. The medium is emissive (nebVolEmit): the web glows,
// and only the dense knots block what is behind them. Every z frequency in
// the warp is a multiple of pi/2 on a half-scale lattice, so the whole web
// repeats exactly with the rig's 8192-unit travel wrap.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (SND_BASS_PRESENCE with the bass band region) THICKENS THE FILAMENTS: the
// web swells on the low end and thins to threads in quiet passages. Each
// onset (SND_ONSET_ENV, SND_ONSET_COUNT) re-picks the knots' hue, sliding to it as the
// onset envelope falls rather than stepping. The key turns the filaments'
// hue. The hats twinkle the stars beyond. Overall level follows SND_ENERGY_TREND
// gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the web's slow undulation and the twinkle (the flight
// is the rig's). Complexity: uMorph adds a second clumping octave
// (nebVolOctaves). Chaos: uWarp/uTurbulence bend the web through nebWarp3;
// uTwist via view(). Colour: filaments and knots from pal(). Energy:
// uIntensity scales the bass thickening. Space: the rig (camFly).

#define WB_FAR 30.0
/** World units per lattice cell: q = p * WB_SCALE, a power of two so the lattice wraps with the rig. */
#define WB_SCALE 0.5
#define WB_HALF_PI 1.5707963

/** The second lattice, turned about z: z is left alone, so its period survives. */
const mat2 WB_TURN = mat2(0.7071068, 0.7071068, -0.7071068, 0.7071068);

float gT;
float gWidth;
float gOct;

/** x = distance to the nearest filament, y = to the nearest knot, both in world units. */
vec2 web(vec3 p) {
    vec3 q = nebWarp3(p, gT) * WB_SCALE;
    q += 0.22 * vec3(sin(q.y * 1.31 + gT * 0.05) + sin(q.z * WB_HALF_PI),
                     sin(q.x * 1.17 - gT * 0.04) + sin(q.z * 3.1415927 + 1.3),
                     sin(q.x * 0.93 + 0.7) + sin(q.y * 1.53 + gT * 0.03));
    vec2 a = nebWeb3(q);
    vec3 q2 = vec3(WB_TURN * q.xy * 1.0 + vec2(0.37, 0.61), q.z + 0.5);
    vec2 b = nebWeb3(q2);
    // The sine warp stretches space by under 2 (row sums of its Jacobian):
    // divide so the skip stays a lower bound.
    return min(a, b) / (WB_SCALE * 2.0);
}

void main() {
    vec2 uv = view();
    gT = nebTime();
    float e = nebEnergy();
    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float bass = clamp(0.6 * SND_BASS_PRESENCE + 0.4 * sndLevel(SND_BASS_ATT), 0.0, 1.3) * e;
    float hats = clamp(SND_HAT_ENV, 0.0, 1.0);
    float key = nebKeyShift(0.1);
    gOct = nebVolOctaves(1.0);
    gWidth = 0.07 * (1.0 + 0.7 * bass);

    // Knot hue: re-picked per onset, sliding to the new value as the envelope falls.
    float env = clamp(SND_ONSET_ENV, 0.0, 1.0);
    env = env * env * (3.0 - 2.0 * env);
    float seed = mod(floor(SND_ONSET_COUNT), 512.0);
    float knotHue = mix(nebHash1(seed), nebHash1(seed - 1.0), env);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);

    vec3 filCol = nebEmission(0.6 + key, 0.1);
    vec3 knotCol = nebEmission(0.08 + 0.35 * knotHue, 0.55);

    vec4 acc = vec4(0.0, 0.0, 0.0, 1.0);
    float tt = 0.1 + nebDither() * 0.08;
    for (int i = 0; i < NEB_VOL_MAX; i++) {
        if (float(i) >= uSteps) break;
        if (tt > WB_FAR) break;
        vec3 p = ro + rd * tt;
        vec2 d = web(p);
        // Filaments widen a little with distance, so far ones stay sampled.
        float w = gWidth * (1.0 + tt * 0.05);
        float dt = nebVolDt(tt, 0.03, 0.012);
        if (d.x < 3.0 * w || d.y < 7.0 * w) {
            // Inside a filament's or knot's glow: never step past its width.
            dt = min(dt, w);
            float clump = 0.45 + 1.1 * nebFbm3(p * 1.0 + vec3(1.3, 4.1, 0.0), gOct);
            float fil = exp(-(d.x * d.x) / (w * w)) * clump;
            float knot = exp(-(d.y * d.y) / (w * w * 9.0));
            float fade = exp(-tt * 0.06) * smoothstep(0.3, 1.2, tt);
            vec3 emission = (filCol * fil * 0.9 + knotCol * knot * 3.0) * fade / (1.0 + tt * 0.05);
            nebVolEmit(acc, emission, knot * 6.0 + fil * 0.6, dt);
            if (acc.a < NEB_TRANS_MIN) break;
        } else {
            dt = max(dt, min(d.x - 2.5 * w, d.y - 6.5 * w) * 0.9);
        }
        tt += dt;
    }

    vec3 bg = pal(0.66 + key) * 0.012 + nebStars(nebSky(rd), 7.0, 2, vec2(0.0), 0.8, 0.16, hats, gT) * 0.7;
    vec3 col = acc.rgb + acc.a * bg;
    col *= 0.85 + 0.15 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
