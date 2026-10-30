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

// Outrun Drive: a night drive down a straight road across a neon wireframe
// floor toward a striped setting sun, with two ranges of mountain silhouettes
// on the horizon and edge lights streaming past. Flown with camFly; the road
// runs down +z and the eye is clamped above the floor.
//
// The floor is a heightfield: flat under the road, rising into hills off to
// the sides. It is not distance-marched. Hills only ever rise, so the hit lies
// between the plane over the tallest hill and the flat floor plane (or, for a
// rising ray, between the eye and that top plane), and a fixed 24-sample
// linear search across the window plus a secant step finds it. It ignores
// uSteps: 24 samples is under the Detail floor, so it is not a marched style.
// The grid is drawn in uniform control flow so fwidth() can antialias it.
// Every z term has a period dividing 64 (hills, lights, dashes, the 2-unit
// grid), so the eye is rebased into one 1024 tile and the 8192 wrap is
// invisible.
//
// ---- audio ------------------------------------------------------------------
//
//   kick (uHits.x)          a glow wave rolls along the grid toward the horizon
//   snare (uHits.y)         the sun's stripes slide, and a few rows of the sun
//                           tear sideways (rows picked per hit by uHitCount)
//   hat (uHits.z)           stars twinkle; the road lights stretch into streaks
//   drop (uEvents.y)        palette swap (grid and sky trade hue sets) and a
//                           bigger sun
//   section (uSectionCount) a new mountain layout, morphed over uEvents.z; the
//                           palette set also alternates per section
//   low bands (uBandsLo.xy with uBassSmooth)  bend the side hills
//   mid (uBandsLo.w)        mountain rim glow
// Road light speed follows uFlowPhase (loudness sets the rate); level, uSwell.

#define OR_FLOOR -2.2
#define OR_FAR 90.0
/** Tallest a hill can be (hills() at full bend), plus margin. */
#define OR_HMAX 5.0
#define OR_ROAD 2.4
#define OR_ZWRAP 1024.0
#define OR_SEARCH 24
#define OR_TAU 6.2831853
/** 2pi / 64: z wavenumber unit, so the hills tile over the 1024 rebase. */
#define OR_KZ 0.09817477

float gBend;

/** Height of the floor above OR_FLOOR: zero on and beside the road. */
float hills(vec2 xz) {
    float side = smoothstep(4.0, 16.0, abs(xz.x));
    float w = 0.5 + 0.25 * sin(xz.x * 0.31 + 1.7) + 0.25 * sin(xz.y * (3.0 * OR_KZ) + xz.x * 0.13);
    float w2 = 0.5 + 0.5 * sin(xz.y * (7.0 * OR_KZ) - xz.x * 0.27);
    return side * (1.0 + 3.6 * gBend) * (0.65 * w + 0.35 * w2);
}

/** 1D value noise, piecewise linear (jagged peaks), wrapping every `period` cells. */
float ridge1(float x, float period, float seed) {
    float i = floor(x);
    float a = motionHash(vec2(mod(i, period), seed));
    float b = motionHash(vec2(mod(i + 1.0, period), seed));
    return mix(a, b, fract(x));
}

/** Mountain top elevation (tan of the angle) at azimuth `az` for one layout. */
float mountain(float az, float seed) {
    float x = az / OR_TAU * 20.0;
    float m = ridge1(x, 20.0, seed);
    return 0.012 + 0.11 * m * m + 0.035 * ridge1(x * 3.0, 60.0, seed + 11.0);
}

