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

// Terrain Mesa: high flight over desert table-lands. The height is lib_terra's
// fbm QUANTISED into four tiers: each tier is a flat top, and the step to the
// next is a short steep cliff (a smoothstep over the last third of the step,
// so it is continuous and the march can follow it). A fine noise added before
// quantising frays the cliff lines into buttes and promontories. The cliff
// faces carry strata - horizontal bands keyed to world height, so a layer
// lines up across every mesa as real bedding does - and the tops are darker
// varnished caprock under a hard, high sun.
//
// The cliffs are the steepest ground in the family, so the march steps short
// (k 0.2); rays from this high mostly land on flat tops in a few steps.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (sndShaped(SND_LOW) with sndSpectrum(0.23)) RAISES THE TABLES: each tier stands
// taller on low notes, the upper ones most, so the skyline lifts and settles
// with the bass. The rise is clamped below the eye. The mid band
// (SND_MID_PRESENCE) slides the strata's colours through the palette. Brightness
// follows (0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)) only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the Chaos drift. Complexity (uMorph) fades in an
// octave of fraying along the cliff lines. Chaos (uWarp, uTurbulence,
// uTwist) bends the table-lands (terraChaos). Energy (uIntensity) scales the
// rise and the strata's slide. Colour is pal() in the strata and sky. Space is
// the rig.

#define MS_MAX_STEPS 128
#define MS_FAR 44.0
#define MS_EYE 4.0
#define MS_TIERS 4.0
#define MS_AMP 2.2

float gOct;
float gRise;

float height(vec2 xz) {
    float f = terraFbm(terraChaos(terraWrap(xz)), gOct);
    float s = clamp((f - 0.3) / 0.42, 0.0, 1.0) * MS_TIERS;
    float tier = (floor(s) + smoothstep(0.62, 1.0, fract(s))) / MS_TIERS;
    return MS_AMP * tier * (1.0 + gRise * tier) + 0.15 * f;
}

void main() {
    vec2 uv = view();
    float swell = clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    float energy = terraEnergy();
    float bass = clamp(0.5 * sndShaped(SND_LOW) + 0.5 * sndSpectrum(0.23), 0.0, 1.2);
    gRise = 0.16 * bass * energy;
    gOct = terraOctaves(4.0);
    float hMax = MS_AMP * (1.0 + gRise) + 0.15;
    float hue = 0.25 * clamp(SND_MID_PRESENCE, 0.0, 1.0) * min(energy, 1.5);

    vec3 ro;
    vec3 rd;
    terraFly(uv, MS_EYE, 0.32, ro, rd);

    vec3 sun = normalize(vec3(0.7, 0.55, 0.35));
    vec3 sunCol = vec3(1.0, 0.92, 0.78);
    vec3 zenith = mix(vec3(0.22, 0.42, 0.72), pal(0.6) * 0.5, 0.2);
    vec3 horizon = mix(vec3(0.88, 0.78, 0.64), pal(0.08), 0.15);

    float hitT = -1.0;
    float used = 0.0;
    vec2 span = terraSlab(ro, rd, hMax, MS_FAR);
    if (span.x >= 0.0) {
        float t = span.x;
        float tPrev = t;
        float dyPrev = 1.0;
        for (int i = 0; i < MS_MAX_STEPS; i++) {
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
            t += min(terraStep(dy, t, 0.2), 0.8 + 0.04 * t);
        }
        // Out of budget short of the far end: a grazing ray creeping along a
        // slope. Shade the ground where it got to rather than show sky through it.
        if (hitT < 0.0 && t <= span.y) hitT = t;
    }

    vec3 col = terraSky(rd, sun, zenith, horizon, sunCol);
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = TERRA_NORMAL(height, p.xz, 0.003 * hitT + 0.008);
        float dif = max(dot(n, sun), 0.0);
        float sky = 0.5 + 0.5 * n.y;
        // Strata keyed to world height, wobbled a little so they are not ruled lines.
        float wob = terraNoise(terraWrap(p.xz) * (TERRA_FREQ * 4.0));
        float band = p.y * 5.5 + 0.6 * wob;
        float stripe = 0.5 + 0.5 * sin(band * 3.1);
        vec3 face = mix(vec3(0.74, 0.36, 0.20), pal(0.05 + hue + 0.08 * floor(band)), 0.15);
        face = mix(face, vec3(0.92, 0.80, 0.62), 0.35 * stripe);
        vec3 cap = mix(vec3(0.45, 0.28, 0.18), pal(0.02 + hue) * 0.5, 0.1);
        vec3 base = mix(face, cap, smoothstep(0.8, 0.95, n.y));
        col = base * (dif * sunCol * 1.1 + sky * zenith * 0.45) * terraStepAO(used);
        col = terraFog(col, rd, hitT, 0.03, horizon, sun, sunCol);
    }

    col *= 0.84 + 0.18 * swell;
    col *= 0.8 + 0.2 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
