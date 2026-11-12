#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_touch
//#include lib_shell

// Mandala Dome: the tapestry hall, now a real room. A great hemispherical
// dome stands over a round floor, and every surface is the same tapestry of
// eight-point stars, rings and diamonds: filled in purples, blues and
// magentas, outlined in green, cyan and red with the red/green fringe split
// apart. A patterned sphere floats in the middle and lights the hall from
// within; six smaller domes bulge up from the floor around it (the 2D look's
// two side domes, carried all the way round). The floor is a mandala: the
// tapestry laid out in polar rings and mirrored across its axis, as the 2D
// hall was mirrored left/right.
//
// The camera is the shared rig in object-centred mode (camAround): the eye
// circles the centre sphere at two turns per 1024 world units of travel, so
// the rig's 8192 wrap lands on a whole number of turns and never shows. The
// room itself has no z term at all, so there is nothing else to wrap. The
// rig's look, banking and shake ride on top; the eye is held inside the hall
// and above the side domes.
//
// ---- audio ------------------------------------------------------------------
//
// High (sndShaped(SND_HIGH))   the outlines glow, as the 2D style's did;
//                              slew-limited, so a cymbal never whites the hall.
// Kick (SND_KICK_ENV)          the centre sphere swells and the side domes
//                              breathe: geometry, not brightness.
// Drop (SND_HELD_SURGE)        the sphere's inner light floods the hall, as a
//                              hump, so it rises and settles.
// Bass (SND_BASS_ATT)          the halo around the sphere widens.
// Treble (SND_TREBLE_PRESENCE) the dust shells around the sphere sparkle.
// Spin                         time alone, never the sound (owner 2026-11-12,
//                              no style spins to the music); the sphere and the
//                              side domes counter-turn, the floor creeps.
// Touch                        the finger lights the centre sphere, as before.
// SND_ENERGY_TREND             overall level.
// Knobs: k0 PULSE the kick swell, k1 TEXTURE the fringe split, k3 TONE where
// the centre sphere floats (a high melody lifts it up toward the dome, a low
// one lowers it toward the floor's mandala; its light and halo go with it;
// and a small hue offset), k4 BUILD the inner light, k5 SPACE the orbit radius.
//
// ---- why the estimate is safe ----------------------------------------------
//
// Every piece is an exact Euclidean distance: the sphere, the hall shell
// (inside a sphere, H - |p - c|), the floor plane and the side domes. The
// domes are one sphere repeated six times by a polar fold; each sits inside
// its 60-degree wedge (radius 3.4 + kick < 9 * sin(30 deg)), and a point is
// always nearer its own wedge's copy than its mirror neighbour's, so the fold
// never overestimates. The min of exact distances is exact. A sphere around
// the eye is carved out of everything, so the eye is never inside a surface.

#define MD_MAX_STEPS 96
#define MD_FAR 40.0
#define MD_TAU 6.2831853
#define MD_PI 3.14159265
#define MD_FLOOR -3.4
/** The hall's radius, about the floor's centre. */
#define MD_HALL 15.0
/** The centre sphere's resting radius. */
#define MD_SPHERE 1.55
/** Side domes: count, distance from the centre and radius. */
#define MD_DOMES 6.0
#define MD_DOME_D 9.0
#define MD_DOME_R 3.4
/** Longitude cells on each sphere: whole numbers, so the seam is a cell wall. */
#define MD_CELLS_SPHERE 12.0
#define MD_CELLS_HALL 28.0
#define MD_CELLS_DOME 10.0
#define MD_BUBBLE 0.7

float gMat;
float gR;
float gDomeR;
vec3 gEye;
/** Set by map(): the hit's position relative to its own side dome's centre. */
vec3 gDomeP;
/** k3 TONE: the centre sphere's centre (the origin at rest). */
vec3 gSC = vec3(0.0);

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

vec2 rot(vec2 p, float a) { return mat2(cos(a), -sin(a), sin(a), cos(a)) * p; }

// Signed distance to an n-point star drawn as a modulated circle.
float star(vec2 l, float n, float r0, float amp) {
    return length(l) - (r0 + amp * cos(n * atan(l.y, l.x)));
}

