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

// Terrain Dunes: gliding low over a sea of sand dunes in afternoon sun. The
// dunes are transverse ridges: a long, gentle windward back and a short steep
// slip face, made by one asymmetric wave profile read through a phase that
// runs across the wind and is bent by a soft fbm, so the crests sinuously
// break and rejoin like real ones. The low sun throws the slip faces into
// shade, which is what draws them. Over every surface lie wind ripples, small
// and dense, added to the NORMAL only: they would cost the march dearly and
// add nothing to the silhouette.
//
// The dune phase runs at 48/128 of a cycle per lattice cell along z (an
// integer number of crests per tile) and the ripples at an integer rate, so
// both tile with lib_terra's field (see its header).
//
// ---- audio ------------------------------------------------------------------
//
// Hats (SND_HAT_ENV) SHIMMER THE RIPPLES: each hat deepens the ripple relief and
// scatters glints of mica across the sunlit sand, both fading with the hat's
// envelope - local glitter, never a frame flash. Bass (sndShaped(SND_LOW)) swells
// the dunes a little, slowly. Brightness follows (0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)) only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the ripples' wind drift. Complexity (uMorph) fades in
// an octave of the bending field. Chaos (uWarp, uTurbulence, uTwist) bends
// the crests (terraChaos). Energy (uIntensity) scales the hat shimmer and the
// bass swell. Colour is pal() in the sand and sky. Space is the rig.

#define DN_MAX_STEPS 112
#define DN_FAR 38.0
#define DN_EYE 2.3
/** The dune wave's phase per lattice cell: x free, z a multiple of 1/128 so it tiles. */
const vec2 DN_WAVE = vec2(0.25, 0.375);
/** The ripples' phase per lattice cell: integers, so they tile. */
const vec2 DN_RIPPLE = vec2(4.0, 6.0);

float gOct;
float gSwellAmp;

float height(vec2 xz) {
    xz = terraChaos(terraWrap(xz));
    float f = terraFbm(xz, gOct);
    float u = fract(dot(xz * TERRA_FREQ, DN_WAVE) + 0.4 * f);
    // Windward back up to the crest at 0.78, slip face down to 1: equal (0) at
    // both ends, so the fract leaves no step.
    float w = u < 0.78 ? smoothstep(0.0, 0.78, u) : 1.0 - smoothstep(0.78, 1.0, u);
    return gSwellAmp * (0.85 * w * (0.6 + 0.4 * f) + 0.2 * f);
}

void main() {
    vec2 uv = view();
    float t0 = terraTime();
    float swell = clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    float energy = terraEnergy();
    float hat = clamp(SND_HAT_ENV * energy, 0.0, 1.0);
    gSwellAmp = 1.0 + 0.15 * clamp(sndShaped(SND_LOW), 0.0, 1.2) * energy;
    gOct = terraOctaves(2.0);
    float hMax = 1.05 * gSwellAmp;

    vec3 ro;
    vec3 rd;
    terraFly(uv, DN_EYE, 0.2, ro, rd);

    vec3 sun = normalize(vec3(0.9, 0.3, 0.25));
    vec3 sunCol = vec3(1.0, 0.86, 0.66);
    vec3 zenith = mix(vec3(0.10, 0.28, 0.62), pal(0.6) * 0.5, 0.15);
    vec3 horizon = mix(vec3(0.80, 0.74, 0.62), pal(0.1), 0.15);

    float hitT = -1.0;
    vec2 span = terraSlab(ro, rd, hMax, DN_FAR);
    if (span.x >= 0.0) {
        float t = span.x;
        float tPrev = t;
        float dyPrev = 1.0;
        for (int i = 0; i < DN_MAX_STEPS; i++) {
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
            t += terraStep(dy, t, 0.5);
        }
        // Out of budget short of the far end: a grazing ray creeping along a
        // slope. Shade the ground where it got to rather than show sky through it.
        if (hitT < 0.0 && t <= span.y) hitT = t;
    }

    vec3 col = terraSky(rd, sun, zenith, horizon, sunCol);
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = TERRA_NORMAL(height, p.xz, 0.003 * hitT + 0.01);
        // Ripples: a bump on the normal across DN_RIPPLE, wandering with a
        // fine noise, drifting downwind, fading out before they would alias.
        vec2 q = terraWrap(p.xz) * TERRA_FREQ;
        float ph = TERRA_TAU * (dot(q, DN_RIPPLE) + 0.8 * terraNoise(q * 4.0)) - t0 * 0.6;
        float fade = exp(-hitT * 0.09) * smoothstep(0.2, 0.7, n.y);
        vec2 dir = normalize(DN_RIPPLE);
        n = normalize(n + vec3(dir.x, 0.0, dir.y) * cos(ph) * (0.10 + 0.22 * hat) * fade);

        float dif = max(dot(n, sun), 0.0);
        float sky = 0.5 + 0.5 * n.y;
        vec3 sand = mix(vec3(0.92, 0.58, 0.30), pal(0.08 + 0.1 * (p.y / hMax)), 0.12);
        col = sand * dif * sunCol * 1.4;
        col += sand * sky * zenith * 0.3;
        // Mica: a glint per tiny cell, only on hats and only where the sun lands.
        float g = terraHash3(vec3(floor(q * 120.0), 3.0));
        col += sunCol * step(0.992, g) * hat * dif * fade * 1.4;
        col = terraFog(col, rd, hitT, 0.03, horizon, sun, sunCol);
    }

    col *= 0.84 + 0.18 * swell;
    col *= 0.8 + 0.2 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
