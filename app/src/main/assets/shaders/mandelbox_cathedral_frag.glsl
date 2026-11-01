#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// The shape controls are spent on the church itself (lib_neon), not on the rays.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_neon

// Mandelbox Cathedral: a slow flight down the nave of an endless gothic
// church. Clustered piers of glass-chrome carry a pointed arcade; above them
// a ribbed vault whose ribs are neon tubes; stained-glass windows in the
// clerestory throw shafts of coloured light across the nave; through the
// arcade, the aisles are filled with a Mandelbox, iridescent and laced with
// the filigree of its own orbit; a polished floor mirrors it all; and far down
// the nave, the light of the altar.
//
// WHY BUILT, NOT CARVED. A Mandelbox seen from inside, with a nave simply cut
// out of it, is solid fractal at every wall: the cut face shows the orbit's
// sub-pixel noise and the picture reads as a textured tube. So the church is
// modelled (signed distances: a two-centred pointed arch for the vault and
// the arcade, cylinders for the piers, tubes along the vault for the ribs),
// and the Mandelbox fills the aisles behind the arcade, where its detail
// reads as ornament.
//
// THE ARCH. A gothic arch is set out with two compass points either side of
// the axis on the springing line: the arch is the intersection of two discs,
// and below the springing line the walls run straight down. arch2() is that
// construction; the vault, the arcade openings and the windows all use it.
//
// THE BAYS. Every MC_BAY (2) world units: piers at the bay ends, an arcade
// opening and a window centred in the bay, a transverse rib over each pair of
// piers, two diagonal ribs crossing at the bay's crown, a ridge rib along the
// top. The fractal repeats every MC_PERIOD (8) and the nave's sway every
// MC_TILE (16); all divide the rig's 8192 travel wrap, so the eye's z is
// rebased into one tile and the wrap is invisible.
//
// LIGHT. Cheap and layered: the ribs are emitters (their glow gathered along
// the ray), the windows are emitters, the shafts are density planes slanting
// down from the windows (sampled along the primary march, no shadow rays),
// the altar is the haze brightening toward the axis ahead. The floor takes a
// short second march along the reflected ray.
//
// ---- the shared controls (lib_neon) ------------------------------------------
//
// - Morph (neonForm) raises the arches from a broad gothic to a steep lancet,
//   and turns the Mandelbox's fold scale (-1.55 to -2.05) so the aisles
//   change pattern: a different church.
// - Warp (neonFlow) bends the nave into a slow S and leans the aisles' fractal.
// - Turbulence (neonChaos) turns a small rotation into every fold, varying in
//   time, and sets the rib wires flickering.
// - Intensity (neonGlow) is the emission of the ribs, the glass and the air.
// Twist, tile, ripple and the kaleidoscope come through view(); the Space
// macro's field of view is the rig's (camFly): wide is a sweeping nave,
// narrow a flat, ornate telephoto.
//
// ---- sound ---------------------------------------------------------------------
//
// - SND_KICK_ENV runs light down the nave: the transverse ribs flare one
//   after another, away from the eye, as the envelope falls.
// - SND_SNARE_ENV flashes the diagonal ribs.
// - SND_HAT_ENV lights dust motes in the shafts.
// - SND_BASS_ATT and SND_SUB_ATT (neonWeight) breathe the aisles' fractal.
// - SND_BUILD_UP brightens the windows and the shafts as a build gathers;
//   SND_HELD_SURGE (the drop) floods the nave with haze and blazes the ribs.
// - SND_CUT_COUNT re-seeds the fractal's offset on a hard cut, eased across
//   SND_HELD_HARD_CUT so the aisles re-grow rather than pop.
// - SND_CHORD_HUE stains the glass and the altar light; the key
//   (neonKeyTurn) slides the whole ramp; SND_TENSION pulls the ribs toward
//   the ramp's hot end.
// - In silence (neonQuiet) the ribs and the shafts dim to embers.
//
// Provenance: written from scratch for MuzViz from general techniques - the
// Mandelbox distance estimator (box fold, sphere fold, scale, offset;
// |p| / |dr|); domain repetition; a two-centred pointed arch as an
// intersection of discs; tubes as the distance to a curve in a 2D section;
// orbit traps as lines; glow gathered along the ray from 1 / (1 + k d^2);
// density planes for light shafts; a Voronoi lattice for leaded glass; a
// cosine thin-film tint. No code was taken from any published shader.