void main() {
    vec2 uv = view();

    float swell = clamp(uSwell, 0.0, 1.5);
    float kick = clamp(uHits.x, 0.0, 1.0);
    float snare = clamp(uHits.y, 0.0, 1.0);
    float hats = clamp(uHits.z, 0.0, 1.0);
    float drop = clamp(uEvents.y, 0.0, 1.0);
    float secEnv = clamp(uEvents.z, 0.0, 1.0);
    float mid = clamp(uBandsLo.w, 0.0, 1.0);
    gBend = clamp(0.5 * uBassSmooth + 0.3 * uBandsLo.x + 0.2 * uBandsLo.y, 0.0, 1.0);

    // ---- palette set ----
    // The section's parity picks a set, cross-faded over the section envelope
    // (it starts at the previous section's value); a drop flips it for as
    // long as it holds. 0: grid on hue 0, sky opposite; 1: they trade roles.
    float sec = uSectionCount;
    float base = mix(mod(sec - 1.0, 2.0), mod(sec, 2.0), 1.0 - secEnv);
    float swap = abs(base - smoothstep(0.05, 0.3, drop));
    float gridHue = mix(0.0, 0.30, swap);
    float skyHue = mix(0.5, 0.75, swap);
    vec3 gridCol = pal(gridHue);
    vec3 horizonCol = pal(skyHue) * 0.32;
    vec3 zenithCol = pal(skyHue + 0.3) * 0.02 + vec3(0.003, 0.002, 0.010);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, OR_ZWRAP);
    ro.y = max(ro.y, OR_FLOOR + hills(ro.xz) + 0.45);

    // ---- floor hit ----
    // Search window: a falling ray from the hill-top plane down to the flat
    // floor; a rising one (the hills can tower over the eye) from the eye up
    // to the hill-top plane.
    float rdy = min(rd.y, -1e-4);
    float tFlat = (ro.y - OR_FLOOR) / -rdy;
    float tTop = 0.0;
    float tEnd = 0.0;
    if (rd.y < -1e-4) {
        tTop = max((ro.y - OR_FLOOR - OR_HMAX) / -rd.y, 0.0);
        tEnd = min(tFlat, OR_FAR);
    } else {
        tEnd = min((OR_FLOOR + OR_HMAX - ro.y) / max(rd.y, 1e-4), OR_FAR);
    }
    float hitT = -1.0;
    if (tTop < tEnd) {
        float dt = (tEnd - tTop) / float(OR_SEARCH);
        float tPrev = tTop;
        float dPrev = ro.y + rd.y * tTop - (OR_FLOOR + hills(ro.xz + rd.xz * tTop));
        for (int i = 1; i <= OR_SEARCH; i++) {
            float t = tTop + dt * float(i);
            float d = ro.y + rd.y * t - (OR_FLOOR + hills(ro.xz + rd.xz * t));
            if (hitT < 0.0 && d <= 0.0) {
                hitT = mix(tPrev, t, clamp(dPrev / max(dPrev - d, 1e-5), 0.0, 1.0));
            }
            tPrev = t;
            dPrev = d;
        }
    }
    // Uniform control flow from here to fwidth(): every pixel has a floor
    // point, real or not, so the derivatives exist everywhere.
    float tUse = hitT > 0.0 ? hitT : min(tFlat, OR_FAR);
    vec2 fp = ro.xz + rd.xz * tUse;
    vec2 gc = fp * 0.5;
    vec2 fw = max(fwidth(gc), vec2(1e-4));

    vec3 col;
    if (hitT > 0.0) {
        float fz = fp.y - ro.z;
        float fade = exp(-hitT * 0.018);
        // The kick's wave: a lit band of grid, starting just ahead and rolling
        // out to the horizon as the kick decays.
        float front = 2.0 + 75.0 * (1.0 - kick);
        float wx = (fz - front) * 0.12;
        float wave = kick * exp(-wx * wx);

        vec2 dist = abs(fract(gc + 0.5) - 0.5);
        vec2 lw = fw * (1.2 + 1.5 * wave);
        vec2 l2 = 1.0 - smoothstep(lw * 0.5, lw * 1.5, dist);
        float line = max(l2.x, l2.y);
        // Where a cell shrinks under a pixel the lines would moire: settle on
        // their average coverage instead.
        line = mix(line, 0.22, smoothstep(0.15, 0.45, max(fw.x, fw.y)));

        col = pal(skyHue) * 0.012;
        col += gridCol * line * (0.85 + 2.6 * wave) * (0.25 + 0.75 * fade);

        float ax = abs(fp.x);
        if (ax < OR_ROAD + 0.6) {
            float road = smoothstep(OR_ROAD + 0.05, OR_ROAD - 0.05, ax);
            col *= 1.0 - 0.6 * road;
            float edge = smoothstep(0.12, 0.04, abs(ax - (OR_ROAD - 0.15)));
            float dash = step(0.5, fract(fp.y * 0.25)) * smoothstep(0.1, 0.03, ax);
            col += pal(skyHue) * (edge * 0.9 + dash * 0.6) * fade;
        }

        // Edge lights, one every 4 units each side, streaming toward the eye
        // at the flow rate; hats stretch them along z into streaks.
        float lz = (fract((fp.y + uFlowPhase * 6.0) * 0.25) - 0.5) * 4.0;
        float len = 0.12 + 1.6 * hats;
        float dz = max(abs(lz) - len, 0.0);
        float dx = ax - (OR_ROAD + 0.4);
        float glow = exp(-(dx * dx + dz * dz) * 16.0);
        vec3 lightCol = fp.x > 0.0 ? pal(gridHue + 0.5) : pal(gridHue + 0.15);
        col += (lightCol * 1.2 + 0.3) * glow * exp(-hitT * 0.03);

        col = mix(col, horizonCol, 1.0 - exp(-hitT * 0.022));
    } else if (rd.y < 0.0) {
        col = horizonCol;
    } else {
        float hz = length(rd.xz);
        float e = rd.y / max(hz, 1e-3);
        // atan(0, 0) is undefined (NaN on some drivers) looking straight up.
        float az = hz > 1e-4 ? atan(rd.x, rd.z) : 0.0;
        col = mix(horizonCol, zenithCol, smoothstep(0.0, 0.6, e));

        // Stars; hats deepen and quicken the twinkle.
        vec3 sc = rd * 150.0;
        float s = hashCell(ivec3(floor(sc)));
        float star = step(0.993, s) * smoothstep(0.35, 0.0, length(fract(sc) - 0.5));
        float tw = mix(1.0, 0.5 + 0.5 * sin(uTime * (5.0 + 12.0 * hats) + s * 600.0), 0.25 + 0.75 * hats);
        col += vec3(0.75) * star * tw * (1.0 + 0.8 * hats) * smoothstep(0.08, 0.3, e);

        // ---- sun ----
        float dropBump = 4.0 * drop * (1.0 - drop);
        float sunR = 0.26 * (1.0 + 0.5 * dropBump);
        vec2 sp = vec2(az, e - 0.12) / sunR;
        // Snare glitch: some rows (new ones each hit) tear sideways.
        float gh = motionHash(vec2(floor(sp.y * 10.0), fract(uHitCount * 0.618) * 97.0));
        sp.x += snare * step(0.55, gh) * (gh - 0.75) * 0.7;
        float r = length(sp);
        float disc = smoothstep(1.0, 0.97, r);
        vec3 sunCol = mix(pal(gridHue + 0.9), pal(skyHue + 0.65), smoothstep(-1.0, 1.0, sp.y)) * 1.4 + 0.08;
        // Stripes across the lower half, gaps widening downward; the snare
        // slides them half a stripe and they settle back as it decays.
        float band = fract(sp.y * 5.0 + uTime * 0.12 + snare * 0.5);
        float gap = mix(0.0, 0.5, smoothstep(0.25, -1.0, sp.y));
        float cut = step(sp.y, 0.25) * (1.0 - smoothstep(gap - 0.04, gap, band));
        col = mix(col, sunCol, disc * (1.0 - cut));
        col += pal(gridHue + 0.9) * 0.3 * exp(-max(r - 1.0, 0.0) * 2.5) * (1.0 - disc);

        // ---- mountains ----
        // Two layouts per layer, the previous section's and this one's,
        // morphed across the section envelope.
        float sa = motionHash(vec2(sec - 1.0, 7.0)) * 200.0;
        float sb = motionHash(vec2(sec, 7.0)) * 200.0;
        float k = 1.0 - secEnv;
        float hb = mix(mountain(az, sa), mountain(az, sb), k) + 0.02;
        float hf = 0.6 * mix(mountain(az + 1.3, sa + 5.0), mountain(az + 1.3, sb + 5.0), k);
        float rim = 0.6 + 0.9 * mid;
        if (e < hb) col = mix(pal(skyHue + 0.15) * 0.05, col, 0.15);
        col += pal(skyHue + 0.1) * exp(-abs(e - hb) * 220.0) * 0.5 * rim;
        if (e < hf) col = pal(skyHue) * 0.015;
        col += gridCol * exp(-abs(e - hf) * 260.0) * 0.7 * rim;

        col += horizonCol * 0.6 * exp(-e * 25.0);
    }

    col *= 0.8 + 0.25 * swell;
    col *= 0.75 + 0.25 * smoothstep(2.2, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
