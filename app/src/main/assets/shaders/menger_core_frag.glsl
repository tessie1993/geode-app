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

// Menger Core: a flight down the central bore of an infinite Menger sponge.
// The world is solid stone with a square cross of holes cut through every
// cell, then again at a third of the size, and again, five levels deep. The
// level-0 cells are 16 world units (a power of two, so the rig's 8192 travel
// wrap is invisible), and the level-0 hole along z through the cell at the
// origin is the corridor the rig flies down. Every hole rim is an emissive
// seam: the map tracks the two nearest hole surfaces, and where they meet
// (an edge) the stone glows.
//
// The eye cannot sit in stone: a tube around the z axis and a sphere around
// the eye are carved out of everything, so a wide camera orbit or a drop
// zoom only ever opens a cavity.
//
// ---- audio: one sound, one property -----------------------------------------
//
// Kick (uHits.x)       the holes grow: every level's hole ratio swells, so the
//                      sponge breathes open and the walls thin.
// Snare (uHits.y)      the seams flash to a contrasting hue, and the inner
//                      levels twist (each bar's cross-section turns, odd and
//                      even levels in opposite directions).
// Hat (uHits.z)        fine glints sparkle along the edges, hashed points
//                      that twinkle only where a seam is.
// Onset (uHits.w)      the inner levels slide inside their cells to a new
//                      hashed offset, gliding from the old one over the onset
//                      envelope: the sponge re-folds a little on every hit.
// Drop (uEvents.y)     a plunge one level deeper: the world magnifies about
//                      the eye (the sponge is self-similar by 3), a fifth level
//                      of holes grows in, and the palette swings round.
// Pitch (uEvents.w)    rotates the stone's hue, as a hump so it eases out and
//                      back rather than snapping.
// Sub/bass (uBandsLo)  fog density: low end closes the far corridor in.
// Section count        picks the fold variant (square, diamond, bored-round
//                      or skewed holes, hole ratio, a held offset and hue),
//                      crossfaded from the old section over its envelope.
// uSwell               overall level, as everywhere.

#define MC_MAX_STEPS 112
/** World to fractal space. Level 0 has period 2 in fractal space, so 16 in the world. */
#define MC_WS 0.125
#define MC_ZWRAP 16.0
#define MC_FAR 36.0
/** Carved tube around the flight axis, world units; inside the level-0 bore (half-width 2.67). */
#define MC_CORE 1.6
#define MC_BUBBLE 0.85
#define MC_TAU 6.2831853

float gHole0;
float gHoleIn;
float gDeep;
float gRound;
float gZoom;
vec3 gOff;
vec3 gEye;
mat2 gRotA;
mat2 gRotB;
/** Set by map(): distance from the nearest hole surface to the second nearest, world units. Small = an edge. */
float gEdge;
/** Set by map(): which level's hole the surface belongs to. */
float gLev;

/**
 * One bar of a level's cross: an infinite square (or rounded) prism along the
 * axis the two given coordinates are perpendicular to. Pushes the hole value
 * into the running top two, plus a corner candidate so a square bar's own
 * long edges read as seams too.
 */
void barPush(vec2 a, float h, float invS, float l, inout float d, inout float d2, inout float lev) {
    vec2 b = abs(a);
    float v = (h - mix(max(b.x, b.y), length(a), gRound)) * invS;
    // Near 0 only on this bar's face AND near its corner; rounded bars have none.
    float corner = min((min(b.x, b.y) - h) * invS, -abs(v)) - gRound * 0.6 * invS;
    d2 = max(d2, max(corner, min(v, d)));
    lev = v > d ? l : lev;
    d = max(d, v);
}

void levelCross(vec3 q, float h, float invS, mat2 r, float l, inout float d, inout float d2, inout float lev) {
    barPush(r * q.xy, h, invS, l, d, d2, lev);
    barPush(r * q.yz, h, invS, l, d, d2, lev);
    barPush(r * q.zx, h, invS, l, d, d2, lev);
}