#define MC_MAX_STEPS 120
#define MC_REFL_STEPS 36
#define MC_ITER 8
#define MC_FAR 40.0
#define MC_TAU 6.2831853
/** World to fractal: the fractal cell (2 units) is MC_PERIOD world units. */
#define MC_WORLD 0.25
#define MC_PERIOD 8.0
#define MC_TILE 16.0
#define MC_BAY 2.0
/** The nave: half its width at the wall line, the springing line, the floor, the wall's thickness. */
#define MC_HALF_W 1.1
#define MC_SPRING 0.2
#define MC_FLOOR -1.0
#define MC_WALL 0.3

#define MAT_STONE 0.0
#define MAT_FLOOR 1.0
#define MAT_RIB 2.0
#define MAT_FRACTAL 3.0

// ---- per-frame state, written in main(), read by map() ----------------------

float gScale;
vec3 gOffset;
float gSway;
float gTurn;
float gTone;
/** The vault's and the arcade's compass offsets: wider is more pointed. */
float gArchC;
float gArcadeC;
vec4 gTrap;
float gMat;
/** The nearest rib, and which kind (0 transverse, 1 diagonal, 2 ridge or moulding), for glow. */
float gRibD;
float gRibKind;
/** The kick's ring of light: where it is along the nave, and how bright. */
vec2 gRing;

/**
 * The neon ramp: electric blue -> violet -> magenta -> orange -> acid green
 * and back, five fixed stops blended smoothly, then pulled toward the user's
 * palette (neonPal). Fixed stops rather than a cosine wheel, which passes
 * through greys and browns between its primaries.
 */
vec3 neon(float t) {
    float x = fract(t + gTone) * 5.0;
    vec3 s0 = vec3(0.05, 0.30, 1.00);
    vec3 s1 = vec3(0.50, 0.08, 1.00);
    vec3 s2 = vec3(1.00, 0.06, 0.62);
    vec3 s3 = vec3(1.00, 0.42, 0.03);
    vec3 s4 = vec3(0.30, 1.00, 0.18);
    float f = smoothstep(0.0, 1.0, fract(x));
    vec3 a = x < 1.0 ? s0 : x < 2.0 ? s1 : x < 3.0 ? s2 : x < 4.0 ? s3 : s4;
    vec3 b = x < 1.0 ? s1 : x < 2.0 ? s2 : x < 3.0 ? s3 : x < 4.0 ? s4 : s0;
    return neonPal(mix(a, b, f), t + gTone);
}

float hash2(vec2 p) {
    p = fract(p * vec2(0.1031, 0.1030));
    p += dot(p, p.yx + 33.33);
    return fract((p.x + p.y) * p.x);
}

float hash3(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.x + p.y) * p.z);
}

/**
 * A two-centred pointed arch in its own plane: negative inside. halfSpan is
 * half the opening, c how far each compass point sits from the axis (0 a
 * round arch, larger more pointed), spring the springing line; below it the
 * sides run straight down.
 */
float arch2(vec2 q, float halfSpan, float c, float spring) {
    q.y = max(q.y, spring);
    float r = halfSpan + c;
    return max(length(q - vec2(-c, spring)) - r, length(q - vec2(c, spring)) - r);
}

/** The nave's centre line at depth z: a slow S whose size is the Warp control. */
vec2 naveAxis(float z) {
    return vec2(gSway * sin(z * MC_TAU / MC_TILE), 0.3 * gSway * sin(z * MC_TAU / (MC_TILE * 0.5)));
}

