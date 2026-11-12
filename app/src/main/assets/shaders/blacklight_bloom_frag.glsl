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

// Blacklight Bloom: a flight down a hall lined with the mirrored UV-ink
// tapestry, now a real thing in space. The walls are a 3D fold-and-scale
// fractal (the KIFS the flat style was the 2D cousin of), set in a ring around
// the flight axis and mirrored round it like a kaleidoscope, dark violet
// bodies whose fold lines glow as ink drawn three times a hair apart: green
// under the core, cyan and magenta fringing it. Behind the ring an outer wall
// carries the flat tapestry itself. Spiral discs and wire tetrahedra tumble in
// the open air, and every 32 units a soap bubble hangs in the hall: the ray
// that meets it is bent like a thin lens, so the tapestry refracts through it,
// under an iridescent film and a rim.
//
// The camera is the shared rig (camFly). The hall runs down +z; the tapestry
// tiles every 8 world units, the floaters and bubbles every 256 and 32, so the
// eye is rebased by 256 (a power of two dividing 8192) and the rig's travel
// wrap is invisible.
//
// ---- audio ------------------------------------------------------------------
//
// SND_ENERGY_TREND breathes the fold scale (how deep the repeat reads), as the
// flat style did, and the violet wash under the ink. SND_BASS_PRESENCE is the
// ink weight: the fractal's strokes thicken and its lines widen. sndShaped
// (SND_HIGH) is the outline separation, the hair between the three inks.
// SND_KICK_ENV makes the bubbles wobble (geometry, not light). SND_HAT_ENV
// lights the tetrahedra's wire. SND_HELD_NOTE_CHANGE slides the ink hue on a
// hump. A new section (SND_CUT_COUNT) picks a new kaleidoscope fold count,
// held for the section, unless the user has set Kaleido's arms. Nothing keys
// brightness off a raw envelope. The mid band (SND_MIDDLE) turns the eye's
// look aside while the mids climb and eases it back as they fall, as
// lib_terra's terraTurn and lib_truchet's truchetTurn do: 0.1 x sndShaped x
// SND_DRIVE screen units (0.15 at most, 0 in silence); the hall holds still.
// Mid gain scales it.
//
// Knobs: k0 PULSE the bubble wobble, k1 TEXTURE the ink line weight, k2 FLOW
// how fast the floaters tumble, k3 TONE the ink hue, k4 BUILD how many
// floaters there are, k5 SPACE the hall's width. Morph turns the folds (a
// different tapestry), Turbulence skews the second fold rotation.
//
// ---- why the estimate is safe ----------------------------------------------
//
// The kaleidoscope fold about the axis, the z triangle wave and every abs()
// fold are continuous mirrorings, and rotations and offsets are isometries, so
// none of them shortens a distance; the one scaling is tracked in `sc` and
// divided out. The final primitive is a bounded jack of three boxes, exact.
// The flight tube is a smax subtraction, so the march step is 0.8. Each
// floater is measured in its own 4-unit cell only and clamped to the cell's
// edge plus the gap to the nearest possible neighbour. The bubbles are
// intersected analytically and carry no geometry inside. A sphere around the
// eye is carved out of everything, so the eye is never inside a surface.

#define BB_TAU 6.2831853
#define BB_MAX_STEPS 96
#define BB_ITERS 5
#define BB_FAR 40.0
/** The eye is rebased by this; every z period below divides it. */
#define BB_ZWRAP 256.0
/** Tapestry tile along z. */
#define BB_TILE 8.0
/** Radius of the ring the fractal is centred on. */
#define BB_RING 4.6
/** The outer wall that carries the flat tapestry. */
#define BB_OUTER 7.4
/** Floater cell length along z: 64 cells per BB_ZWRAP. */
#define BB_CELL 4.0
/** Bubble spacing along z: 8 bubbles per BB_ZWRAP. */
#define BB_BUBBLE_GAP 32.0
#define BB_EYE 0.7

float gFolds;
float gScale;
vec3 gOff;
mat2 gRotA;
mat2 gRotB;
float gBar;
float gTube;
float gSpin;
float gDensity;
vec3 gEye;

