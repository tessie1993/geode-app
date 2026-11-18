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
//#include lib_glass

// Glass Marble: the flat style's one big cat's-eye marble, now a whole drift
// of them hanging in the sunset sky, and the eye flies through it. Every
// marble is the same object the 2D style drew: clear glass round three to five
// twisted coloured vanes that meet at its axis, with a small nebula swirling at
// its heart and seed bubbles trapped in the glass. The glass is the family's
// two-bounce sphere (glassSphereTrace), so each marble shows the sunset sea
// upside down with dispersed edges; the vanes are a true volume sampled along
// the refracted chord and composited front to back, lit from behind by the
// family's back key, which sits ahead of the flight, so the marbles glow as
// the eye comes on them. Far marbles sink into the sunset (distance fog to the
// world itself), and a faint warm haze gathers where rays graze the glass.
//
// ---- the scene and the camera -----------------------------------------------
//
// Space is cut into 4-unit cubic cells; a cell holds one marble or none, its
// centre jittered, its radius, roll, spin axis and vane hue hashed from the
// cell. Cells round the travel axis are mostly left empty, so there is an open
// lane to fly down. The camera is the shared rig (camFly) down +z. The cell
// hash repeats every 16 cells along z (64 world units, which divides 8192),
// and the eye is rebased by mod 64, so the rig's travel wrap is invisible and
// z keeps its precision. Nothing else in the scene reads world z: the vanes,
// bubbles and glints are all in the marble's own frame.
//
// A marble near the eye shrinks away as the eye passes (its radius eases to 0
// inside 1.4..3.6 units of its centre), so the eye is never inside the glass
// and every marble stays a whole sphere, which is what the analytic optics
// assume. That also works on the rig's orbit and hover paths, which leave the
// lane.
//
// ---- audio ------------------------------------------------------------------
//
//   bass         swells the vanes, the nebula hearts and (a little, weighted
//                by k0 PULSE) the marbles themselves: geometry, not light;
//   flatness     fills the glass with seed bubbles (frost): noise clouds it;
//   centroid     the dispersion at the rim;
//   pitch        the sun's height (glassLook), so the highlights and the
//                vanes' backlight follow the melody. No sound rolls a marble,
//                twists its vanes or turns the sun (owner 2026-11-12, no style
//                spins to the music);
//   hat          glints on the surfaces.
//
// ---- macros and knobs --------------------------------------------------------
//
// Speed scales the turn. Complexity adds vanes (three to five). Chaos wobbles
// the surfaces. Energy scales every reaction. k0 PULSE weights the bass swell,
// k3 TONE turns the vane hue, k5 SPACE thins or fills the drift.
//
// ---- why the distance estimate is safe --------------------------------------
//
// Each cell measures only its own marble. A neighbour's marble can be closer,
// so the cell's distance is clamped to its own wall plus the gap every marble
// leaves to its cell wall: the jitter is at most 0.5 per axis and the largest
// radius (1.05, times the 1.12 full swell) is 1.18, so a marble reaches at most
// 1.68 of the 2.0 half-cell, and MD_GAP is 0.3. The radius depends only on the
// cell (and the eye, which is fixed for the frame), so the field is exact
// inside a cell and a lower bound across it. The marched hit is then replaced
// by the exact ray-sphere intersection for the optics.
//
// ---- cost -------------------------------------------------------------------
//
// One hash-and-sphere per march step (bounded by MD_MAX_STEPS, broken on the
// Detail budget uSteps). On a marble's pixels: 6 / 9 / 14 volume samples
// (Saver / Balanced / High), each one plane test, and 6 world lookups, as the
// flat style spent on its one marble.

#define MD_MAX_STEPS 96
#define MD_FAR 42.0
/** Cell size, world units. */
#define MD_CELL 4.0
/** Cells along z before the hash repeats: 16 * 4 = 64 world units, which divides 8192. */
#define MD_ZCELLS 16.0
#define MD_ZWRAP 64.0
/** The clearance every marble leaves to its cell wall (see the header). */
#define MD_GAP 0.3

/** The eye, z rebased into 0..64 (see main). */
vec3 gEye;
/** The frame's swell and drift density. */
float gSwell;
float gEmpty;
/** What the field holds at a point (lib_glass_field): the distance, and the cell's marble centre, radius and hash. */
struct GlassHit {
    float d;
    vec3 c;
    float r;
    float h;
};