struct Ornament {
    vec3 fill;
    vec3 line;
};

// One tile of the tapestry: a square frame around one of three motifs - an
// eight-point star in a ring, nested squares around a four-point star, or
// concentric rings - each with a wreath of dots, and a four-point star on
// every corner. The motif and the fills come from the cell's own hash, with
// cell.x taken modulo `period` so a tapestry wrapped round a sphere has no
// seam. Lines keep fixed colours so the outline hierarchy stays readable.
// `aa` is the pixel's footprint in cell units: lines never go thinner than
// it, and fade as they would have, so the far wall shimmers less.
Ornament ornament(vec2 p, float glow, float period, float aa) {
    vec2 cell = floor(p);
    cell.x = mod(cell.x, period);
    vec2 l = fract(p) - 0.5;
    vec2 corner = fract(p + 0.5) - 0.5;
    float seed = hash21(cell);
    float motif = floor(hash21(cell + 17.3) * 3.0);
    float dFrame = max(abs(l.x), abs(l.y)) - 0.47;
    float dRing = abs(length(l) - 0.38) - 0.012;
    float dStar;
    float dInner;
    if (motif < 1.0) {
        dStar = star(l, 8.0, 0.23, 0.08);
        dInner = star(rot(l, 0.3927), 8.0, 0.09, 0.035);
    } else if (motif < 2.0) {
        float sq = max(abs(l.x), abs(l.y));
        dStar = min(abs(sq - 0.27) - 0.01, abs(sq - 0.19) - 0.01);
        dInner = star(l, 4.0, 0.08, 0.05);
    } else {
        dStar = min(abs(length(l) - 0.26) - 0.01, abs(length(l) - 0.17) - 0.01);
        dInner = length(l) - 0.07;
    }
    float wreathA = floor(atan(l.y, l.x) / MD_TAU * 16.0 + 0.5) * MD_TAU / 16.0;
    float dWreath = length(l - 0.31 * vec2(cos(wreathA), sin(wreathA))) - 0.03;
    float dCorner = star(corner, 4.0, 0.12, 0.07);
    float dCornerDot = length(corner) - 0.035;

    // Fills, chosen from three purples/blues per cell.
    vec3 violet = pal(0.20);
    vec3 blue = pal(0.31);
    vec3 magenta = pal(0.12);
    vec3 base = mix(pal(0.25), pal(0.19), seed) * 0.22;
    vec3 outer = mix(mix(violet, blue, step(0.33, seed)), magenta, step(0.66, seed)) * 0.7;
    float e = max(0.01, aa);
    vec3 fill = base;
    fill = mix(fill, outer, smoothstep(e, -e, dRing + 0.012));    // inside the ring
    fill = mix(fill, magenta * 0.9, smoothstep(e, -e, dStar));     // the star
    fill = mix(fill, blue, smoothstep(e, -e, dInner));             // its heart
    fill = mix(fill, violet * 0.75, smoothstep(e, -e, dCorner));   // corner stars

    // Lines: cyan frames and rings, green stars, red corner stars, white hearts and dots.
    float w = max(0.02, aa * 1.2);
    float fade = 0.02 / w;
    float lFrame = smoothstep(w, 0.0, abs(dFrame));
    float lRing = smoothstep(w, 0.0, abs(dRing));
    float lStar = smoothstep(w, 0.0, abs(dStar));
    float lInner = smoothstep(w, 0.0, abs(dInner));
    float lWreath = smoothstep(w, 0.0, dWreath);
    float lCorner = smoothstep(w, 0.0, abs(dCorner));
    float lDot = smoothstep(w, 0.0, dCornerDot);
    vec3 line = pal(0.42) * 0.45 * lFrame + mix(pal(0.5), pal(0.6), 0.5) * 0.9 * lRing + pal(0.64) * lStar +
                pal(0.02) * 0.8 * lCorner + vec3(0.9, 0.8, 1.0) * lInner + pal(0.62) * 1.1 * lWreath +
                vec3(0.9, 0.95, 1.0) * lDot;
    return Ornament(fill, line * fade * (1.0 + glow));
}