/** The nave's cross-section: negative inside the vault. */
float naveSd(vec2 q) {
    return arch2(q, MC_HALF_W, gArchC, MC_SPRING);
}

/** The arcade opening in the bay-local (z, y) plane. */
float arcadeSd(float zl, float y) {
    return arch2(vec2(zl, y), 0.66, gArcadeC, -0.2);
}

/** A clerestory window in the bay-local (z, y) plane: a lancet above a sill. */
float windowSd(float zl, float y) {
    return max(arch2(vec2(zl, y), 0.3, 0.12, 1.0), 0.72 - y);
}

/** The Mandelbox distance in world units; orbit traps into gTrap. */
float fractal(vec3 pw) {
    vec3 p = pw * MC_WORLD;
    p.z = mod(p.z + 1.0, 2.0) - 1.0;
    float dr = 1.0;
    vec4 trap = vec4(1e3);
    for (int i = 0; i < MC_ITER; i++) {
        p = clamp(p, -1.0, 1.0) * 2.0 - p;
        // Turbulence: a small turn inside every fold, an isometry, so the
        // estimate stays a bound.
        if (gTurn > 0.0) {
            float a = gTurn * sin(float(i) * 1.7 + uTime * 0.37);
            p.xy = mat2(cos(a), -sin(a), sin(a), cos(a)) * p.xy;
        }
        // Minimum radius^2 0.25, fixed radius^2 1.
        float k = clamp(1.0 / max(dot(p, p), 0.25), 1.0, 4.0);
        p *= k;
        dr *= k;
        p = p * gScale + gOffset;
        dr = dr * abs(gScale) + 1.0;
        trap = min(trap, vec4(abs(p), abs(length(p) - 1.6)));
    }
    gTrap = trap;
    return length(p) / abs(dr) / MC_WORLD;
}

float map(vec3 p) {
    vec2 q = p.xy - naveAxis(p.z);
    float ax = abs(q.x);
    // Bay-local z: piers at +-1 (the bay ends), the opening and window at 0.
    float zl = mod(p.z, MC_BAY) - 0.5 * MC_BAY;
    float nave = naveSd(q);

    // The walls and vault: a shell MC_WALL thick outside the nave, with the
    // arcade opened through it.
    float arcade = arcadeSd(zl, q.y);
    float stone = max(max(-nave, nave - MC_WALL), -arcade);

    // Clustered piers standing proud of the wall line at the bay ends.
    vec2 pp = vec2(ax - (MC_HALF_W - 0.06), abs(zl) - 0.5 * MC_BAY);
    float pier = length(pp) - 0.16;
    vec2 ps = abs(pp);
    pier = min(pier, min(length(vec2(ps.x - 0.18, pp.y)), length(vec2(pp.x, ps.y - 0.18))) - 0.06);
    pier = max(pier, q.y - 1.4);
    stone = min(stone, pier);

    // Ribs, as tubes along curves on the vault (only above the springing line).
    float above = MC_SPRING - 0.05 - q.y;
    float tr = max(length(vec2(nave, abs(zl) - 0.5 * MC_BAY)) - 0.07, above);
    float k = 0.5 * MC_BAY / MC_HALF_W;
    float dg = max(length(vec2(nave, (abs(zl) - ax * k) * inversesqrt(1.0 + k * k))) - 0.045, above);
    float ridge = length(vec2(nave, q.x)) - 0.04;
    float moulding = max(length(vec2(arcade, ax - MC_HALF_W)) - 0.045, MC_FLOOR + 0.1 - q.y);
    float rib = min(min(tr, dg), min(ridge, moulding));
    gRibD = rib;
    gRibKind = tr <= rib ? 0.0 : dg <= rib ? 1.0 : 2.0;

    // The aisles beyond the arcade: the Mandelbox. Its bound (the distance
    // to the aisle) is free; the fractal is only run when that bound could be
    // the nearest thing, which spares it for every ray down the nave.
    float floorD = q.y - MC_FLOOR;
    float near = min(min(stone, rib), floorD);
    float fr = MC_HALF_W + 0.45 - ax;
    if (fr < near) fr = max(fractal(p), fr);
    float d = min(near, fr);
    gMat = d == floorD ? MAT_FLOOR : d == rib ? MAT_RIB : d == fr ? MAT_FRACTAL : MAT_STONE;
    return d;
}