/** The marble a cell holds: centre `c`, radius `r` (0 for an empty cell), hash `h`. */
void marbleIn(vec3 cell, out vec3 c, out float r, out float h) {
    vec3 hc = vec3(cell.xy, mod(cell.z, MD_ZCELLS));
    float h1 = glassHash3(hc);
    float h2 = glassHash3(hc + vec3(7.13, 3.71, 1.37));
    float h3 = glassHash3(hc + vec3(2.93, 11.31, 5.77));
    h = h1;
    c = (cell + 0.5) * MD_CELL + (vec3(h1, h2, h3) - 0.5);
    r = (0.55 + 0.5 * h2) * gSwell;
    // The lane: the four cells round the travel axis keep a marble only rarely.
    vec2 cc = (cell.xy + 0.5) * MD_CELL;
    float lane = dot(cc, cc) < 9.0 ? 0.8 : 0.0;
    if (h3 < max(gEmpty, lane)) r = 0.0;
    // A marble the eye is passing shrinks away, so the eye is never inside one.
    r *= smoothstep(1.4, 3.6, length(c - gEye));
}

GlassHit glassMap(vec3 p) {
    vec3 cell = floor(p / MD_CELL);
    vec3 c;
    float r;
    float h;
    marbleIn(cell, c, r, h);
    vec3 af = abs(p - (cell + 0.5) * MD_CELL);
    float toEdge = 0.5 * MD_CELL - max(af.x, max(af.y, af.z));
    float ds = r > 0.01 ? length(p - c) - r : 1e3;
    GlassHit res;
    res.c = c;
    res.r = r;
    res.h = h;
    res.d = min(ds, toEdge + MD_GAP);
    return res;
}

//#include lib_glass_field

/**
 * One cat's-eye marble, the flat style's shading end to end: the hit at `p`
 * (exact, on the sphere `c`, `R`), the vane volume along the refracted chord,
 * the nebula heart, seed bubbles, then the two-bounce glass, reflection, rim
 * and glints. `h` is the marble's own hash: its roll phase, axis and hue.
 */
vec3 shadeMarble(vec3 p, vec3 rd, vec3 c, float R, float h, float t, float hue, float vanes, GlassEar ear, GlassLook lk) {
    float ior = glassIor(1.5);
    float spread = glassSpread(0.015 + 0.035 * ear.sharp);
    float twist = 1.4 + 0.3 * sin(t * 0.2 + h * 6.0);
    float roll = t * (0.1 + 0.1 * h) + h * 9.0;
    mat3 rot = glassEuler(vec3(0.35 * sin(t * 0.11 + h * 4.0) + h * 3.0, roll, 0.5 + 0.2 * sin(t * 0.07) + h * 5.0));
    mat3 inv = transpose(rot);
    float mh = hue + 0.25 * glassHash1(h * 97.0);

    vec3 n = glassWobble(normalize(p - c), p - c, glassChaos());
    float cosI = max(dot(n, -rd), 0.0);
    float fr = glassFresnel(cosI, ior);
    vec3 key = glassKeyDir(lk);

    // The volume: sample the refracted chord through the vanes.
    vec3 d1 = glassRefract(rd, n, 1.0 / ior);
    vec3 oc = p - c;
    float b = dot(oc, d1);
    float chord = max(-b + sqrt(max(b * b - (dot(oc, oc) - R * R), 0.0)), 0.0);
    float q = glassQuality();
    int samples = q < 0.3 ? 6 : (q < 0.6 ? 9 : 14);
    float dt = chord / float(samples);
    vec3 acc = vec3(0.0);
    float trans = 1.0;
    float prev = 0.0;
    vec3 prevS = vec3(0.0);
    for (int i = 0; i <= 14; i++) {
        if (i > samples) break;
        vec3 s = inv * (oc + d1 * (dt * float(i))) / R;
        float th = atan(s.z, s.x) + s.y * twist;
        float g = sin(vanes * th) * length(s.xz);
        if (i > 0 && g * prev < 0.0) {
            // A vane crossed between the two samples: interpolate to it and shade it as a sheet.
            vec3 x = mix(prevS, s, prev / (prev - g));
            float rr = length(x.xz);
            float r3 = length(x);
            float edge = 1.0 - smoothstep(0.5, 0.62 + 0.08 * clamp(ear.bass, 0.0, 1.2), r3);
            if (edge > 0.0) {
                float tx = atan(x.z, x.x) + x.y * twist;
                float id = floor(mod(tx * vanes / GLASS_PI + 0.5, 2.0 * vanes));
                vec3 sn = rot * vec3(-sin(tx), 0.0, cos(tx));
                float cs = abs(dot(sn, d1));
                vec3 vc = pal(mh + 0.18 + 0.11 * mod(id, vanes));
                float back = 0.3 + 0.9 * pow(max(dot(d1, key), 0.0), 2.0);
                float lit = 0.35 + 0.65 * abs(dot(sn, key));
                float a = edge * (1.0 - exp(-0.35 / max(cs, 0.05))) * smoothstep(0.0, 0.05, rr);
                acc += trans * a * vc * (lit + back) * 0.9;
                trans *= 1.0 - a;
            }
        }
        // The nebula heart: a slow, warped glow at the marble's centre, between the vanes.
        float r3c = length(s);
        if (r3c < 0.5) {
            vec3 w = s * 4.0;
            w += 0.6 * sin(w.zxy * 1.9 + lk.flow * 2.0 + h * 5.0);
            w += 0.4 * sin(w.yzx * 3.1 - lk.flow * 1.6);
            float neb = pow(0.5 + 0.5 * sin(w.x + w.y + w.z), 3.0) * (1.0 - smoothstep(0.1, 0.5, r3c));
            vec3 nc = pal(mh + 0.6 + 0.2 * w.y);
            acc += trans * nc * neb * dt / R * 2.2 * (0.6 + 0.8 * clamp(ear.bass, 0.0, 1.2));
        }
        prev = g;
        prevS = s;
    }
    // Seed bubbles: tiny bright specks in the glass, more in noisy music.
    vec3 sp = inv * (oc + d1 * chord * 0.35) / R * 9.0;
    float bub = glassHash3(floor(sp));
    float speck = step(0.975 - 0.06 * ear.frost, bub) * exp(-dot(fract(sp) - 0.5, fract(sp) - 0.5) * 40.0);
    acc += trans * vec3(1.0, 0.95, 0.9) * speck * 0.8;

    vec3 tint = mix(vec3(1.0), pal(mh + 0.55), 0.15);
    vec3 through = glassSphereTrace(p, n, rd, c, R, ior, spread, tint, 0.25, glassBounces(), lk);
    vec3 inside = through * trans + acc;
    vec3 refl = glassEnv(reflect(rd, n), lk);
    vec3 col = inside * (1.0 - fr) + refl * fr;
    col += glassRim(cosI, mh + 0.2, 0.5);
    // Glints in the marble's own frame, scaled to it, so a small marble glints as finely as the hero did.
    col += vec3(1.0) * glassGlint(inv * (p - c) * (1.45 / R), 6.0, ear.hat) * (0.4 + fr);
    return col;
}