// The tapestry sampled three times with the outline colour split apart, so
// every edge carries the red/green fringe of the reference. Only the hit
// pixel pays for it, once.
vec3 tapestry(vec2 p, float glow, float split, float period, float aa) {
    Ornament o = ornament(p, glow, period, aa);
    Ornament r = ornament(p * (1.0 + split), glow, period, aa);
    Ornament b = ornament(p * (1.0 - split), glow, period, aa);
    return o.fill + vec3(r.line.r, o.line.g, b.line.b);
}

/** A unit direction as tapestry coordinates: `cells` across a turn of longitude, square cells in latitude. */
vec2 sphereCells(vec3 n, float spin, float cells) {
    float x = abs(n.x) < 1e-5 ? 1e-5 : n.x;
    float lon = atan(n.z, x) + spin;
    float lat = asin(clamp(n.y, -1.0, 1.0));
    return vec2(lon, lat) * (cells / MD_TAU);
}

float mdField(vec3 p) {
    // The centre sphere, floating at the origin.
    float dS = length(p - gSC) - gR;
    // The hall: the inside of a sphere about the floor's centre.
    float dH = MD_HALL - length(p - vec3(0.0, MD_FLOOR, 0.0));
    float dF = p.y - MD_FLOOR;
    // Side domes: fold the angle about y into one wedge, centred on +x.
    float sector = MD_TAU / MD_DOMES;
    float a = atan(p.z, p.x);
    float k = floor(a / sector + 0.5);
    vec2 q = rot(p.xz, -k * sector);
    vec3 dp = vec3(q.x - MD_DOME_D, p.y - MD_FLOOR, q.y);
    float dD = length(dp) - gDomeR;

    float d = dS;
    gMat = 0.0;
    if (dH < d) { d = dH; gMat = 1.0; }
    if (dF < d) { d = dF; gMat = 2.0; }
    if (dD < d) { d = dD; gMat = 3.0; }
    gDomeP = dp;
    return max(d, MD_BUBBLE - length(p - gEye));
}

/** mdField through lib_shell's hooks: exactly mdField(p) at rest, and mdField read once, in a loop the compiler cannot unroll. */
float mdFieldGrown(vec3 p) {
    shellStart3(p, SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK);
    float r = 0.0;
    for (int i = SHELL_FROM; i < 4; i++) {
        if (shellPass3(i)) r = shellKeep3(i, mdField(shellAt3(i)));
    }
    return r;
}

/**
 * The field every march, normal and occlusion tap reads: mdField, the style's
 * own, through lib_shell's hook (the identity until the shell drives it).
 */
