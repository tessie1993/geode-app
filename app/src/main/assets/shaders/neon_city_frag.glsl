#version 300 es
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
//#include lib_sdf3

// Neon City: a synthwave flyover. An endless grid of blocks below the eye has
// neon-edged roofs and lit windows, the streets are glowing grid lines, and a
// striped sun sits on a glowing horizon straight down the travel axis.
//
// The camera is the shared rig (camFly). The block hash repeats every 1024
// world units along z, which divides the rig's 8192 wrap.
//
// ---- audio ------------------------------------------------------------------
//
// Each block is assigned one of the eight band regions (uBandsLo, uBandsHi) by
// its hash, and its height follows that band, so the skyline is a spectrum
// laid out at random. A snare (uHits.y) sends a ring of window light outward
// from under the eye through the blocks: it is a moving band, not a flash. A
// drop (uEvents.y) stretches every building and lifts the sun and the horizon
// glow higher in the sky; they move, they do not brighten. Overall level
// rides uSwell.
//
// ---- why the estimate is safe ----------------------------------------------
//
// Each cell measures only its own box. Below the tallest possible roof the
// distance is clamped to the cell boundary, so a taller neighbour is never
// stepped over. Every roof is capped NC_CLEAR below the eye, and a sphere
// around the eye is carved out as well.

#define NC_MAX_STEPS 96
#define NC_FAR 80.0
#define NC_GROUND -9.0
#define NC_CELL 2.0
/** Cells along z before the hash repeats: 1024 world units. */
#define NC_ZWRAP 512.0
/** Window columns along z repeat every 3072, also 1024 world units. */
#define NC_WINWRAP 3072.0
/** Tallest a building can be before the drop stretch. */
#define NC_MAX_HEIGHT 7.1
#define NC_CLEAR 0.9
#define NC_BUBBLE 0.8

float gStretch;
float gCap;
float gHiMax;

float gTop;
float gBand;
float gH;
vec2 gCentre;
vec2 gHalf;
float gMat;
/** The eye, z rebased into 0..1024 (see main). */
vec3 gEye;

/** One of the eight band regions, picked by h in 0..1. */
float bandPick(float h) {
    float k = floor(h * 8.0);
    vec4 lo = vec4(equal(vec4(k), vec4(0.0, 1.0, 2.0, 3.0)));
    vec4 hi = vec4(equal(vec4(k), vec4(4.0, 5.0, 6.0, 7.0)));
    return dot(lo, uBandsLo) + dot(hi, uBandsHi);
}