void main() {
    vec2 uv = view();
    glassSetup();
    float t = glassTime();
    GlassEar ear = glassListen();
    float tone = gGlassIn.tone - 0.5;
    float hue = 0.04 * sin(t * 0.019) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue) + 0.3 * tone;
    GlassLook lk = glassLook(GLASS_ENV_FRACTAL, hue, ear);
    float vanes = 3.0 + floor(glassComplexity() * 2.0 + 0.5);

    // k0 PULSE 0..1 (0.5 neutral). At most 1.12: the cell gap in the header is sized for it.
    float pulse = gGlassIn.pulse;
    gSwell = 1.0 + 0.12 * clamp(ear.bass / 1.2, 0.0, 1.0) * pulse;
    gEmpty = clamp(0.45 - 0.6 * (gGlassIn.space - 0.5), 0.15, 0.75);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // The cells tile z with period 64 (power of two), so this shift is exact
    // and invisible; without it a highp z far down the 8192 travel keeps too
    // little fraction for the cell hash and the hit.
    ro.z = mod(ro.z, MD_ZWRAP);
    // Grow and layers (lib_grow) measure from the eye in the frame the world is read in.
    growEye(ro);
    gEye = ro;

    vec3 sky = glassEnv(rd, lk);
    float tt = 0.02;
    float hitT = -1.0;
    vec3 haze = vec3(0.0);
    vec3 hazeCol = pal(hue + 0.08) * 0.5 + 0.5 * vec3(1.0, 0.7, 0.45);
    // The field where the ray met a marble: which one.
    GlassHit hit;
    hit.c = vec3(0.0);
    hit.r = 0.0;
    hit.h = 0.0;
    for (int i = 0; i < MD_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * tt;
        GlassHit at = glassField(p);
        float d0 = at.d;
        if (d0 < glassEps(tt)) {
            hitT = tt;
            hit = at;
            break;
        }
        // A warm haze where the ray grazes a marble (the sun scattered off the glass).
        if (at.r > 0.01) {
            float near = length(p - at.c) - at.r;
            haze += hazeCol * exp(-max(near, 0.0) * 3.0) * 0.006 * exp(-tt * 0.06);
        }
        tt += d0;
        if (tt > MD_FAR) break;
    }

    vec3 col = sky;
    if (hitT > 0.0) {
        vec3 c = hit.c;
        float R = hit.r;
        float h = hit.h;
        // The exact surface for the optics: the march only found which marble.
        vec2 hs = glassSphere(ro, rd, c, R);
        float th = hs.x > 0.0 ? hs.x : hitT;
        vec3 p = ro + rd * th;
        vec3 m = shadeMarble(p, rd, c, R, h, t, hue, vanes, ear, lk);
        col = mix(sky, m, exp(-th * 0.045));
    }
    col += haze;

    col *= 0.85 + 0.15 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(glassTone(col)), 1.0);
}
