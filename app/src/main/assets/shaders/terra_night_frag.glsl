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
//#include lib_terra

// Terrain Night: gliding over dark rolling hills under an aurora. The hills
// are lib_terra's softest field (fbm, eased toward rounded crowns), almost
// black, lit only from above by the aurora's green and a little starlight, so
// their crests read as soft edges against the sky. The aurora is a curtain
// of three sheets hung across the sky ahead, each standing on its own wavy
// line far out over the land - bright and green at its hem, fading through
// violet as it rises - and striated by fine vertical rays that drift along
// it. It is a sky object: it is placed
// relative to the eye, as something far away is, so the flight never passes
// under it.
//
// ---- audio ------------------------------------------------------------------
//
// Highs (uBandsHi.x and y, with uTrebleSmooth) LIGHT THE AURORA: its glow
// follows the upper bands, so bright treble passages set the curtain blazing
// and a quiet one leaves a faint ghost. That is a band level, continuous, not
// a hit, and it lights the sky band only. Mids (uMidSmooth) ripple the
// curtain's folds faster. Brightness otherwise follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the curtain's drift. Complexity (uMorph) fades in an
// octave of hill. Chaos (uWarp, uTurbulence, uTwist) bends the hills
// (terraChaos). Energy (uIntensity) scales how far the highs light the
// aurora. Colour is pal() in the aurora and the night sky. Space is the rig.

#define NT_MAX_STEPS 100
#define NT_FAR 40.0
#define NT_EYE 2.0
#define NT_AMP 1.2
#define NT_SHEETS 3

float gOct;

float height(vec2 xz) {
    float f = terraFbm(terraChaos(terraWrap(xz)), gOct);
    return NT_AMP * f * f * (3.0 - 2.0 * f);
}

/**
 * The aurora seen along rd (eye-relative): NT_SHEETS curtains, each a vertical
 * sheet standing on a wavy line z = Z(x) ahead. The ray meets a sheet where
 * its own z equals Z at its own x; two fixed-point steps find that well enough
 * for a soft curtain. The height of the meeting point picks the colour (green
 * hem, violet top) and the fade, so the curtain is continuous in elevation.
 */
vec3 aurora(vec3 rd, float t, float glow, float fold) {
    if (rd.y < 0.02 || rd.z < 0.05) return vec3(0.0);
    vec2 slope = rd.xy / rd.z;
    vec3 acc = vec3(0.0);
    for (int i = 0; i < NT_SHEETS; i++) {
        float k = float(i);
        float z = 16.0 + 9.0 * k;
        float x = 0.0;
        for (int j = 0; j < 2; j++) {
            x = slope.x * z;
            z = 16.0 + 9.0 * k + 4.0 * sin(x * 0.08 + t * 0.07 + 2.0 * k) + 3.0 * terraNoise(vec2(x * 0.1 + t * fold, 4.0 + 7.0 * k));
        }
        float v = slope.y * z - 4.5;
        float prof = smoothstep(-0.6, 0.4, v) * exp(-max(v, 0.0) / 3.0);
        float rays = 0.5 + 0.5 * sin(x * 1.3 + 3.0 * terraNoise(vec2(x * 0.4 - t * 0.2, 11.0 + k)));
        vec3 c = mix(mix(vec3(0.15, 1.0, 0.45), pal(0.38), 0.3), mix(vec3(0.6, 0.2, 0.9), pal(0.8), 0.3), smoothstep(0.0, 5.0, v));
        acc += c * prof * (0.35 + 0.65 * rays) * (1.0 - 0.25 * k);
    }
    return acc * glow * 0.45 * smoothstep(0.02, 0.1, rd.y);
}

void main() {
    vec2 uv = view();
    float t0 = terraTime();
    float swell = clamp(uSwell, 0.0, 1.5);
    float energy = terraEnergy();
    float highs = clamp(0.35 * (uBandsHi.x + uBandsHi.y) + 0.3 * uTrebleSmooth, 0.0, 1.2);
    float glow = (0.25 + 1.6 * highs * energy) * 1.4;
    float fold = 0.08 + 0.12 * clamp(uMidSmooth, 0.0, 1.0);
    gOct = terraOctaves(3.0);

    vec3 ro;
    vec3 rd;
    terraFly(uv, NT_EYE, 0.12, ro, rd);

    vec3 zenith = mix(vec3(0.005, 0.01, 0.03), pal(0.65) * 0.02, 0.4);
    vec3 horizon = mix(vec3(0.02, 0.04, 0.07), pal(0.55) * 0.05, 0.4);
    vec3 col = mix(horizon, zenith, pow(clamp(rd.y, 0.0, 1.0), 0.5));
    col += vec3(0.85, 0.9, 1.0) * terraStars(rd, 0.006) * smoothstep(0.0, 0.25, rd.y);
    col += aurora(rd, t0, glow, fold);
    // The aurora's light on the land: its colour, straight down.
    vec3 skyLight = mix(vec3(0.15, 1.0, 0.45), pal(0.38), 0.3) * glow * 0.12 + vec3(0.02, 0.03, 0.05);

    float hitT = -1.0;
    vec2 span = terraSlab(ro, rd, NT_AMP, NT_FAR);
    if (span.x >= 0.0) {
        float t = span.x;
        float tPrev = t;
        float dyPrev = 1.0;
        for (int i = 0; i < NT_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (t > span.y) break;
            vec3 p = ro + rd * t;
            float dy = p.y - height(p.xz);
            if (dy < terraEps(t)) {
                hitT = terraRefine(tPrev, dyPrev, t, dy);
                break;
            }
            tPrev = t;
            dyPrev = dy;
            t += terraStep(dy, t, 0.55);
        }
        // Out of budget short of the far end: a grazing ray creeping along a
        // slope. Shade the ground where it got to rather than show sky through it.
        if (hitT < 0.0 && t <= span.y) hitT = t;
    }

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = TERRA_NORMAL(height, p.xz, 0.004 * hitT + 0.01);
        float top = max(n.y, 0.0);
        float rim = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
        vec3 ground = vec3(0.10, 0.11, 0.11);
        col = ground * skyLight * (0.4 + 2.2 * top * top) * 6.0;
        col += ground * vec3(0.25, 0.3, 0.45) * max(dot(n, normalize(vec3(0.6, 0.5, -0.3))), 0.0);
        col += skyLight * rim * 0.25;
        col = mix(col, horizon, 1.0 - exp(-hitT * 0.05));
    }

    col *= 0.85 + 0.17 * swell;
    col *= 0.8 + 0.2 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