vec3 calcNormal(vec3 p, float t) {
    vec2 e = vec2(0.0006 * (1.0 + t), -0.0006 * (1.0 + t));
    return normalize(e.xyy * map(p + e.xyy) + e.yyx * map(p + e.yyx) + e.yxy * map(p + e.yxy) + e.xxx * map(p + e.xxx));
}

/**
 * Each bay's own colour: an eighth of the ramp per bay, so the nave runs
 * through the whole spectrum every MC_TILE and closes on itself at the wrap.
 */
float bayHue(float z) {
    return floor(z / MC_BAY) * (MC_BAY / MC_TILE);
}

/** How bright the rib at depth z is: its own glow, the kick's ring, a snare on the diagonals. */
float ribLight(float z, float kind, float base, float snare) {
    float ring = gRing.y * exp(-abs(z - gRing.x) * 1.6);
    return base * (1.0 + 2.5 * ring) + (kind == 1.0 ? snare : 0.0);
}

vec3 ribColour(float z, float kind, float hot) {
    float h = bayHue(z);
    return neon(kind == 0.0 ? h + hot : kind == 1.0 ? h + 0.4 + hot : 0.25 + hot + 0.02 * z);
}

/**
 * Leaded glass: a Voronoi lattice of panes, each a colour off the ramp
 * stained toward the chord, with dark lead between them and a rose at the
 * lancet's head. `w` is window-local (0 at the centre, about +-1 across).
 */
vec3 glass(vec2 w, float stain, float id) {
    vec2 g = w * vec2(3.2, 2.4);
    vec2 cell = floor(g);
    float f1 = 9.0;
    float f2 = 9.0;
    float pick = 0.0;
    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            vec2 c = cell + vec2(float(i), float(j));
            vec2 o = c + 0.5 + 0.35 * (vec2(hash2(c + id), hash2(c.yx + id * 1.7)) - 0.5);
            float d = length(g - o);
            if (d < f1) {
                f2 = f1;
                f1 = d;
                pick = hash2(c * 1.31 + id);
            } else if (d < f2) {
                f2 = d;
            }
        }
    }
    float lead = smoothstep(0.04, 0.12, f2 - f1);
    // Panes from the ramp's blue-to-orange span: its acid green reads as olive behind lead.
    vec3 pane = neon(0.02 + pick * 0.6 + stain);
    // A rose: rings and petals at the head of the lancet.
    vec2 r = w - vec2(0.0, 0.95);
    float rr = length(r);
    float pet = abs(fract(atan(r.y, r.x) / MC_TAU * 8.0) - 0.5);
    float rose = smoothstep(0.42, 0.38, rr) * (0.6 + 0.4 * smoothstep(0.2, 0.0, pet - rr * 0.3));
    pane = mix(pane, neon(0.55 + stain + rr), rose * 0.7);
    return pane * lead * (0.7 + 0.3 * pick);
}

float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1.0, 0.0)), f.x), mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0, 1.0)), f.x), f.y);
}

/** Marble veins: thin bright lines where a warped noise crosses its middle. */
float veins(vec2 p) {
    float n = vnoise(p * 1.7) * 0.6 + vnoise(p * 4.3) * 0.3 + vnoise(p * 11.0) * 0.1;
    return exp(-abs(n - 0.5 + 0.15 * sin(p.x * 3.0 + p.y)) * 55.0);
}

/** A thin line at distance d, width w, as an emissive profile. */
float inlay(float d, float w) {
    return exp(-abs(d) / w);
}

