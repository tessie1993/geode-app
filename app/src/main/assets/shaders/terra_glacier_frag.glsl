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

// Terrain Glacier: flying over an ice field broken into terraces and split by
// crevasses, under a pale cold sky with a low white sun. The field is lib_terra's
// fbm, terraced (a smoothstep inside each step, so the steps meet without a
// crack) and mixed most of the way toward the terraced copy, so the ice reads
// as shelves of firn stepping down to the valley. Crevasses are the
// mid-contour of a second, finer noise, cut downward as narrow trenches; they
// glow blue from inside, the colour light takes after travelling through ice.
// The surface is lit with a sharp specular and a strong Fresnel sheen.
//
// Cracks and risers are steep, so the march steps short (k 0.3).
//
// ---- audio ------------------------------------------------------------------
//
// Snare (SND_SNARE_ENV) CRACKS THE ICE: each snare widens the crevasses a little
// (geometry, easing back with the envelope) and strikes glints of light along
// their lips - points of light on the crack edges only, never the frame.
// Bass (sndShaped(SND_LOW)) deepens the blue inside the crevasses. Brightness follows
// SND_INTENSITY only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the glints' shimmer and the Chaos drift. Complexity
// (uMorph) fades in an octave of shelf detail. Chaos (uWarp, uTurbulence,
// uTwist) bends the shelves and cracks (terraChaos). Energy (uIntensity)
// scales the snare's crack and glints. Colour is pal() in the ice's blues and
// the sky. Space is the rig.

#define GC_MAX_STEPS 120
#define GC_FAR 36.0
#define GC_EYE 2.5
#define GC_AMP 1.7
#define GC_SHELVES 6.0

float gOct;
float gCrackW;

/** 1 at the middle of a crevasse, 0 outside it. */
float crack(vec2 xz) {
    float c = terraNoise(xz * (TERRA_FREQ * 3.0) + vec2(41.0, 17.0));
    return 1.0 - smoothstep(0.0, gCrackW, abs(c - 0.5));
}

float height(vec2 xz) {
    xz = terraChaos(terraWrap(xz));
    float f = terraFbm(xz, gOct);
    float s = f * GC_SHELVES;
    float terr = (floor(s) + smoothstep(0.3, 0.7, fract(s))) / GC_SHELVES;
    return GC_AMP * mix(f, terr, 0.75) - 0.35 * crack(xz);
}

void main() {
    vec2 uv = view();
    float t0 = terraTime();
    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float energy = terraEnergy();
    float snare = clamp(SND_SNARE_ENV * energy, 0.0, 1.0);
    float bass = clamp(sndShaped(SND_LOW), 0.0, 1.2);
    gCrackW = 0.03 + 0.02 * snare;
    gOct = terraOctaves(4.0);

    vec3 ro;
    vec3 rd;
    terraFly(uv, GC_EYE, 0.24, ro, rd);

    vec3 sun = normalize(vec3(-0.5, 0.18, 0.85));
    vec3 sunCol = vec3(1.0, 0.97, 0.92);
    vec3 zenith = mix(vec3(0.30, 0.48, 0.70), pal(0.58) * 0.6, 0.2);
    vec3 horizon = mix(vec3(0.80, 0.88, 0.95), pal(0.52), 0.12);

    float hitT = -1.0;
    float used = 0.0;
    vec2 span = terraSlab(ro, rd, GC_AMP, GC_FAR);
    if (span.x >= 0.0) {
        float t = span.x;
        float tPrev = t;
        float dyPrev = 1.0;
        for (int i = 0; i < GC_MAX_STEPS; i++) {
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
            t += terraStep(dy, t, 0.3);
        }
        // Out of budget short of the far end: a grazing ray creeping along a
        // slope. Shade the ground where it got to rather than show sky through it.
        if (hitT < 0.0 && t <= span.y) hitT = t;
    }

    vec3 col = terraSky(rd, sun, zenith, horizon, sunCol);
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = TERRA_NORMAL(height, p.xz, 0.003 * hitT + 0.008);
        vec2 w = terraChaos(terraWrap(p.xz));
        float cr = crack(w);
        float dif = max(dot(n, sun), 0.0);
        float sky = 0.5 + 0.5 * n.y;
        float fres = pow(1.0 - max(dot(n, -rd), 0.0), 5.0);
        float spec = pow(max(dot(reflect(rd, n), sun), 0.0), 40.0);
        vec3 deep = mix(vec3(0.05, 0.30, 0.55), pal(0.6) * 0.6, 0.25) * (1.0 + 0.5 * bass);
        vec3 ice = mix(vec3(0.82, 0.90, 0.98), vec3(0.45, 0.70, 0.90), (1.0 - smoothstep(0.5, 0.9, n.y)));
        ice = mix(ice, deep, cr);
        col = ice * (dif * sunCol * 0.9 + sky * zenith * 0.7) * terraStepAO(used);
        col += deep * cr * 0.25;
        col += sunCol * spec * 0.6 + horizon * fres * 0.35;
        // Snare glints: tiny cells on the crack lips catch the light.
        float lip = smoothstep(0.05, 0.4, cr) * (1.0 - smoothstep(0.6, 0.95, cr));
        float g = terraHash3(vec3(floor(w * 30.0), floor(t0 * 6.0)));
        col += vec3(0.85, 0.95, 1.0) * step(0.9, g) * lip * snare * exp(-hitT * 0.08) * 1.5;
        col = terraFog(col, rd, hitT, 0.04, horizon, sun, sunCol);
    }

    col *= 0.82 + 0.18 * swell;
    col *= 0.8 + 0.2 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