float map(vec3 w) {
    // The drop zoom is a scale about the eye. Rebasing the eye by 16 shifts
    // this point by exactly 16 too, so the zoom keeps the wrap invisible.
    vec3 f = gEye + (w - gEye) * gZoom;
    vec3 p = f * MC_WS;
    float d = -1e4;
    float d2 = -1e4;
    float lev = 0.0;
    // Level 0: the lattice of bores, untwisted and unshifted so the corridor holds.
    levelCross(mod(p + 1.0, 2.0) - 1.0, gHole0, 1.0, mat2(1.0), 0.0, d, d2, lev);
    float s = 1.0;
    for (int l = 1; l < 4; l++) {
        s *= 3.0;
        vec3 q = mod(p * s + 1.0 + gOff, 2.0) - 1.0;
        levelCross(q, gHoleIn, 1.0 / s, l == 2 ? gRotB : gRotA, float(l), d, d2, lev);
    }
    // The fifth level exists only while a drop grows it in: at zero width its
    // bars would still mark spurious edge dots where they pierce the walls.
    if (gDeep > 0.001) {
        vec3 q = mod(p * 81.0 + 1.0 + gOff, 2.0) - 1.0;
        levelCross(q, gHoleIn * gDeep, 1.0 / 81.0, gRotB, 4.0, d, d2, lev);
    }
    float inv = 1.0 / (MC_WS * gZoom);
    gEdge = (d - d2) * inv;
    gLev = lev;
    float dw = d * inv;
    dw = max(dw, MC_CORE - length(w.xy));
    dw = max(dw, MC_BUBBLE - length(w - gEye));
    return dw;
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** Section variant: x hole ratio, y roundness, z bar angle, w hue offset. */
vec4 mcVariant(float n) {
    uint h = uhash(uint(n) + 0x51u);
    uint pick = h % 4u;
    vec4 v = vec4(0.333, 0.0, 0.0, 0.0);
    if (pick == 1u) v = vec4(0.30, 0.0, 0.7853982, 0.12);
    if (pick == 2u) v = vec4(0.36, 1.0, 0.0, -0.10);
    if (pick == 3u) v = vec4(0.27, 0.0, 0.3926991, 0.22);
    v.x += 0.03 * (uhashUnit(uhash(h)) - 0.5);
    return v;
}

/** A held per-section offset of the inner levels, in cell fractions. */
vec3 mcSectionOffset(float n) {
    uint h = uhash(uint(n) + 0x7a3u);
    return (vec3(uhashUnit(h), uhashUnit(uhash(h)), uhashUnit(uhash(h ^ 0x9e37u))) - 0.5) * 0.3;
}

vec3 mcOnsetOffset(float n) {
    uint h = uhash(uint(n) + 0x2c1u);
    return vec3(uhashUnit(h), uhashUnit(uhash(h)), uhashUnit(uhash(h ^ 0x85ebu))) - 0.5;
}

void main() {
    vec2 uv = view();

    float swell = clamp(uSwell, 0.0, 1.5);
    float kick = clamp(uHits.x, 0.0, 1.0);
    float snare = clamp(uHits.y, 0.0, 1.0);
    float hat = clamp(uHits.z, 0.0, 1.0);
    float drop = clamp(uEvents.y, 0.0, 1.0);
    float pitch = clamp(uEvents.w, 0.0, 1.0);
    float low = clamp(0.6 * uBandsLo.x + 0.4 * uBandsLo.y, 0.0, 1.0);
    // 0 at the instant of the event and at its end: the rise never snaps.
    float dropBump = 4.0 * drop * (1.0 - drop);
    float pitchBump = 4.0 * pitch * (1.0 - pitch);

    // Section crossfade without frame state: uSectionCount steps and
    // uEvents.z jumps to 1 on the same frame, so w starts at the OLD variant.
    float sw = 1.0 - clamp(uEvents.z, 0.0, 1.0);
    sw = sw * sw * (3.0 - 2.0 * sw);
    float prevSect = mod(uSectionCount - 1.0, 4096.0);
    vec4 vr = mix(mcVariant(prevSect), mcVariant(uSectionCount), sw);
    vec3 secOff = mix(mcSectionOffset(prevSect), mcSectionOffset(uSectionCount), sw);

    // Onset glide: the count steps as the envelope jumps to >= 0.4, so the
    // mix starts exactly at the previous offset and eases to the new one.
    float og = smoothstep(0.4, 0.05, clamp(uHits.w, 0.0, 1.0));
    vec3 onOff = mix(mcOnsetOffset(mod(uHitCount - 1.0, 4096.0)), mcOnsetOffset(uHitCount), og);

    gHole0 = 0.333 + 0.05 * kick;
    gHoleIn = vr.x * (1.0 + 0.24 * kick);
    gRound = vr.y;
    gOff = secOff + 0.16 * onOff + 0.05 * vec3(sin(uTime * 0.07), cos(uTime * 0.05), sin(uTime * 0.04 + 1.3));
    float twist = 0.38 * snare + 0.06 * sin(uTime * 0.13);
    gRotA = rot2(vr.z + twist);
    gRotB = rot2(vr.z - twist);
    gZoom = mix(1.0, 0.4, dropBump);
    gDeep = smoothstep(0.1, 0.6, dropBump);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every term tiles with a period dividing 16 along z, so this shift is
    // exact and invisible, and keeps highp z precise far down the travel.
    ro.z = mod(ro.z, MC_ZWRAP);
    gEye = ro;

    float t = 0.03;
    float hitT = -1.0;
    float used = 0.0;
    float edge = 1.0;
    float lev = 0.0;
    float glow = 0.0;
    for (int i = 0; i < MC_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (t > MC_FAR) break;
        used = float(i);
        float d = map(ro + rd * t);
        float eps = 0.0008 * t + 0.0005;
        if (d < eps) {
            hitT = t;
            edge = gEdge;
            lev = gLev;
            break;
        }
        // Light bleeding off seams the ray passes close to.
        glow += exp(-gEdge * 10.0) * exp(-d * 6.0) * 0.006;
        t += max(d * 0.8, eps);
    }

    float hue = 0.58 + 0.03 * sin(uTime * 0.021) + vr.w + 0.30 * pitchBump + 0.5 * dropBump;
    vec3 seamCol = mix(pal(hue + 0.5), pal(hue + 0.17) * 1.3, snare);

    float fogK = 0.035 + 0.10 * low;
    float ahead = pow(max(rd.z, 0.0), 10.0);
    vec3 fogCol = pal(hue + 0.35) * (0.04 + 0.20 * ahead) + vec3(0.003, 0.003, 0.006);
    vec3 col = fogCol;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.0012 * hitT));
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.3, 0.2, 1.0);
        float diff = max(dot(n, -rd), 0.0);
        float spec = pow(max(dot(reflect(rd, n), -rd), 0.0), 32.0);
        float fres = pow(1.0 - diff, 3.0);
        // Dark stone, its tone stepping with the level whose hole it lines.
        vec3 stone = pal(hue + 0.07 * lev) * (0.05 + 0.22 * diff) * ao
                   + pal(hue + 0.25) * fres * 0.10 * ao + vec3(spec) * 0.18 * ao;

        // Seams: a neon line on every edge, antialiased to the pixel footprint
        // and dimmed when thinner than a pixel so distance does not wash out.
        float px = hitT * 2.0 / (max(uCamFocal, 0.1) * uResolution.y);
        float wl = 0.03;
        float line = 1.0 - smoothstep(wl, wl + 1.5 * px, edge);
        line *= min(1.0, 2.0 * wl / (wl + px));
        float halo = exp(-edge * 14.0) * 0.22;
        vec3 seam = seamCol * (line * (0.9 + 1.3 * snare) + halo) * (0.75 + 0.35 * swell);

        // Hat glints: sparse hashed points, only on seams. z wrapped like the
        // world (16 * 5 is whole) so the eye's rebase never re-hashes them.
        vec3 f = gEye + (p - gEye) * gZoom;
        vec3 gp = vec3(f.xy, mod(f.z, MC_ZWRAP)) * 5.0;
        float g = hashCell(ivec3(floor(gp)));
        float tw = 0.5 + 0.5 * sin(uTime * (8.0 + 14.0 * g) + g * 70.0);
        float pt = smoothstep(0.35, 0.0, length(fract(gp) - 0.5));
        float onEdge = 1.0 - smoothstep(0.0, 0.14, edge);
        vec3 glint = mix(vec3(1.0), seamCol, 0.3) * step(0.86 - 0.14 * hat, g) * pt * tw * onEdge
                   * (0.08 + 1.5 * hat);

        col = stone + seam + glint;
        col = mix(col, fogCol, 1.0 - exp(-hitT * fogK));
    }
    col += seamCol * glow * (0.6 + 0.4 * swell);

    col *= 0.85 + 0.2 * swell;
    col *= 0.72 + 0.28 * smoothstep(2.2, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