float map(vec3 p) {
    vec2 cell = floor(p.xz / NC_CELL);
    vec2 centre = (cell + 0.5) * NC_CELL;
    vec2 f = p.xz - centre;
    uint u = uhash3(uvec3(ivec3(int(cell.x), 7, int(mod(cell.y, NC_ZWRAP)))));
    float h = uhashUnit(u);
    uint u2 = uhash(u);
    float hb = uhashUnit(u2);
    float hw = uhashUnit(uhash(u2));

    float band = clamp(bandPick(hb), 0.0, 1.0);
    float tower = step(0.88, h);
    float height = ((0.8 + 3.2 * h) * (0.45 + 0.7 * band) + 2.5 * tower) * gStretch;
    float top = min(NC_GROUND + height, gCap);
    // Footprint at most 0.8 of the 1.0 half-cell: the rest is street.
    vec2 hs = vec2(0.55 + 0.25 * hw, 0.55 + 0.25 * fract(hw * 7.13));
    // The box runs one unit below the ground so there is no seam at its foot.
    float yc = 0.5 * (top + NC_GROUND - 1.0);
    float hy = 0.5 * (top - NC_GROUND + 1.0);
    float db = sdBox(vec3(f.x, p.y - yc, f.y), vec3(hs.x, hy, hs.y));

    vec2 af = abs(f);
    float toEdge = NC_CELL * 0.5 - max(af.x, af.y);
    float d = min(db, max(toEdge + 0.02, p.y - gHiMax));
    float dg = p.y - NC_GROUND;
    gMat = dg < db ? 1.0 : 0.0;
    d = min(d, dg);
    d = max(d, NC_BUBBLE - length(p - gEye));

    gTop = top;
    gBand = band;
    gH = h;
    gCentre = centre;
    gHalf = hs;
    return d;
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** Gradient sky, a glow band at glowY (in rd.y) and the striped sun sitting on it. */
vec3 skyColour(vec3 rd, float glowY) {
    float y = rd.y;
    // The gradient itself stays put, so a drop moves the glow without
    // brightening the sky as a whole.
    vec3 col = mix(pal(0.92) * 0.30, pal(0.68) * 0.035, smoothstep(-0.03, 0.57, y));
    float by = (y - glowY) / 0.13;
    col += pal(0.05) * 0.30 * exp(-by * by);

    vec3 sd = normalize(vec3(0.0, glowY + 0.10, 1.0));
    float r = sqrt(max(2.0 * (1.0 - dot(rd, sd)), 0.0));
    float below = y - sd.y;
    // Gaps widen toward the bottom of the disc and drift slowly down.
    float gap = clamp(-below / 0.19, 0.0, 1.0) * 0.6;
    float stripes = below > 0.0 ? 1.0 : step(gap, fract(below * 26.0 + uTime * 0.25));
    float disc = (1.0 - smoothstep(0.183, 0.19, r)) * stripes;
    col += mix(pal(0.08), pal(0.95), (1.0 - smoothstep(-0.19, 0.19, below))) * disc * 0.85;
    col += pal(0.02) * 0.10 * (1.0 - smoothstep(0.0, 0.7, r));

    vec3 sp = rd * 150.0;
    float s = hashCell(ivec3(floor(sp)));
    float star = (1.0 - smoothstep(0.0, 0.3, length(fract(sp) - 0.5))) * step(0.996, s);
    col += vec3(0.7) * star * smoothstep(glowY + 0.15, glowY + 0.4, y);
    return col;
}

void main() {
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 1024, so this shift is
    // invisible and exact (power of two). Without it, far down the 8192
    // travel a highp z has ~0.001 of fraction left: every sample and the
    // finite-difference normals quantise into speckle.
    ro.z = mod(ro.z, NC_ZWRAP * NC_CELL);
    gEye = ro;

    float swell = clamp(uSwell, 0.0, 1.5);
    float energy = clamp(uEnergySmooth, 0.0, 1.5);
    float drop = clamp(uEvents.y, 0.0, 1.0);
    float snare = clamp(uHits.y, 0.0, 1.0);

    gStretch = 1.0 + 0.55 * drop;
    gCap = uCamPos.y - NC_CLEAR;
    gHiMax = min(NC_GROUND + NC_MAX_HEIGHT * gStretch, gCap);
    float glowY = 0.02 + 0.16 * drop;

    float t = 0.05;
    float hitT = -1.0;
    for (int i = 0; i < NC_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * t;
        // Above every roof and climbing: nothing left to hit.
        if (rd.y >= 0.0 && p.y > gHiMax + 0.1) break;
        float d = map(p);
        if (d < 0.0015 + 0.0008 * t) {
            hitT = t;
            break;
        }
        t += d * 0.95;
        if (t > NC_FAR) break;
    }

    vec3 sky = skyColour(rd, glowY);
    vec3 col = sky;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.001 * hitT));
        map(p);
        float fres = pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 3.0);
        vec3 horizon = pal(0.92) * 0.30;
        float hue = 0.55 + 0.35 * gH;

        if (gMat > 0.5) {
            // Streets: neon grid lines on the cell boundaries.
            vec2 g = abs(fract(p.xz / NC_CELL + 0.5) - 0.5) * NC_CELL;
            float w = 0.03 + 0.0025 * hitT;
            float line = (1.0 - smoothstep(0.0, w, min(g.x, g.y)));
            col = pal(0.70) * 0.02 + pal(0.85) * line * 0.8 + horizon * fres * 0.3;
        } else {
            vec3 L = normalize(vec3(0.3, 0.7, -0.6));
            vec3 facade = pal(hue + 0.6) * (0.035 + 0.05 * max(dot(n, L), 0.0)) + horizon * fres * 0.25;

            // Box edges are where two face distances are both near zero.
            vec3 lp = p - vec3(gCentre.x, 0.0, gCentre.y);
            float ex = gHalf.x - abs(lp.x);
            float ez = gHalf.y - abs(lp.z);
            float et = gTop - p.y;
            float second = max(min(ex, ez), min(max(ex, ez), et));
            float ew = 0.025 + 0.002 * hitT;
            float edge = (1.0 - smoothstep(0.0, ew, second));

            // Windows on the side faces.
            float sideFace = 1.0 - step(0.5, n.y);
            float along = abs(n.x) > abs(n.z) ? p.z : p.x;
            vec2 wc = vec2(along * 3.0, p.y * 2.2);
            vec2 wf = fract(wc);
            float shape = smoothstep(0.18, 0.26, wf.x) * (1.0 - smoothstep(0.74, 0.82, wf.x))
                        * smoothstep(0.25, 0.33, wf.y) * (1.0 - smoothstep(0.77, 0.85, wf.y));
            float wid = hashCell(ivec3(int(mod(floor(wc.x), NC_WINWRAP)), int(floor(wc.y)), int(gH * 4096.0)));
            float lit = step(wid, 0.22 + 0.35 * gBand);
            // A slow sweep keeps the lights moving in silence.
            float dist = length(p.xz - gEye.xz);
            float sweep = 0.5 + 0.5 * sin(dist * 0.35 - uTime * 1.2 + gH * 6.0);
            // The snare ring: it starts under the eye and runs outward as the
            // envelope falls, lighting nearly every window it passes.
            float front = 3.0 + 42.0 * (1.0 - snare);
            float dr = (dist - front) / 3.5;
            float wave = snare * exp(-dr * dr) * step(wid, 0.9);
            vec3 win = pal(hue + 0.12) * lit * (0.45 + 0.45 * sweep)
                     + mix(pal(hue + 0.4), vec3(1.0), 0.35) * wave * 1.3;

            col = facade + win * shape * sideFace + pal(hue) * edge * 1.1;
        }
        col = mix(col, sky, 1.0 - exp(-hitT * 0.028));
    }

    col *= 0.80 + 0.30 * swell + 0.08 * energy;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