/**
 * The nave floor: polished black marble with pale veins, inlaid in the
 * Cosmati manner. A runner down the middle of two braided strands with an
 * interlaced ring in each half bay; either side, a panel per bay holding an
 * eight-point star of coloured tesserae; borders between them and a band
 * under every pair of piers. Returns the inlay's light; `base` gets the
 * stone colour.
 */
vec3 floorDesign(vec2 q, float z, float zl, float ringPulse, out vec3 base) {
    float ax = abs(q.x);
    float h = bayHue(z);
    base = vec3(0.012, 0.008, 0.024) + vec3(0.05, 0.045, 0.08) * veins(vec2(q.x * 2.0, z));
    vec3 e = vec3(0.0);
    // The runner: two strands braiding down the middle, a ring per half bay.
    float sA = q.x - 0.2 * sin(z * 3.14159265);
    float sB = q.x + 0.2 * sin(z * 3.14159265);
    float zh = mod(z, 1.0) - 0.5;
    float ring = abs(length(vec2(q.x, zh)) - 0.21);
    if (ax < 0.34) {
        e += neon(h + 0.1) * (inlay(sA, 0.012) + inlay(sB, 0.012));
        e += neon(h + 0.55) * inlay(ring, 0.01) * (1.0 + 2.0 * ringPulse);
        base += neon(h + 0.3) * 0.05 * step(length(vec2(q.x, zh)), 0.2);
    }
    // Borders.
    e += neon(h + 0.25) * (inlay(ax - 0.36, 0.008) + inlay(ax - 1.0, 0.008)) * 0.8;
    e += neon(h) * inlay(abs(zl) - 0.5 * MC_BAY, 0.01) * step(ax, 1.0) * 0.7;
    // The side panels: an eight-point star of tesserae in each bay.
    if (ax > 0.38 && ax < 0.98) {
        vec2 c = vec2(ax - 0.68, zl);
        float r = length(c);
        float a = atan(c.y, c.x);
        float seg = 3.14159265 / 4.0;
        float fa = abs(mod(a, seg) - 0.5 * seg);
        float star = r * cos(fa) - mix(0.28, 0.16, fa / (0.5 * seg)) * 0.95;
        vec2 tess = floor(vec2(q.x, z) / 0.045);
        float jitter = hash2(tess);
        vec2 tf = fract(vec2(q.x, z) / 0.045);
        float grout = step(0.12, tf.x) * step(0.12, tf.y);
        if (star < 0.0) {
            base = mix(base, neon(h + 0.5 + 0.25 * jitter) * (0.25 + 0.35 * jitter) * grout, 0.9);
            e += neon(h + 0.5) * 0.18 * grout * jitter * jitter;
        }
        e += neon(h + 0.7) * inlay(star, 0.008);
        e += neon(h + 0.15) * inlay(r - 0.06, 0.006) * 1.5;
    }
    return e;
}

/**
 * The light shafts' density at p: planes slanting down from the left-hand
 * windows across the nave, soft-edged, inside the nave only. `motes` lights
 * dust in them.
 */
float shafts(vec3 p, float motes) {
    vec2 q = p.xy - naveAxis(p.z);
    float s = p.z + 0.55 * (q.y - 1.3) + 0.35 * (q.x + 0.9);
    float z = mod(s, MC_BAY) - 0.5 * MC_BAY;
    float beam = exp(-z * z * 9.0) * smoothstep(MC_FLOOR, 1.2, q.y + 0.4) * smoothstep(0.05, -0.35, naveSd(q));
    // Dust: one soft round mote in a few cells of a fine grid.
    vec3 cell = floor(p * 12.0);
    vec3 f = fract(p * 12.0) - 0.5;
    float dust = step(0.97, hash3(cell)) * exp(-dot(f, f) * 60.0) * motes;
    return beam * (1.0 + 8.0 * dust);
}

/** The colour of the shaft through p: the glass of the window it fell through. */
vec3 shaftColour(vec3 p) {
    vec2 q = p.xy - naveAxis(p.z);
    float s = p.z + 0.55 * (q.y - 1.3) + 0.35 * (q.x + 0.9);
    return mix(vec3(1.0), neon(bayHue(s + MC_BAY * 0.5) + 0.3), 0.7);
}