/** Set by map(): 0 fractal, 1 outer wall, 2 spiral disc, 3 wire tetrahedron. */
float gMat;
/** Set by map(): the floater's local point and its seed. */
vec3 gLocal;
float gSeed;

/** The polar mirror fold about the z axis: `n` arms, continuous across every seam. */
vec2 bbKaleido(vec2 p, float n) {
    float r = length(p);
    if (r < 1e-6) return vec2(0.0);
    float w = BB_TAU / n;
    float a = mod(atan(p.y, p.x), w);
    a = abs(a - 0.5 * w);
    return r * vec2(cos(a), sin(a));
}

/** z as a triangle wave of period BB_TILE, centred on 0: a mirror, not a cut. */
float bbTileZ(float z) {
    return abs(mod(z, BB_TILE) - 0.5 * BB_TILE) - 0.25 * BB_TILE;
}

/**
 * The 3D tapestry. Returns the distance; `trap` is how close the point ever
 * came to a fold plane, in world units, which is what draws the ink lines.
 */
float tapestry3(vec3 w, out float trap) {
    vec3 p = vec3(bbKaleido(w.xy, gFolds), bbTileZ(w.z));
    p.x -= BB_RING;
    float sc = 1.0;
    trap = 1e9;
    for (int i = 0; i < BB_ITERS; i++) {
        p = abs(p) - gOff;
        p.xy = gRotA * p.xy;
        p.yz = gRotB * p.yz;
        p *= gScale;
        sc *= gScale;
        trap = min(trap, min(abs(p.x), abs(p.y)) / sc);
    }
    float t = gBar;
    float d = min(sdBox(p, vec3(1.3, t, t)), min(sdBox(p, vec3(t, 1.3, t)), sdBox(p, vec3(t, t, 1.3))));
    return d / sc;
}

/** The flat tapestry of the 2D style, for the outer wall: fold, turn, scale, trap. */
float tapestry2(vec2 p, float turn) {
    float trap = 1e9;
    float w = 1.0;
    for (int i = 0; i < 6; i++) {
        p = abs(p) - 0.62;
        p = rot2(turn) * p;
        p *= 1.34;
        w *= 1.34;
        trap = min(trap, min(abs(p.x), abs(p.y)) / w);
    }
    return trap;
}