float map(vec3 p) {
    return mdFieldGrown(p);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

void main() {
    vec2 uv = view();
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float dropBump = 4.0 * drop * (1.0 - drop);
    float bass = clamp(sndShaped(SND_BASS_ATT), 0.0, 1.0);
    float treble = clamp(SND_TREBLE_PRESENCE, 0.0, 1.0);
    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    // sndShaped(SND_HIGH) straight into a glow term was the flash in the 2D style: keep it gentle.
    float glow = 0.4 * sndShaped(SND_HIGH);
    // ---- the shell: this style's shape as normalized inputs ---------------
    //
    // Every tunable of the shape is a 0..1 input here, set from the params and
    // the six knobs and 0.5 at their defaults, and mapped to its range in this
    // one place: lib_shell's shellMap(n, lo, rest, hi) puts 0.5 exactly on the
    // value the style was tuned at, so the default look is unchanged, and the
    // shell's hooks can later fractal out, move and manipulate any of them.
    float nSwell = clamp(uKnob[0], 0.0, 1.0);
    float nSplit = clamp(uKnob[1], 0.0, 1.0);
    float nFloat = clamp(uKnob[3], 0.0, 1.0);
    float nInner = clamp(uKnob[4], 0.0, 1.0);
    float nOrbit = clamp(uKnob[5], 0.0, 1.0);
    // The fringe split, the kick's swell of the sphere, where the sphere
    // floats, its inner light, and the orbit's radius.
    float split = shellMap(nSplit, 0.0048, 0.012, 0.0192);
    float swellK = shellMap(nSwell, 0.4, 1.0, 1.6);
    float lift = shellMap(nFloat, -0.9, 0.0, 0.9);
    float innerK = shellMap(nInner, 0.75, 1.0, 1.25);
    float orbitR = shellMap(nOrbit, 4.6, 5.2, 5.8);
    float hue = shellMap(nFloat, -0.1, 0.0, 0.1);
    // Time alone, at the rate the loudness clock (uFlowPhase) used to add at a typical level.
    float spin = uTime * 0.175;
    float finger = touchFalloff(uv, 0.5);

    gR = MD_SPHERE * (1.0 + 0.16 * kick * swellK);
    gDomeR = MD_DOME_R * (1.0 + 0.04 * kick);
    gSC = vec3(0.0, lift, 0.0);

    vec3 ro;
    vec3 rd;
    camAround(uv, orbitR, 2.0 * CAM_SPIN_UNIT, ro, rd);
    // Lift the eye a little so the floor's mandala shows, and hold it above
    // the side domes' tops (so it is never inside one) and well inside the
    // hall (orbit at most 5.8 * 1.35 out, 4.5 up) whatever the rig's pitch does.
    ro.y = clamp(ro.y + 0.7, MD_FLOOR + MD_DOME_R + 0.9, 4.5);
    gEye = ro;

    float t = 0.05;
    float hitT = -1.0;
    for (int i = 0; i < MD_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        float d = map(ro + rd * t);
        if (d < 0.001 + 0.0007 * t) {
            hitT = t;
            break;
        }
        t += d;
        if (t > MD_FAR) break;
    }

    // The inner light: the drop floods the hall, BUILD raises its floor.
    float inner = innerK * (1.0 + 1.6 * dropBump) * (0.85 + 0.25 * swell);
    vec3 lightCol = mix(pal(0.5 + hue), vec3(1.0, 0.9, 1.0), 0.35);
    vec3 fogCol = pal(0.22 + hue) * 0.05 + vec3(0.006, 0.003, 0.012);
    vec3 col = fogCol;
    float sceneT = hitT > 0.0 ? hitT : MD_FAR;
    // Cell units per world unit times world units per pixel, for the line AA.
    float pxWorld = 2.0 / (max(uCamFocal, 0.1) * uResolution.y);

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.001 * hitT));
        float ao = clamp(1.0 - 2.0 * (0.25 - map(p + n * 0.25)), 0.3, 1.0);
        // Restore the hit's material after the normal and AO taps.
        map(p);
        float ndv = clamp(dot(n, -rd), 0.05, 1.0);
        float px = hitT * pxWorld / ndv;

        vec3 base;
        if (gMat < 0.5) {
            // The centre sphere: its own tapestry, turning, lit from inside.
            vec3 sn = (p - gSC) / max(gR, 1e-3);
            float dens = MD_CELLS_SPHERE / (MD_TAU * gR);
            base = tapestry(sphereCells(sn, -spin * 1.5, MD_CELLS_SPHERE), glow + finger, split * 0.6,
                            MD_CELLS_SPHERE, px * dens);
            float limb = 0.45 + 0.55 * ndv;
            float rim = pow(1.0 - ndv, 3.0);
            float highlight = pow(max(dot(reflect(rd, n), normalize(vec3(-0.4, 0.7, 0.6))), 0.0), 24.0);
            col = base * limb * (0.9 + 0.3 * inner) + pal(0.5 + hue) * rim * 0.6
                + vec3(0.8, 0.85, 1.0) * highlight * 0.35;
        } else {
            float glowW = glow;
            if (gMat < 1.5) {
                // The dome overhead: the tapestry wrapped round the inside of the hall.
                vec3 hn = normalize(p - vec3(0.0, MD_FLOOR, 0.0));
                float dens = MD_CELLS_HALL / (MD_TAU * MD_HALL);
                base = tapestry(sphereCells(hn, spin * 0.2, MD_CELLS_HALL), glowW, split, MD_CELLS_HALL, px * dens);
            } else if (gMat < 2.5) {
                // The floor mandala: polar rings, mirrored across a slowly turning axis,
                // drifting outward with the flow.
                vec2 f = rot(p.xz, spin * 0.15);
                float r = length(f);
                float ang = abs(atan(f.y, abs(f.x) < 1e-5 ? 1e-5 : f.x));
                vec2 fp = vec2(ang / MD_PI * 8.0, r * 0.62 - uFlowPhase * 0.08);
                float dens = max(0.62, 16.0 / (MD_TAU * max(r, 0.5)));
                base = tapestry(fluidWarp(fp, 0.9, 0.04), glowW, split, 1024.0, px * dens);
            } else {
                // A side dome: its own sphere of tapestry, counter-turning.
                vec3 dn = gDomeP / max(gDomeR, 1e-3);
                float dens = MD_CELLS_DOME / (MD_TAU * gDomeR);
                base = tapestry(sphereCells(dn, spin, MD_CELLS_DOME), glowW, split * 0.6, MD_CELLS_DOME, px * dens);
                // The limb rim of the 2D domes, kept as a cyan edge.
                base += pal(0.5 + hue) * pow(1.0 - ndv, 4.0) * 0.4;
            }
            // Lit by the centre sphere (inverse square, softened) and a dim key from above.
            vec3 toL = gSC - p;
            float dl = length(toL);
            float dif = max(dot(n, toL / max(dl, 1e-3)), 0.0) * inner * 6.0 / (4.0 + dl * dl);
            float key = 0.18 * max(dot(n, normalize(vec3(-0.3, 0.9, 0.3))), 0.0);
            col = base * (0.25 + dif + key) * ao + lightCol * dif * 0.08;
        }
        col = mix(col, fogCol, 1.0 - exp(-hitT * 0.045));
    }

    // The halo around the sphere: the ray's closest approach to the centre,
    // taken only up to what the ray hit, so it never shines through a wall.
    float tc = clamp(dot(gSC - ro, rd), 0.0, sceneT);
    float dmin = length(ro + rd * tc - gSC);
    float haloW = 1.5 - 0.7 * bass;
    col += lightCol * 0.22 * inner * exp(-max(dmin - gR, 0.0) * haloW);

    // Dust: points on four slow shells about the sphere, one hashed candidate
    // per cell of each shell's own tapestry grid. The treble makes them sparkle.
    for (int k = 0; k < 4; k++) {
        float rk = 2.6 + 1.5 * float(k);
        float b = dot(ro, rd);
        float c = dot(ro, ro) - rk * rk;
        float disc = b * b - c;
        if (disc < 0.0) continue;
        float sq = sqrt(disc);
        for (int j = 0; j < 2; j++) {
            float ti = j == 0 ? -b - sq : -b + sq;
            if (ti < 0.4 || ti > sceneT) continue;
            vec3 hp = (ro + rd * ti) / rk;
            float cells = 18.0 + 8.0 * float(k);
            vec2 g = sphereCells(hp, uTime * 0.03 * (float(k) + 1.0) * (k == 1 || k == 3 ? -1.0 : 1.0), cells);
            vec2 cell = vec2(mod(floor(g.x), cells), floor(g.y));
            float h = hash21(cell + float(k) * 31.7);
            if (h < 0.7) continue;
            float h2 = hash21(cell + 7.1 + float(k) * 13.3);
            vec2 l = fract(g) - 0.5 - (vec2(h, h2) - 0.5) * 0.5;
            float size = 0.06 + ti * pxWorld * cells / (MD_TAU * rk);
            float m = (1.0 - smoothstep(0.0, size, length(l))) * min(1.0, 0.06 / size);
            float tw = 0.5 + 0.5 * sin(uTime * (5.0 + 9.0 * h2) + h * 40.0);
            col += pal(0.72 + hue) * m * (0.15 + 1.1 * treble * tw) * exp(-ti * 0.045);
        }
    }

    col *= 0.82 + 0.25 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
