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

// Terrain Ridge: low flight over ridged mountains at dusk. The family's free
// style and its plain form: one ridged-multifractal field (lib_terra), four
// octaves (three on a low Detail budget), one bounded march, one normal. The
// sun sits low ahead so every crest catches a warm rim and the valleys fall
// into violet shade; snow settles where the slope is gentle and the ground
// high. Haze thickens with distance into a sky that runs from ember at the
// horizon to deep dusk overhead.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (uBandsLo.y, steadied with uBassSmooth) LIFTS THE RIDGES: the lift is
// weighted by the height already there, so peaks climb and valleys stay put,
// and the range seems to draw breath on every low note. The lift is clamped,
// and the eye flies above the highest peak it can make. The key (uKeyHue,
// weighted by uKeyStrength) drifts the dusk colours a little. Brightness
// follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the palette drift and the Chaos warp's drift.
// Complexity (uMorph) fades in a fifth octave (terraOctaves). Chaos (uWarp,
// uTurbulence, uTwist) bends the ridges sideways (terraChaos). Energy
// (uIntensity) scales the bass lift. Colour is pal() in the rock, sky and
// haze. Space is the rig (terraFly over camFly).

#define RG_MAX_STEPS 128
#define RG_FAR 42.0
#define RG_EYE 3.1

float gOct;
float gLift;

float height(vec2 xz) {
    float r = terraRidged(terraChaos(terraWrap(xz)), gOct);
    return 1.5 * r * (1.0 + gLift * r);
}

void main() {
    vec2 uv = view();
    float swell = clamp(uSwell, 0.0, 1.5);
    float bass = clamp(0.6 * uBandsLo.y + 0.4 * uBassSmooth, 0.0, 1.2);
    gLift = 0.22 * bass * terraEnergy();
    gOct = terraOctaves(4.0);
    float hMax = 1.5 * (1.0 + gLift);

    vec3 ro;
    vec3 rd;
    terraFly(uv, RG_EYE, 0.22, ro, rd);

    float hue = 0.03 * sin(terraTime() * 0.02) + 0.1 * uKeyStrength * sin(TERRA_TAU * uKeyHue);
    vec3 sun = normalize(vec3(0.35, 0.10, 1.0));
    vec3 sunCol = mix(vec3(1.0, 0.55, 0.25), pal(hue + 0.05), 0.25);
    vec3 zenith = mix(vec3(0.04, 0.03, 0.13), pal(hue + 0.6) * 0.12, 0.3);
    vec3 horizon = mix(vec3(0.85, 0.42, 0.22), pal(hue), 0.25);

    float hitT = -1.0;
    float used = 0.0;
    vec2 span = terraSlab(ro, rd, hMax, RG_FAR);
    if (span.x >= 0.0) {
        float t = span.x;
        float tPrev = t;
        float dyPrev = 1.0;
        for (int i = 0; i < RG_MAX_STEPS; i++) {
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
            t += terraStep(dy, t, 0.45);
        }
        // Out of budget short of the far end: a grazing ray creeping along a
        // slope. Shade the ground where it got to rather than show sky through it.
        if (hitT < 0.0 && t <= span.y) hitT = t;
    }

    vec3 col = terraSky(rd, sun, zenith, horizon, sunCol);
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = TERRA_NORMAL(height, p.xz, 0.004 * hitT + 0.01);
        float h = p.y / hMax;
        float dif = max(dot(n, sun), 0.0);
        float sky = 0.5 + 0.5 * n.y;
        float rim = pow(1.0 - max(dot(n, -rd), 0.0), 4.0) * max(dot(rd, sun), 0.0);
        vec3 rock = mix(vec3(0.16, 0.13, 0.15), pal(hue + 0.55 + 0.25 * h) * 0.25, 0.35);
        float snow = smoothstep(0.55, 0.8, h + 0.25 * (n.y - 0.7)) * smoothstep(0.55, 0.8, n.y);
        vec3 base = mix(rock, vec3(0.85, 0.82, 0.9), snow);
        col = base * dif * sunCol * 1.3;
        col += base * sky * zenith * 1.6;
        col += sunCol * rim * 0.35;
        col *= terraStepAO(used);
        col = terraFog(col, rd, hitT, 0.06, horizon * 0.8, sun, sunCol);
    }

    col *= 0.82 + 0.2 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