/** The fractal's surface: iridescent glass-chrome with orbit filigree. */
vec3 fractalColour(vec3 rd, vec3 n, vec4 tr, float filGain) {
    float cosV = max(dot(n, -rd), 0.0);
    float fres = 0.08 + 0.92 * pow(1.0 - cosV, 4.0);
    // Thin film through the ramp, kept to its cool half by a triangle wave:
    // dark orange reads as brown, so the warm end stays for the filigree.
    float film = 1.0 + 2.5 * tr.w + 1.2 * tr.y;
    vec3 irid = neon(0.5 * abs(2.0 * fract(film * (0.5 + cosV) * 0.45) - 1.0));
    vec3 surf = vec3(0.02, 0.01, 0.04) + irid * (0.15 + 0.5 * fres);
    vec3 fil = neon(0.15 + tr.w * 1.2) * exp(-tr.x * 30.0) + neon(0.55 + tr.x) * exp(-tr.w * 22.0);
    return surf + fil * filGain * 0.8;
}

/** Glass-chrome stone: dark, a thin film over the view angle, the vault's neon reflected. */
vec3 stoneColour(vec3 rd, vec3 n, float y, float occ) {
    float cosV = max(dot(n, -rd), 0.0);
    float fres = 0.06 + 0.94 * pow(1.0 - cosV, 5.0);
    vec3 refl = reflect(rd, n);
    vec3 irid = neon(0.35 + 0.25 * abs(2.0 * fract(1.7 * (0.5 + cosV) + 0.3 * y) - 1.0));
    vec3 env = neon(0.1 + 0.2 * refl.y) * (0.2 + 0.8 * smoothstep(-0.3, 1.0, refl.y));
    return (vec3(0.025, 0.012, 0.05) + irid * 0.12 + env * fres * 0.9) * occ;
}

