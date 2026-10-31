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
//#include lib_terra

// Terrain Volcano: one volcano at night, framed whole on the shared rig
// (camAround, spin 0) from out on the plain, its cone standing against
// the sky. The cone is an
// exponential profile (concave flanks, as ash cones settle) with a bowl
// smooth-minned into its summit, ridged gullies running down its flanks and a
// rough lava plain around its foot. Glowing veins run down the slopes: the
// mid-contour of a noise read in a frame stretched along the downhill
// direction, so the lines run radially from the crater the way flows do. The
// crater glows from below; that glow lights the cone's upper flanks, and a
// faint red ash haze sits low over the plain under a starry sky.
//
// The cone stays near the origin, so this style ignores lib_terra's z tile.
// The radial direction is xz / r: no atan, so no seam.
//
// ---- audio ------------------------------------------------------------------
//
// Kicks (SND_KICK_ENV) PULSE THE LAVA: each kick swells the veins (wider and
// hotter, easing back) - light on the veins and in the crater only, never the
// frame. Bass (SND_BASS_PRESENCE) keeps the crater's glow breathing between kicks.
// The key (uKeyHue, gated by uKeyStrength) drifts the lava's hue a little.
// Brightness follows SND_ENERGY_TREND only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the lava's crawl. Complexity (uMorph) fades in an
// octave of gully. Chaos (uWarp, uTurbulence, uTwist) bends the flanks and
// the veins (terraChaos). Energy (uIntensity) scales the kick's pulse. Colour
// is pal() in the lava, rock and sky. Space is the rig (camAround).

#define VC_MAX_STEPS 112
#define VC_FAR 26.0
#define VC_CAM_R 11.0
#define VC_LIFT 0.5
/** Bound on the cone's height, rim and gullies included (terraSlab). */
#define VC_PEAK 2.3

float gOct;

float height(vec2 xz) {
    vec2 w = terraChaos(xz);
    float r = length(w);
    float cone = 3.6 * exp(-0.8 * r) - 0.4;
    float bowl = 1.6 + 1.2 * r * r;
    float h = -log(exp(-cone * 8.0) + exp(-bowl * 8.0)) / 8.0;
    float g = terraRidged(w * 1.5, gOct);
    return h + 0.32 * (g - 0.4) * smoothstep(0.4, 1.4, r);
}

/** The lava veins at xz: 1 on a vein, 0 off it. */
float vein(vec2 xz, float width, float t) {
    vec2 w = terraChaos(xz);
    float r = max(length(w), 1e-3);
    vec2 dir = w / r;
    float n = terraNoise(dir * 7.0 + vec2(0.0, r * 0.7 - t * 0.05));
    return (1.0 - smoothstep(0.0, width, abs(n - 0.5))) * smoothstep(0.45, 0.75, r) * (1.0 - smoothstep(1.6, 2.8, r));
}

void main() {
    vec2 uv = view();
    float t0 = terraTime();
    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float energy = terraEnergy();
    float kick = clamp(SND_KICK_ENV * energy, 0.0, 1.0);
    float bass = clamp(SND_BASS_PRESENCE, 0.0, 1.2);
    gOct = terraOctaves(3.0);
    float hue = 0.02 + 0.08 * uKeyStrength * sin(TERRA_TAU * uKeyHue);

    vec3 ro;
    vec3 rd;
    camAround(uv + vec2(0.0, 0.08), VC_CAM_R, 0.0, ro, rd);
    ro.y += VC_LIFT;

    vec3 moon = normalize(vec3(-0.6, 0.5, -0.4));
    vec3 lava = mix(vec3(1.0, 0.32, 0.06), pal(hue), 0.3);
    vec3 zenith = vec3(0.01, 0.01, 0.03);
    vec3 horizon = mix(vec3(0.20, 0.05, 0.04), pal(hue + 0.9) * 0.15, 0.3);

    float hitT = -1.0;
    float used = 0.0;
    vec2 span = terraSlab(ro, rd, VC_PEAK, VC_FAR);
    if (span.x >= 0.0) {
        float t = span.x;
        float tPrev = t;
        float dyPrev = 1.0;
        for (int i = 0; i < VC_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (t > span.y) break;
            used = float(i);
            vec3 p = ro + rd * t;
            float dy = p.y - height(p.xz);
            if (dy < terraEps(t)) {
                hitT = terraRefine(tPrev, dyPrev, t, dy);
                break;
            }
            tPrev = t;
            dyPrev = dy;
            t += terraStep(dy, t, 0.4);
        }
        // Out of budget short of the far end: a grazing ray creeping along a
        // slope. Shade the ground where it got to rather than show sky through it.
        if (hitT < 0.0 && t <= span.y) hitT = t;
    }

    vec3 col = mix(horizon, zenith, pow(clamp(rd.y + 0.05, 0.0, 1.0), 0.4));
    col += vec3(0.8, 0.85, 1.0) * terraStars(rd, 0.004) * smoothstep(0.05, 0.3, rd.y);
    // The crater's glow on the haze above it.
    vec3 toTop = normalize(vec3(0.0, 1.9, 0.0) - ro);
    col += lava * pow(max(dot(rd, toTop), 0.0), 60.0) * (0.25 + 0.15 * bass);

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = TERRA_NORMAL(height, p.xz, 0.003 * hitT + 0.008);
        float r = length(p.xz);
        float dif = max(dot(n, moon), 0.0);
        vec3 rock = mix(vec3(0.07, 0.06, 0.07), pal(hue + 0.7) * 0.06, 0.4);
        float rim = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
        col = rock * (dif * vec3(0.9, 1.0, 1.3) * 1.6 + 0.2) * terraStepAO(used);
        col += horizon * rim * 0.35;
        // Crater light falls on the upper flanks and the bowl.
        float up = smoothstep(0.4, 1.9, p.y);
        col += lava * rock * up * (3.0 + 1.5 * bass) * (0.5 + 0.5 * n.y);
        float inBowl = (1.0 - smoothstep(0.3, 0.6, r)) * (1.0 - smoothstep(1.7, 2.1, p.y));
        col += lava * inBowl * (0.6 + 0.3 * bass + 0.4 * kick);
        float v = vein(p.xz, 0.035 + 0.03 * kick, t0);
        float heat = 0.35 + 0.9 * kick + 0.2 * bass;
        col += mix(lava, vec3(1.0, 0.8, 0.4), v * 0.5) * v * heat;
        col = mix(col, horizon * 0.6, 1.0 - exp(-hitT * 0.04));
    }
    // Ash haze hugging the plain.
    col += horizon * 0.25 * exp(-max(rd.y + 0.1, 0.0) * 8.0);

    col *= 0.84 + 0.18 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
