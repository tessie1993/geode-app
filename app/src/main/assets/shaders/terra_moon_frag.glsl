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

// Terrain Moon: skimming a grey cratered moonscape under a black, starry sky,
// with the Earth hanging low ahead. The ground is a gentle fbm with craters
// laid on it at three sizes. A crater is a bowl inside its radius and a
// raised rim just outside it, and each lives wholly inside one cell of a
// grid (its radius and its offset from the cell's centre are bounded so its
// outer slope reaches zero before the cell's edge), so one lookup per size
// finds it and the field is continuous across every cell border. The grids
// are integer multiples of lib_terra's lattice, so they tile with it. Light
// is the raw sun from low on one side: hard terminator shading, no air, no
// haze. The Earth is drawn in the sky as a lit sphere - oceans, land, cloud,
// a thin blue limb - with its night side dark.
//
// ---- audio ------------------------------------------------------------------
//
// Onsets DROP NEW CRATERS. Every onset bumps SND_ONSET_COUNT, and a fresh set of
// cells, hashed from the count, gets a crater: it sinks in over the onset's
// envelope (SND_ONSET_ENV) with a glowing ring of ejecta spreading from its rim,
// while the craters from three onsets ago fill back in at the same rate.
// Three generations are alive at once and every change eases, so the ground
// is reshaped by the rhythm without a single crater popping. Bass
// (SND_BASS_PRESENCE) turns the Earth's clouds faster. Brightness follows SND_LEVEL
// only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the Earth's clouds and its slow rise. Complexity
// (uMorph) fades in an octave of ground. Chaos (uWarp, uTurbulence, uTwist)
// bends the ground and its craters (terraChaos). Energy (uIntensity) scales
// the ejecta's glow. Colour is pal() in the regolith and the Earth. Space is
// the rig.

#define MN_MAX_STEPS 112
#define MN_FAR 36.0
#define MN_EYE 2.0
/** Fbm plus every rim that could stack in one spot, so a true bound for terraSlab. */
#define MN_HMAX 1.25

float gOct;
vec3 gGen;
vec3 gWeight;

/**
 * The crater of one cell, at grid `scale` (an integer multiple of the
 * octave-0 lattice) and seed: (height in world units, distance from its
 * centre in radii). `chance` is the share of cells that hold one.
 */
vec2 crater(vec2 xz, float scale, vec2 seed, float chance) {
    vec2 q = xz * (TERRA_FREQ * scale);
    vec2 c = floor(q) + seed;
    if (terraHash(c) > chance) return vec2(0.0, 9.0);
    float rr = 0.12 + 0.1 * terraHash(c + vec2(37.0, 11.0));
    float room = 0.5 - 1.8 * rr;
    vec2 ctr = 0.5 + (vec2(terraHash(c + vec2(5.0, 71.0)), terraHash(c + vec2(83.0, 29.0))) - 0.5) * 2.0 * room;
    float d = length(fract(q) - ctr) / rr;
    float prof = min(d * d - 1.0, 0.0) * 0.9 + 0.35 * exp(-(d - 1.0) * (d - 1.0) * 10.0);
    prof *= (1.0 - smoothstep(1.4, 1.8, d));
    return vec2(prof * rr * 2.0 / scale, d);
}

float height(vec2 xz) {
    xz = terraChaos(terraWrap(xz));
    float h = 0.45 * terraFbm(xz, gOct);
    h += crater(xz, 1.0, vec2(0.0), 0.4).x;
    h += crater(xz, 2.0, vec2(3.0, 9.0), 0.5).x;
    h += crater(xz, 4.0, vec2(7.0, 2.0), 0.55).x;
    // The onset craters, on a grid offset by half a cell from the big one.
    vec2 o = xz + 2.0;
    h += gWeight.x * crater(o, 1.0, vec2(gGen.x * 17.0, gGen.x * 31.0), 0.12).x;
    h += gWeight.y * crater(o, 1.0, vec2(gGen.y * 17.0, gGen.y * 31.0), 0.12).x;
    h += gWeight.z * crater(o, 1.0, vec2(gGen.z * 17.0, gGen.z * 31.0), 0.12).x;
    return h;
}