void main() {
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, MC_TILE);

    float form = neonForm();
    float weight = neonWeight();
    float surge = neonSurge();
    float build = neonBuild();
    float quiet = neonQuiet();
    float glowGain = neonGlow();
    float awake = 1.0 - 0.75 * quiet;

    gScale = mix(-1.55, -2.05, form) - 0.08 * weight + 0.02 * sin(uTime * 0.13);
    gArchC = mix(0.55, 1.1, form);
    gArcadeC = mix(0.25, 0.55, form);
    gSway = 0.55 * neonFlow();
    gTurn = 0.35 * neonChaos();
    gTone = neonKeyTurn() - 0.08;
    // A hard cut re-seeds the offset; the held envelope eases the old seed
    // into the new one, so the aisles re-grow instead of jumping.
    float seed = floor(SND_CUT_COUNT + 0.5);
    float ease = smoothstep(0.0, 1.0, 1.0 - clamp(SND_HELD_HARD_CUT, 0.0, 1.0));
    vec3 offA = vec3(1.0) + 0.18 * sin(vec3(1.3, 2.1, 2.9) * (seed - 1.0));
    vec3 offB = vec3(1.0) + 0.18 * sin(vec3(1.3, 2.1, 2.9) * seed);
    gOffset = mix(offA, offB, ease) + vec3(0.3 * neonFlow(), 0.0, 0.0);

    // Keep the eye in the middle of the nave, whatever the rig's wander.
    vec2 axis0 = naveAxis(ro.z);
    vec2 rel = clamp(ro.xy - axis0, vec2(-0.45, MC_FLOOR + 0.55), vec2(0.45, 0.9));
    ro.xy = axis0 + rel;

    float kick = neonKick();
    gRing = vec2(ro.z + 2.0 + (1.0 - kick) * 16.0, kick * awake);
    float snare = 1.6 * sndShaped(clamp(SND_SNARE_ENV, 0.0, 1.0)) * awake;
    float hot = 0.2 * clamp(SND_TENSION, 0.0, 1.0);
    float flicker = 1.0 - neonChaos() * 0.5 * step(0.5, hash2(vec2(floor(uTime * 14.0), 3.0)));
    float ribBase = (0.55 + 1.6 * surge) * glowGain * awake * flicker;
    float glassGain = (0.6 + 0.8 * build + 1.2 * surge) * glowGain * (1.0 - 0.6 * quiet);
    float shaftGain = (0.45 + 0.8 * build + 0.6 * surge) * awake;
    float filGain = (0.5 + 1.2 * surge) * glowGain * awake;
    float stain = SND_CHORD_HUE * 0.3;
    float motes = neonSparkle();

    float t = 0.02;
    bool hit = false;
    int steps = 0;
    vec3 glow = vec3(0.0);
    vec3 air = vec3(0.0);
    for (int i = 0; i < MC_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * t;
        float d = map(p);
        steps = i;
        // The ribs' glow in the air around them.
        glow += ribColour(p.z, gRibKind, hot) * ribLight(p.z, gRibKind, ribBase, snare) / (1.0 + gRibD * gRibD * 900.0) * exp(-t * 0.07);
        if (d < 0.0012 * (1.0 + t)) {
            hit = true;
            break;
        }
        float stepLen = d * 0.85;
        air += shaftColour(p) * shafts(p, motes * smoothstep(1.0, 3.0, t)) * min(stepLen, 0.5) * exp(-t * 0.16);
        t += stepLen;
        if (t > MC_FAR) break;
    }

    // The altar: the haze brightens toward the nave's axis ahead, in the
    // colour of the chord being played.
    vec3 altarCol = mix(vec3(1.0, 0.86, 0.96), neon(0.22 + stain), 0.35);
    float ahead = pow(max(rd.z, 0.0), 8.0);
    vec3 fogCol = vec3(0.035, 0.008, 0.08) + altarCol * ahead * (0.3 + 0.6 * build + 0.45 * surge) * awake;
    // The great rose at the end of the nave, at infinity, glowing through the
    // haze: sixteen petals and three rings, turning slowly, flaring on the beat.
    if (rd.z > 0.0) {
        vec2 rp = rd.xy / rd.z * 4.0 - vec2(0.0, 0.25);
        float rr = length(rp);
        float ra = atan(rp.y, rp.x) + uTime * 0.03;
        float petal = abs(fract(ra / MC_TAU * 16.0) - 0.5);
        float rose = inlay(rr - 0.55, 0.02) + inlay(rr - 0.35, 0.015) + inlay(rr - 0.12, 0.02)
            + inlay(petal * rr * 2.0 - 0.02, 0.01) * step(rr, 0.55) * step(0.12, rr);
        float beat = 0.7 + 0.6 * kick + 1.2 * surge;
        fogCol += neon(0.3 + rr * 0.6 + stain) * rose * smoothstep(0.75, 0.2, rr) * 0.6 * beat * awake * glowGain;
    }
    vec3 col = fogCol;
    if (hit) {
        vec3 p = ro + rd * t;
        vec3 n = calcNormal(p, t);
        map(p);
        float mat = gMat;
        vec2 q = p.xy - naveAxis(p.z);
        float zl = mod(p.z, MC_BAY) - 0.5 * MC_BAY;
        float occ = clamp(1.0 - float(steps) / 100.0, 0.2, 1.0);
        if (mat == MAT_FLOOR) {
            // Polished stone: dark, a neon runner down the middle, and the
            // church mirrored in it by a short second march.
            vec3 base;
            float ringPulse = gRing.y * exp(-abs(p.z - gRing.x) * 1.2);
            vec3 inl = floorDesign(q, p.z, zl, ringPulse, base);
            // Pools of coloured light where the shafts reach the floor.
            vec3 fp = p + vec3(0.0, 0.05, 0.0);
            vec3 pool = shaftColour(fp) * shafts(fp, 0.0) * shaftGain;
            col = base * (1.0 + 4.0 * pool) + inl * 0.55 * glowGain * awake + pool * 0.12;
            vec3 rr = reflect(rd, vec3(0.0, 1.0, 0.0));
            float rt = 0.05;
            vec3 rcol = fogCol * 0.4;
            for (int j = 0; j < MC_REFL_STEPS; j++) {
                vec3 rp = p + rr * rt;
                float rd2 = map(rp);
                if (rd2 < 0.004 * (1.0 + rt)) {
                    if (gMat == MAT_RIB) rcol += ribColour(rp.z, gRibKind, hot) * ribLight(rp.z, gRibKind, ribBase, snare);
                    break;
                }
                rt += rd2 * 0.9;
                if (rt > MC_FAR * 0.5) break;
            }
            float fres = 0.04 + 0.96 * pow(1.0 - max(-rd.y, 0.0), 5.0);
            col += rcol * mix(0.18, 0.5, fres) * exp(-rt * 0.06);
        } else if (mat == MAT_RIB) {
            // A neon tube: a hot core over a coloured sheath.
            float core = pow(max(dot(n, -rd), 0.0), 2.0);
            vec3 c = ribColour(p.z, gRibKind, hot);
            col = c * c * ribLight(p.z, gRibKind, ribBase, snare) * (0.8 + 1.8 * core) + c * core * 0.3 * ribBase;
        } else if (mat == MAT_FRACTAL) {
            col = fractalColour(rd, n, gTrap, filGain) * occ;
        } else {
            col = stoneColour(rd, n, q.y, occ);
            float ax = abs(q.x);
            float h = bayHue(p.z);
            bool pierHit = abs(abs(zl) - 0.5 * MC_BAY) < 0.3 && ax < MC_HALF_W + 0.05;
            if (pierHit) {
                // Veined marble shafts, a gilded capital at the springing line, a lit plinth.
                col += vec3(0.08, 0.07, 0.12) * veins(vec2(atan(n.x, n.z) * 2.0, q.y * 3.0)) * occ;
                float cap = inlay(q.y - MC_SPRING, 0.025) + 0.6 * inlay(q.y - MC_SPRING + 0.08, 0.012);
                col += neon(h + 0.6) * cap * 0.9 * glowGain * awake;
                col += neon(h + 0.6) * inlay(q.y - MC_FLOOR - 0.12, 0.02) * 0.5 * glowGain * awake;
            } else if (ax > MC_HALF_W - 0.03) {
                // Ashlar courses, staggered.
                float course = floor(q.y / 0.22);
                float jz = mod(p.z + 0.22 * mod(course, 2.0), 0.44);
                float joint = min(inlay(fract(q.y / 0.22), 0.03) + inlay(jz, 0.012), 1.0);
                col *= 1.0 - 0.55 * joint;
                // Blind tracery under the windows: a row of small pointed arches.
                float sub = mod(zl + 0.22, 0.44) - 0.22;
                float blind = arch2(vec2(sub, q.y), 0.17, 0.07, 0.45);
                float band = step(0.3, q.y) * step(q.y, 0.78);
                col += neon(h + 0.35) * inlay(blind, 0.01) * band * 0.7 * glowGain * awake;
            }
            // Stained glass in the clerestory: the window's own light.
            float win = windowSd(zl, q.y);
            if (win < 0.0 && abs(q.x) > MC_HALF_W - 0.4) {
                vec2 w = vec2(zl, q.y - 1.2) / 0.34;
                float id = floor(p.z / MC_BAY) + (q.x > 0.0 ? 101.0 : 0.0);
                col = glass(w, stain + bayHue(p.z), mod(id, 8.0)) * glassGain * 1.8;
            } else if (win < 0.03) {
                col *= 0.4;
            }
        }
        float fog = 1.0 - exp(-t * 0.06);
        col = mix(col, fogCol, fog);
    }
    col += glow * 0.02 * (1.0 + surge);
    col += air * 0.32 * shaftGain;

    fragColor = vec4(neonFinish(col, 1.15), 1.0);
}