float bbSeg(vec3 p, vec3 a, vec3 b) {
    vec3 pa = p - a;
    vec3 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

/** A wire tetrahedron of circumradius `s`: its six edges. */
float wireTetra(vec3 p, float s, float r) {
    float k = s * 0.5773503;
    vec3 a = vec3(k, k, k);
    vec3 b = vec3(k, -k, -k);
    vec3 c = vec3(-k, k, -k);
    vec3 e = vec3(-k, -k, k);
    float d = min(min(bbSeg(p, a, b), bbSeg(p, a, c)), min(bbSeg(p, a, e), bbSeg(p, b, c)));
    d = min(d, min(bbSeg(p, b, e), bbSeg(p, c, e)));
    return d - r;
}

/** A flat disc in the local xz plane: radius `R`, half thickness `th`. Exact. */
float sdDisc(vec3 p, float R, float th) {
    vec2 q = vec2(length(p.xz) - R, abs(p.y) - th);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

/** The floater of the cell `w` is in, clamped so a neighbour's can never be nearer. */
float floaters(vec3 w, out float type) {
    type = 0.0;
    float cz = floor(w.z / BB_CELL);
    float fz = w.z - (cz + 0.5) * BB_CELL;
    // Bodies stay within 0.9 of the cell middle, so a neighbour's is at least
    // (edge distance) + (half cell - 0.9) away.
    float toEdge = 0.5 * BB_CELL - abs(fz);
    float bound = toEdge + 0.5 * BB_CELL - 0.9;
    float ci = mod(cz, BB_ZWRAP / BB_CELL);
    // The two cells either side of each bubble stay empty.
    float slot = mod(ci, BB_BUBBLE_GAP / BB_CELL);
    if (slot == 3.0 || slot == 4.0) return bound;
    uint h = uhash3(uvec3(uint(ci), 0x51u, 0x2bu));
    float h1 = uhashUnit(h);
    if (h1 > gDensity) return bound;
    uint hb = uhash(h);
    float h2 = uhashUnit(hb);
    float h3 = uhashUnit(uhash(hb ^ 0x9e37u));
    float h4 = uhashUnit(uhash(hb + 0x7f4au));
    float ang = h2 * BB_TAU;
    float rad = 0.9 + 0.7 * h3;
    vec3 c = vec3(rad * cos(ang), rad * sin(ang), (cz + 0.5) * BB_CELL + (h4 - 0.5) * 0.7);
    c.y += 0.12 * sin(uTime * (0.3 + 0.2 * h2) + h3 * 6.0);
    vec3 q = w - c;
    q.xz = rot2(gSpin * (0.5 + h3) + h2 * BB_TAU) * q.xz;
    q.yz = rot2(gSpin * (0.3 + 0.6 * h4) + h3 * BB_TAU) * q.yz;
    gLocal = q;
    gSeed = h4;
    float d;
    if (h4 < 0.5) {
        type = 2.0;
        d = sdDisc(q, 0.42, 0.02);
    } else {
        type = 3.0;
        d = wireTetra(q, 0.44, 0.022 + 0.012 * gBar);
    }
    return min(d, bound);
}

float map(vec3 w) {
    float trap;
    float d = tapestry3(w, trap);
    float r = length(w.xy);
    // The flight tube, blended so it reads as a hall rather than a cut.
    d = smax(d, gTube - r, 0.6);
    gMat = 0.0;
    float dw = BB_OUTER - r;
    if (dw < d) {
        d = dw;
        gMat = 1.0;
    }
    float type;
    float df = floaters(w, type);
    if (df < d) {
        d = df;
        gMat = type;
    }
    return max(d, BB_EYE - length(w - gEye));
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** A spiral arm on a disc face: an Archimedean groove wound `turns` times. */
float spiralArm(vec2 p, float radius, float turns) {
    float rr = length(p);
    float a = atan(p.y, p.x + 1e-6);
    float arm = fract((rr / radius) * turns - a / BB_TAU);
    float line = 1.0 - smoothstep(0.0, 0.18, min(arm, 1.0 - arm));
    return line * (1.0 - smoothstep(radius * 0.75, radius, rr));
}

/** The ink line from a trap distance, never thinner than the pixel. */
float inkLine(float trap, float width, float px) {
    float w = max(width, px * 1.2);
    return (1.0 - smoothstep(w * 0.25, w, trap)) * min(1.0, width / w + 0.35);
}

void main() {
    vec2 uv = view();

    float bass = clamp(SND_BASS_PRESENCE, 0.0, 1.5);
    float treb = clamp(sndShaped(SND_HIGH), 0.0, 1.0);
    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float hat = clamp(SND_HAT_ENV, 0.0, 1.0);
    float pitch = clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0);
    float pitchBump = 4.0 * pitch * (1.0 - pitch);

    // Kaleidoscope arms: the user's when they have set some, otherwise a count
    // the section picked, held until the next one.
    float sectionPick = float(uhash(uint(mod(SND_CUT_COUNT, 4096.0)) + 0x5b1u) % 3u);
    gFolds = (uKaleido > 0.5 && uSymmetry >= 2.0) ? floor(uSymmetry) : 6.0 + 2.0 * sectionPick;
    // Kept inside 1.80..1.98: past it the strokes outrun the trap and the
    // tapestry turns to grit.
    gScale = 1.82 + 0.10 * min(swell, 1.0) + 0.06 * uMorph;
    gOff = vec3(1.0, 0.82, 0.9);
    float turn = 0.42 + 0.30 * uMorph + 0.06 * sin(uTime * 0.07);
    gRotA = rot2(turn);
    gRotB = rot2(0.27 + 0.25 * uTurbulence + 0.05 * sin(uTime * 0.05 + 1.0));
    gBar = (0.34 + 0.16 * min(bass, 1.0)) * (0.8 + 0.4 * uKnob[1]);
    gTube = 2.6 * (0.85 + 0.3 * uKnob[5]);
    gSpin = uTime * (0.25 + 0.5 * uKnob[2]);
    gDensity = 0.25 + 0.5 * uKnob[4];
    float split = (0.012 + 0.016 * treb);
    float hue = 0.12 * pitchBump + 0.2 * (uKnob[3] - 0.5);

    vec3 ro;
    vec3 rd;
    // The mids turn the look aside (see the header).
    camFly(uv - vec2(0.1 * sndShaped(SND_MIDDLE) * clamp(SND_DRIVE, 0.0, 1.0), 0.0), ro, rd);
    // Every z term tiles with a period dividing 256, so this rebase is exact
    // and invisible, and z keeps its precision far down the 8192 travel.
    ro.z = mod(ro.z, BB_ZWRAP);
    gEye = ro;

    // ---- the bubbles, analytically -------------------------------------------
    float tb = 1e9;
    vec3 bC = vec3(0.0);
    float bR = 1.0;
    float m0 = floor(ro.z / BB_BUBBLE_GAP);
    for (int k = -1; k <= 1; k++) {
        float m = m0 + float(k);
        float hm = hash11(mod(m, BB_ZWRAP / BB_BUBBLE_GAP) + 3.0);
        float R = 0.85 + 0.05 * min(swell, 1.0)
                + 0.07 * kick * (0.5 + uKnob[0]) * sin(uTime * 9.0 + hm * 20.0);
        vec3 c = vec3(0.9 * cos(hm * BB_TAU), 0.7 * sin(hm * BB_TAU), m * BB_BUBBLE_GAP + 0.5 * BB_BUBBLE_GAP);
        vec3 oc = ro - c;
        float b = dot(oc, rd);
        float cc = dot(oc, oc) - R * R;
        float h = b * b - cc;
        // The eye inside a bubble sees no film: it has flown through it.
        if (cc > 0.0 && h > 0.0) {
            float t = -b - sqrt(h);
            if (t > 0.0 && t < tb) {
                tb = t;
                bC = c;
                bR = R;
            }
        }
    }
    bool hasBubble = tb < BB_FAR;
    vec3 bN = vec3(0.0, 0.0, -1.0);
    vec3 bentDir = rd;
    if (hasBubble) {
        bN = normalize(ro + rd * tb - bC);
        // A thin lens, harder toward the limb, as the flat bubble bent.
        bentDir = normalize(mix(rd, refract(rd, bN, 0.75), 0.55 + 0.2 * min(bass, 1.0)));
    }

    // ---- the march ------------------------------------------------------------
    vec3 o = ro;
    vec3 dir = rd;
    float t = 0.04;
    float done = 0.0;
    float tLimit = hasBubble ? tb : 1e9;
    bool bent = false;
    float hitT = -1.0;
    float used = 0.0;
    for (int i = 0; i < BB_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (done + t > BB_FAR) break;
        used = float(i);
        float d = map(o + dir * t);
        float eps = 0.0007 * (done + t) + 0.0005;
        if (d < eps) {
            hitT = t;
            break;
        }
        float adv = max(d * 0.8, eps);
        if (!bent && t + adv >= tLimit) {
            // Through the film: carry on from the bubble's face, bent.
            o = o + dir * tLimit;
            done += tLimit;
            dir = bentDir;
            t = 0.0;
            bent = true;
            continue;
        }
        t += adv;
    }
    float total = done + (hitT > 0.0 ? hitT : t);
    if (!bent) hasBubble = false;

    vec3 fogCol = pal(0.70 + hue) * 0.05 * (0.5 + 0.5 * swell) + vec3(0.006, 0.002, 0.012);
    vec3 col = fogCol;
    if (hitT > 0.0) {
        vec3 p = o + dir * hitT;
        vec3 n = normalAt(p, max(0.0012, 0.0009 * total));
        // Restore the hit's globals after the normal taps.
        map(p);
        float mat = gMat;
        float ndv = clamp(dot(n, -dir), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.1, 0.3, 1.0);
        float px = total * 2.0 / (max(uCamFocal, 0.1) * uResolution.y);
        float sp = split * (bent ? 1.8 : 1.0);
        float lineW = (0.012 + 0.010 * min(bass, 1.0)) * (0.7 + 0.6 * uKnob[1]);

        if (mat < 0.5) {
            // The ink, three times a hair apart: the trap read at the hit and
            // at the hit scaled a hair either way about the axis.
            float tg;
            float tc;
            float tm;
            tapestry3(p, tg);
            tapestry3(vec3(p.xy * (1.0 + sp), p.z), tc);
            tapestry3(vec3(p.xy * (1.0 - sp), p.z), tm);
            float g = inkLine(tg, lineW, px);
            float c = inkLine(tc, lineW, px);
            float m = inkLine(tm, lineW, px);
            vec3 ink = pal(0.62 + hue) * g;
            ink += pal(0.46 + hue) * c * (1.0 - 0.55 * g);
            ink += pal(0.88 + hue) * m * (1.0 - 0.55 * g);
            vec3 body = pal(0.72 + hue) * (0.03 + 0.07 * ndv) * ao;
            col = body + ink * (0.8 + 0.4 * ao) + pal(0.46 + hue) * fres * 0.30 * ao;
        } else if (mat < 1.5) {
            // The outer wall: the flat tapestry, painted on the cylinder.
            vec2 fold = bbKaleido(p.xy, gFolds);
            vec2 q = vec2(fold.y, bbTileZ(p.z)) * 0.42;
            float tw = 0.42 + 0.06 * sin(uTime * 0.07);
            float lw = max(0.035, px * 0.6);
            float g = 1.0 - smoothstep(0.0, lw, tapestry2(q, tw));
            float c = 1.0 - smoothstep(0.0, lw, tapestry2(q * (1.0 + 2.0 * sp), tw));
            float m = 1.0 - smoothstep(0.0, lw, tapestry2(q * (1.0 - 2.0 * sp), tw));
            vec3 ink = pal(0.62 + hue) * g;
            ink += pal(0.46 + hue) * c * (1.0 - 0.55 * g);
            ink += pal(0.88 + hue) * m * (1.0 - 0.55 * g);
            col = pal(0.70 + hue) * 0.03 + ink * 0.55;
        } else {
            vec3 tint = mix(pal(0.44 + hue), pal(0.92 + hue), gSeed);
            float spec = pow(max(dot(reflect(dir, n), normalize(vec3(-0.4, 0.6, -0.7))), 0.0), 30.0);
            if (mat < 2.5) {
                // A spiral disc: dark face, the arm glowing on it.
                float s = spiralArm(gLocal.xz, 0.42, 3.0 + floor(gSeed * 12.0));
                col = tint * (0.05 + 0.08 * ndv) + mix(vec3(1.0), tint, 0.45) * s * 0.75
                    + tint * fres * 0.25 + vec3(spec) * 0.2;
            } else {
                // Wire: the hats light it.
                col = tint * (0.45 + 0.25 * ndv) * (0.8 + 0.8 * hat) + vec3(spec) * 0.3;
            }
        }
        col = mix(col, fogCol, 1.0 - exp(-total * 0.05));
    }

    // ---- the film over whatever the bubble showed ------------------------------
    if (hasBubble) {
        float bdv = clamp(dot(bN, -rd), 0.0, 1.0);
        // Thin-film colour: the interference band walks with the view angle.
        vec3 film = pal(fract(0.2 + bdv * 0.8 + hue));
        col = col * 1.12 + film * 0.12 * (1.0 - bdv);
        float rim = 1.0 - smoothstep(0.05, 0.35, bdv);
        col += pal(0.50 + hue) * rim * 0.30;
        col += vec3(0.9, 0.95, 1.0) * pow(max(dot(reflect(rd, bN), normalize(vec3(-0.4, 0.6, -0.7))), 0.0), 40.0) * 0.35;
        col = mix(col, fogCol, (1.0 - exp(-tb * 0.05)) * 0.5);
    }

    if (!touchIdle()) {
        col += pal(0.5 + 0.15 * sin(uTime * 0.05)) * min(touchWake(uv), 3.0) * 0.05;
    }
    col *= 0.85 + 0.2 * min(swell, 1.0);
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