void main() {
    vec2 uv = view();
    float t0 = terraTime();
    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    float energy = terraEnergy();
    float onset = clamp(SND_ONSET_ENV, 0.0, 1.0);
    float n0 = floor(SND_ONSET_COUNT);
    gGen = mod(vec3(n0, n0 - 1.0, n0 - 2.0), 64.0);
    // Newest sinks in as the envelope falls, the oldest fills as it does.
    gWeight = vec3(1.0 - onset, 1.0, onset);
    gOct = terraOctaves(3.0);

    vec3 ro;
    vec3 rd;
    terraFly(uv, MN_EYE, 0.22, ro, rd);

    vec3 sun = normalize(vec3(-0.85, 0.28, 0.3));
    vec3 col = vec3(0.0);
    col += vec3(0.9, 0.92, 1.0) * terraStars(rd, 0.007);

    // The Earth: a lit sphere drawn on the sky, rising very slowly.
    vec3 earthDir = normalize(vec3(-0.09, 0.2 + 0.03 * sin(t0 * 0.004), 1.0));
    float er = 0.085;
    vec3 ex = normalize(cross(vec3(0.0, 1.0, 0.0), earthDir));
    vec3 ey = cross(earthDir, ex);
    vec2 e = vec2(dot(rd, ex), dot(rd, ey)) / er;
    float e2 = dot(e, e);
    if (e2 < 1.0 && dot(rd, earthDir) > 0.0) {
        vec3 en = normalize(e.x * ex + e.y * ey - sqrt(1.0 - e2) * earthDir);
        float lit = smoothstep(-0.05, 0.25, dot(en, sun));
        vec2 sp = e * 2.5;
        float land = smoothstep(0.52, 0.58, terraNoise(sp + vec2(t0 * 0.01, 3.0)));
        float cloud = smoothstep(0.5, 0.8, terraNoise(sp * 2.0 + vec2(t0 * (0.02 + 0.03 * SND_BASS_PRESENCE), 9.0)));
        vec3 surf = mix(mix(vec3(0.05, 0.18, 0.45), pal(0.6) * 0.5, 0.2), vec3(0.25, 0.38, 0.18), land);
        surf = mix(surf, vec3(0.95), cloud * 0.8);
        float limb = pow(e2, 3.0);
        col = surf * lit + vec3(0.3, 0.55, 1.0) * limb * lit * 0.6;
    }

    float hitT = -1.0;
    float used = 0.0;
    vec2 span = terraSlab(ro, rd, MN_HMAX, MN_FAR);
    if (span.x >= 0.0) {
        float t = span.x;
        float tPrev = t;
        float dyPrev = 1.0;
        for (int i = 0; i < MN_MAX_STEPS; i++) {
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

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = TERRA_NORMAL(height, p.xz, 0.003 * hitT + 0.008);
        vec2 w = terraChaos(terraWrap(p.xz));
        float dif = max(dot(n, sun), 0.0);
        float albedo = 0.32 + 0.12 * terraNoise(w * 3.0);
        vec3 grey = mix(vec3(albedo), pal(0.6) * albedo, 0.08);
        col = grey * dif * 1.6 + grey * vec3(0.3, 0.4, 0.6) * 0.04 * (0.5 + 0.5 * n.y);
        col *= terraStepAO(used);
        // Ejecta: a bright ring spreading from the newest craters' rims.
        float d = crater(w + 2.0, 1.0, vec2(gGen.x * 17.0, gGen.x * 31.0), 0.12).y;
        float ringR = 1.0 + 0.6 * (1.0 - onset);
        float ring = exp(-pow((d - ringR) * 7.0, 2.0)) * (1.0 - smoothstep(1.55, 1.8, d)) * onset;
        col += mix(vec3(1.0, 0.85, 0.6), pal(0.08), 0.3) * ring * 0.7 * min(energy, 1.5);
        col *= exp(-hitT * 0.025);
    }

    col *= 0.85 + 0.17 * swell;
    fragColor = vec4(grade(col), 1.0);
}
