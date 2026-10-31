// The Dive family: a raymarched fly-through that dives down an endless
// corridor through one of five repeating scenes - a beam lattice, faceted
// jewel chambers, a mushroom-forest tunnel, cells of turning gyroscopes and a
// kaleidoscopic mandala tunnel - on one of five camera paths, coloured by six
// cosine palettes that switch with the music.
//
// The design is the user's own shader. Its header, mapped onto this engine:
//
//   PARALLAX_MODE  0 auto-cycle, 1 straight, 2 serpent, 3 helix, 4 breathing
//                  accordion, 5 orbit look-around. Here the DIVE_CAM_* ids
//                  keep that numbering; dive_show is mode 0.
//   SCENE_SECONDS  13 s per scene: DIVE_SCENE_SECONDS, on the Speed-scaled
//                  clock, so Speed also hurries the cycle.
//   BEAT_Z, DROP_Z The original kept its own spectral-flux history of the
//                  45-140 Hz band and z-scored it for kicks (1.7) and palette-
//                  switching drops (3.5). A style here is one stateless pass:
//                  it cannot remember last frame's spectrum. The app already
//                  runs exactly that kind of detector on the CPU, so the kick
//                  is SND_KICK_ENV (the normalised kick level), the kick toggle
//                  SND_KICK_TOGGLE, the drop envelope SND_HELD_SURGE, and the counters
//                  SND_ONSET_COUNT and SND_CUT_COUNT. Their thresholds are the app's
//                  (Reactivity tab), not a number in this file.
//   PAL_COOLDOWN,  A palette switch needs memory (when did we last switch?),
//   PAL_MAX_HOLD   which a stateless shader has not got. The counters stand in
//                  for it: the palette slot is a function of
//                  SND_CUT_COUNT + floor(SND_ONSET_COUNT / DIVE_PAL_ONSETS), so it
//                  changes on every section change (where drops land) and
//                  after every DIVE_PAL_ONSETS onsets otherwise - at ordinary
//                  tempos a few seconds, which is what cooldown plus max-hold
//                  amounted to. See dive_palSlot for why two neighbouring
//                  slots never pick the same palette.
//   QUALITY        The Detail control: uSteps < 80 is "fast" - a coarser hit
//                  epsilon and a longer stride - and otherwise "full".
//
// WHY THE WORLD IS PERIODIC IN 256
//
// The rig's travel wraps at 8192 world units. Every scene here repeats along
// z with a period that divides 256 (cells of 4 or 2, per-cell hashes taken
// mod 64 cells), and every camera path's wobble repeats in 16 or 32. So the
// dive runs in LOCAL coordinates, diveTravel() = mod(2 * travel, 256): the
// wrap is invisible (2 * 8192 is 64 periods), and the march works near the
// origin, where a float still resolves a tenth of a millimetre. Marching at
// z = 8000 leaves about 1e-3 of precision, coarser than the normal epsilon,
// and the surfaces go grainy half an hour into a session. The factor 2 is a
// dive rate: the rig's default Fly speed is a stroll for a scene this dense.
//
// The same rule binds colour: nothing is coloured by absolute z (it jumps at
// the 256 wrap). Colour reads per-cell hashes, which wrap with the cells, or
// depth relative to the eye.
//
// WHY THE SCENES MIRROR
//
// Every scene is folded into ONE cell by p - s * round(p / s) and drawn with
// a field that is symmetric across the cell's walls, so the two sides of a
// cell border evaluate the same surface and the cells meet without a crack.
// The beam lattice and the gyroscope cells are offset by half a cell so the
// dive axis runs down the open channel between them, never through a beam.
// Angular repetition (mushroom rings, the mandala) uses WHOLE fold counts
// only: atan jumps by 2*pi at the negative x axis, and an integer count of
// wedges makes that jump a whole number of wedges, so the seam lands on a
// wedge border the fold already treats as a mirror. The mandala's fold count
// steps with Complexity and crossfades across a short band at each step
// rather than popping a wedge in.
//
// WHY ONE MARCHER
//
// Unlike lib_bulb, all five maps live here, so the march, the normal and the
// shading can too: diveMap() picks the scene from gDiveScene, a value that is
// the same for every pixel of a frame, so the branch is coherent and costs
// one scene per step. The camera, the accordion remap and the Chaos warp are
// applied in diveMap() so every style marches the same world.
//
// THE CAMERA. diveCamera() builds the eye from its own path (straight,
// serpent, helix, accordion, orbit) and looks along the path's tangent; the
// rig's look (uCamBasis: yaw, pitch, roll, bank, shake, FOV) is then applied
// ON TOP, by reading camRayDir() as a ray in the path's frame. Travel is the
// rig's (uCamTravel), and the dolly punches the eye forward along the path.
// The helix camera ROLLS, but by a bounded oscillation that comes back, never
// a turn that keeps going: a continuously spinning frame is the one motion
// this app never puts on screen. A small bubble round the eye is carved out
// of every scene, so a path that brushes a cap or a ring never parks the
// lens inside geometry.
//
// ---- the Customize macros ---------------------------------------------------
//
// Speed: uSpeed scales the animation clock (diveTime), the gyroscopes, the
// orbit swing and the scene cycle; the dive itself runs on the rig's Fly.
// Complexity: uMorph adds a layer to every scene - cross-braces in the
// lattice, a second ring of star facets on the jewels, a second, smaller
// ring of mushrooms, a fourth gyroscope ring, more mandala folds.
// Chaos: uWarp and uTurbulence bend space before the scene (diveChaos, the
// estimate divided by its stretch); uTwist stays the screen twist in view().
// Energy: diveEnergy() is the gain on every in-shader reaction, 0..2.
// Colour: diveColor() is half the user's cosine palettes, half pal(), so the
// Colour controls always move the picture. Space: the rig.
// At defaults (uSpeed 1, uMorph 0, chaos 0, uIntensity 1) each is identity.
//
// INCLUDE ORDER: last, after lib_scene_uniforms, lib_scene_motion,
// lib_camera, lib_palette and lib_scene_grade. It reads uSteps,
// uMorph, uWarp, uTurbulence, uSpeed, uIntensity, sndShaped(SND_MIDDLE), uFlowPhase,
// the lib_sound events, the rig and vUv (diveIris), and calls pal().
// It includes nothing itself and defines its own small helpers (dive_*), so
// it does not depend on lib_sdf3.

#define DIVE_TAU 6.2831853
/** Compile-time march ceiling; the user's budget (uSteps) breaks below it. */
#define DIVE_MAX_STEPS 128
/** Cell size of the lattice, jewel and gyroscope scenes, along every repeated axis. */
#define DIVE_CELL 4.0
/** The local world's period along z (see the header). */
#define DIVE_WORLD 256.0
#define DIVE_FAR 36.0
#define DIVE_EYE_R 0.42
#define DIVE_SCENE_SECONDS 13.0
/** Onsets per palette switch when no section change comes first. */
#define DIVE_PAL_ONSETS 16.0

#define DIVE_LATTICE 0
#define DIVE_JEWEL 1
#define DIVE_MUSHROOM 2
#define DIVE_GYRO 3
#define DIVE_MANDALA 4

// The user's PARALLAX_MODE numbering.
#define DIVE_CAM_STRAIGHT 1
#define DIVE_CAM_SERPENT 2
#define DIVE_CAM_HELIX 3
#define DIVE_CAM_ACCORDION 4
#define DIVE_CAM_ORBIT 5

// ---- small helpers ----------------------------------------------------------

mat2 dive_rot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

// Hashes from fract() and products, no sine: sin() of a large argument loses
// its fraction differently per GPU, so a sine hash shimmers between devices.
float dive_hash11(float p) {
    vec2 q = fract(vec2(p * 0.1546, p * 0.3721) + vec2(0.29, 0.83));
    q += dot(q, q.yx + 23.17);
    return fract(q.x * q.y);
}

float dive_hash21(vec2 p) {
    vec3 q = fract(p.xyx * vec3(0.1546, 0.3721, 0.2917));
    q += dot(q, q.zyx + 23.17);
    return fract((q.x + q.y) * q.z);
}

float dive_hash31(vec3 p) {
    p = fract(p * vec3(0.1546, 0.3721, 0.2917));
    p += dot(p, p.zyx + 23.17);
    return fract((p.x + p.y) * p.z);
}

float dive_smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

/** A 2D box of half extents b: the cross-section of a beam or a ring band. */
float dive_box2(vec2 v, vec2 b) {
    vec2 d = abs(v) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

// ---- the macros -------------------------------------------------------------

float diveTime() {
    return uTime * uSpeed;
}

float diveEnergy() {
    return clamp(uIntensity, 0.0, 2.0);
}

float diveMorph() {
    return clamp(uMorph, 0.0, 1.0);
}

/** Dive distance in local coordinates: the rig's travel at twice its rate, plus the dolly punch, wrapped (see the header). */
float diveTravel() {
    return mod(2.0 * uCamTravel + 0.8 * uCamDolly, DIVE_WORLD);
}

// ---- the palettes -----------------------------------------------------------
//
// The user's six cosine palettes, a + b * cos(TAU * (c * x + d)), values
// unchanged. Tuned around the reference videos; keep them.
const vec3 DIVE_PA[6] = vec3[6](vec3(.50, .50, .50), vec3(.55, .30, .75), vec3(.45, .60, .35), vec3(.65, .35, .40), vec3(.40, .55, .80), vec3(.45, .45, .35));
const vec3 DIVE_PB[6] = vec3[6](vec3(.50, .50, .50), vec3(.45, .35, .35), vec3(.50, .45, .50), vec3(.40, .30, .40), vec3(.40, .40, .30), vec3(.50, .45, .45));
const vec3 DIVE_PC[6] = vec3[6](vec3(1.0, 1.0, 1.0), vec3(1.0, 1.0, 1.0), vec3(1.0, 1.0, .50), vec3(1.0, .70, .40), vec3(1.0, 1.0, 1.0), vec3(1.0, 1.0, 1.0));
const vec3 DIVE_PD[6] = vec3[6](vec3(.00, .33, .67), vec3(.70, .85, .10), vec3(.80, .90, .30), vec3(.00, .15, .20), vec3(.00, .10, .20), vec3(.30, .20, .55));

vec3 dive_cpal(float x, int i) {
    return DIVE_PA[i] + DIVE_PB[i] * cos(DIVE_TAU * (DIVE_PC[i] * x + DIVE_PD[i]));
}

int gDivePalNew;
int gDivePalOld;
float gDivePalFade;

/**
 * The palette slot for switch number c: c + 2 * (0 or 1, hashed), mod 6. Two
 * neighbouring slots differ by 1 + 2 * (difference of two bits), that is -1,
 * 1 or 3 - never 0 mod 6 - so every switch really changes the palette, which
 * a plain hash of c could not promise.
 */
int dive_palSlot(float c) {
    float s = mod(c + 2.0 * floor(2.0 * dive_hash11(c * 1.37 + 0.5)), 6.0);
    return int(min(floor(s + 0.5), 5.0));
}

/**
 * Picks the active palette and the one it is fading from. A switch caused by
 * the Nth onset fades over that onset's own envelope (SND_ONSET_ENV, 0.22 s); one
 * caused by a section change over the section envelope (SND_HELD_HARD_CUT, 1.5 s).
 * The onset fade is gated to the onset that tipped the counter, or every
 * onset would flick back to the old palette for a moment.
 */
void dive_palInit() {
    float c = SND_CUT_COUNT + floor(SND_ONSET_COUNT / DIVE_PAL_ONSETS);
    gDivePalNew = dive_palSlot(c);
    gDivePalOld = dive_palSlot(c - 1.0);
    float onsetFade = mod(SND_ONSET_COUNT, DIVE_PAL_ONSETS) < 0.5 ? clamp(SND_ONSET_ENV, 0.0, 1.0) : 0.0;
    float fade = max(onsetFade, clamp(SND_HELD_HARD_CUT, 0.0, 1.0));
    gDivePalFade = smoothstep(0.0, 1.0, fade);
}

/** The family's colour: the active cosine palette (crossfading from the last one) half and half with pal(). */
vec3 diveColor(float x) {
    vec3 c = mix(dive_cpal(x, gDivePalNew), dive_cpal(x, gDivePalOld), gDivePalFade);
    return mix(max(c, 0.0), pal(x), 0.5);
}

// ---- per-frame scene state ----------------------------------------------------

int gDiveScene;
vec3 gDiveEye;
/** Accordion: z is scaled about the eye by this (1 = off). */
float gDiveBreath;
float gDiveKick;
float gDiveBeam;
float gDiveSpin;
float gDiveFolds;
float gDivePetal;
float gDiveChaosAmp;
float gDiveChaosT;
float gDiveSway;
float gDiveHelixR;
float gDiveSwing;

// What the last map() call found. Copy them right after the hit: the normal's
// four taps overwrite them.
/** Part: 0 the structure (beam, wall, ring 1), 1 a feature (node, crystal, cap, petal), 2 an accent (brace, stem, bead). */
float gDiveMat;
/** 0..1, a stable hash of the cell or part, for per-part colour. */
float gDiveId;
float gDiveHitMat;
float gDiveHitId;

/**
 * Sets every default for `scene` and picks the palette. Call once at the top
 * of main(), then override what the style wants (gDiveBreath, gDiveSway...).
 */
void diveSetup(int scene) {
    float e = diveEnergy();
    dive_palInit();
    gDiveScene = scene;
    gDiveBreath = 1.0;
    gDiveKick = clamp(SND_KICK_ENV * e, 0.0, 1.0);
    gDiveBeam = 0.13 + 0.05 * gDiveKick;
    // Loudness sets the base rate (uFlowPhase only ever advances); mids push
    // the rings ahead while they climb. sndShaped(SND_MIDDLE) has its own attack and release, so no jump.
    gDiveSpin = diveTime() * 0.45 + uFlowPhase * 0.6 + 1.6 * clamp(sndShaped(SND_MIDDLE), 0.0, 1.5) * e;
    gDiveFolds = 6.0 + 6.0 * diveMorph();
    gDivePetal = 1.0 + 0.3 * clamp(SND_KICK_TOGGLE * e, 0.0, 1.0);
    gDiveChaosAmp = 0.06 * clamp(uWarp, 0.0, 2.0) + 0.04 * clamp(uTurbulence, 0.0, 2.0);
    gDiveChaosT = diveTime() * 0.5;
    gDiveSway = 1.0;
    gDiveHelixR = 0.6;
    gDiveSwing = 0.7;
    gDiveMat = 0.0;
    gDiveId = 0.0;
}

// ---- the five scenes ------------------------------------------------------------

/** Beam lattice: square beams along all three axes, an octahedral knuckle at every crossing. */
float diveLattice(vec3 p) {
    vec3 q = p + vec3(0.5 * DIVE_CELL, 0.5 * DIVE_CELL, 0.0);
    vec3 c = round(q / DIVE_CELL);
    vec3 r = q - DIVE_CELL * c;
    vec3 a = abs(r);
    float w = gDiveBeam;
    vec2 bw = vec2(w - 0.02);
    float bx = dive_box2(a.yz, bw);
    float by = dive_box2(a.xz, bw);
    float bz = dive_box2(a.xy, bw);
    float beams = min(bx, min(by, bz)) - 0.02;
    float node = (a.x + a.y + a.z - 4.2 * w) * 0.57735;
    float d = beams;
    gDiveMat = 0.0;
    gDiveId = bx < min(by, bz) ? 0.1 : (by < bz ? 0.35 : 0.6);
    if (node < d) {
        d = node;
        gDiveMat = 1.0;
        gDiveId = dive_hash31(mod(c, 64.0));
    }
    // Complexity: X braces in the two faces that run along the dive. The
    // third face (the cross-section) would brace straight across the axis.
    float m = diveMorph();
    if (m > 0.01) {
        float wb = 0.07 * m;
        float br = min(length(vec2(r.x, (a.y - a.z) * 0.7071)), length(vec2(r.y, (a.x - a.z) * 0.7071))) - wb;
        if (br < d) {
            d = br;
            gDiveMat = 2.0;
            gDiveId = 0.8;
        }
    }
    return d;
}

/**
 * Jewel chambers: a cavity per cell along z, cut like a gem - an octagonal
 * girdle, crown facets tapering to a table at each end, star facets on the
 * corners - joined by a round doorway, with a crystal set in every facet
 * wedge of the girdle. The 8-fold symmetry is folded with reflections
 * (abs and a swap), so there is no atan and no seam.
 */
float diveJewel(vec3 p) {
    float zc = round(p.z / DIVE_CELL);
    vec3 q = vec3(p.xy, p.z - DIVE_CELL * zc);
    float cid = mod(zc, 64.0);
    // Every other chamber turned half a facet: the facets do not line up
    // down the tunnel. The turn is hidden in the door plate, which is round.
    q.xy = dive_rot(mod(zc, 2.0) * DIVE_TAU / 16.0) * q.xy;
    float wedge = (q.x < 0.0 ? 1.0 : 0.0) + (q.y < 0.0 ? 2.0 : 0.0);
    vec2 f = abs(q.xy);
    if (f.x < f.y) {
        f = f.yx;
        wedge += 4.0;
    }
    float z = abs(q.z);
    const vec2 G = vec2(0.9238795, 0.3826834);
    float m = diveMorph();
    float cav = dot(f, G) - 2.2;
    float facet = 0.0;
    float crown = (dot(f, G) + 0.74 * z - 2.57) * inversesqrt(1.5476);
    if (crown > cav) { cav = crown; facet = 1.0; }
    float star = (f.x + z - 2.55) * 0.7071;
    if (star > cav) { cav = star; facet = 2.0; }
    float star45 = (dot(f, vec2(0.7071)) + z - 2.5 - 3.0 * (1.0 - m)) * 0.7071;
    if (star45 > cav) { cav = star45; facet = 3.0; }
    float table = z - 1.85;
    if (table > cav) { cav = table; facet = 4.0; }
    float d = max(-cav, 1.25 - length(q.xy));
    gDiveMat = 0.0;
    gDiveId = dive_hash11(cid * 61.0 + wedge * 7.0 + facet);
    vec3 o = abs(vec3(f - G * 2.05, q.z));
    float crystal = (o.x + o.y + o.z - 0.42) * 0.57735;
    if (crystal < d) {
        d = crystal;
        gDiveMat = 1.0;
        gDiveId = dive_hash11(cid * 61.0 + wedge * 7.0 + 9.0);
    }
    return d;
}

/**
 * Mushroom forest lining a tunnel: eight mushrooms round the wall per ring,
 * a ring every 2 units, alternate sectors staggered by half a ring. Each
 * grows inward from the wall: a tapering stem and a domed cap that the kick
 * swells (gDiveKick). Sizes are kept inside the sector so a mushroom never
 * crosses into its neighbour's wedge.
 */
float diveMushrooms(vec3 p) {
    const float N = 8.0;
    const float RT = 2.6;
    float w = DIVE_TAU / N;
    float r = length(p.xy);
    float ang = atan(p.y, p.x);
    float sector = round(ang / w);
    float sid = mod(sector, N);
    vec2 l = dive_rot(-sector * w) * p.xy;
    float zs = p.z + mod(sid, 2.0);
    float zc = round(zs * 0.5);
    float lz = zs - 2.0 * zc;
    float h = dive_hash21(vec2(sid, mod(zc, 64.0)));
    // m: x across the sector, y up from the wall toward the axis, z along.
    vec3 m = vec3(l.y, RT - l.x, lz);
    float H = 0.6 + 0.45 * h;
    float yy = clamp(m.y, 0.0, H);
    float stem = length(vec3(m.x, m.y - yy, m.z)) - (0.07 + 0.05 * (1.0 - yy / H));
    float cr = (0.30 + 0.10 * h) * (1.0 + 0.3 * gDiveKick);
    float cap = (length(vec3(m.x, (m.y - H) * 1.8, m.z)) - cr) / 1.8;
    cap = max(cap, H - 0.03 - m.y);
    // The wall: integer multiples of the angle only, so no seam at atan's jump.
    float wall = RT - r + 0.05 * sin(3.0 * N * ang) * sin(p.z * 3.1415927);
    float d = dive_smin(wall, stem, 0.25);
    gDiveMat = d < wall - 0.01 ? 2.0 : 0.0;
    gDiveId = h;
    if (cap < d) {
        d = cap;
        gDiveMat = 1.0;
    }
    // Complexity: a second, smaller mushroom between each pair along the ring.
    float mo = diveMorph();
    if (mo > 0.01) {
        vec3 m2 = vec3(m.x, m.y, 1.0 - abs(m.z));
        float y2 = clamp(m2.y, 0.0, 0.35);
        float st2 = length(vec3(m2.x, m2.y - y2, m2.z)) - 0.045;
        float cp2 = max((length(vec3(m2.x, (m2.y - 0.35) * 1.8, m2.z)) - 0.22 * mo) / 1.8, 0.33 - m2.y);
        d = dive_smin(d, st2, 0.1);
        if (cp2 < d) {
            d = cp2;
            gDiveMat = 1.0;
            gDiveId = fract(h + 0.5);
        }
    }
    return d;
}

/** One band of a gyroscope: a flat ring of radius R in the xy plane. */
float dive_ring(vec3 v, float R, vec2 band) {
    return dive_box2(vec2(length(v.xy) - R, v.z), band);
}

/**
 * Gyroscope cells: three nested rings at every cell centre, each turning
 * inside the last like a gimbal, all driven by gDiveSpin with a per-cell
 * phase and direction. The cells sit half a cell off the dive axis, so the
 * nearest one is 2.8 units from the eye and the outer ring (1.5) never
 * reaches the path. Rotations are rigid: the estimate stays exact.
 */
float diveGyro(vec3 p) {
    vec3 q = p + vec3(0.5 * DIVE_CELL, 0.5 * DIVE_CELL, 0.0);
    vec3 c = round(q / DIVE_CELL);
    vec3 r = q - DIVE_CELL * c;
    float h = dive_hash31(mod(c, 64.0));
    float ph = gDiveSpin * (h > 0.5 ? 1.0 : -1.0) + h * DIVE_TAU;
    r.yz = dive_rot(h * 2.4 - 1.2) * r.yz;
    r.xz = dive_rot(ph) * r.xz;
    float d = dive_ring(r, 1.45, vec2(0.035, 0.11));
    gDiveMat = 0.0;
    gDiveId = h;
    r.yz = dive_rot(ph * 1.37) * r.yz;
    float d2 = dive_ring(r, 1.08, vec2(0.03, 0.09));
    if (d2 < d) { d = d2; gDiveMat = 1.0; gDiveId = fract(h + 0.25); }
    r.xz = dive_rot(ph * 1.83) * r.xz;
    float d3 = dive_ring(r, 0.72, vec2(0.028, 0.08));
    if (d3 < d) { d = d3; gDiveMat = 1.0; gDiveId = fract(h + 0.5); }
    float m = diveMorph();
    r.yz = dive_rot(ph * 2.3) * r.yz;
    float d4 = dive_ring(r, 0.44, vec2(0.025, 0.07 * m + 0.001));
    float core = length(r) - 0.16 - 0.1 * m;
    d4 = min(d4, core);
    if (d4 < d) { d = d4; gDiveMat = 2.0; gDiveId = fract(h + 0.75); }
    return d;
}

/** The mandala tunnel at a whole fold count n: wall, petals, beads, rays. */
float dive_mandalaN(vec3 p, float n) {
    const float R = 2.6;
    float w = DIVE_TAU / n;
    float r = length(p.xy);
    // A whole number of wedges: atan's 2*pi jump is n wedges, invisible.
    float a = abs(mod(atan(p.y, p.x) + 0.5 * w, w) - 0.5 * w);
    vec2 f = r * vec2(cos(a), sin(a));
    float lz = p.z - 2.0 * round(p.z * 0.5);
    float az = abs(lz);
    float wall = R - r;
    // Ridge rings round the wall on every period border.
    wall = dive_smin(wall, length(vec2(r - (R - 0.02), 1.0 - az)) - 0.08, 0.1);
    vec3 rad = vec3(0.55, 0.26, 0.55) * gDivePetal;
    vec3 pe = vec3(f.x - (R - 0.12), f.y, lz);
    float petal = (length(pe / rad) - 1.0) * min(rad.y, rad.x);
    vec2 E = vec2(cos(0.5 * w), sin(0.5 * w));
    float bead = length(vec3(f - E * (R - 0.6), 1.0 - az)) - 0.14;
    float along = clamp(dot(f, E), 1.55, R);
    float ray = length(vec3(f - E * along, lz)) - 0.03;
    float d = dive_smin(wall, petal, 0.15);
    gDiveMat = petal < wall ? 1.0 : 0.0;
    gDiveId = clamp(0.5 + 0.4 * (f.x - 2.0) + 0.3 * az, 0.0, 1.0);
    float acc = min(bead, ray);
    if (acc < d) {
        d = acc;
        gDiveMat = 2.0;
        gDiveId = bead < ray ? 0.85 : 0.6;
    }
    return d;
}

/** The mandala at gDiveFolds: at a step the next fold count crossfades in over the last 15% of the step. */
float diveMandala(vec3 p) {
    float n = floor(gDiveFolds);
    float d = dive_mandalaN(p, n);
    float bl = smoothstep(0.85, 1.0, fract(gDiveFolds));
    if (bl > 0.001) {
        float mat = gDiveMat;
        float id = gDiveId;
        d = mix(d, dive_mandalaN(p, n + 1.0), bl);
        if (bl < 0.5) {
            gDiveMat = mat;
            gDiveId = id;
        }
    }
    return d;
}

// ---- the world ----------------------------------------------------------------

/** Chaos: a slow sine warp, period 4 along every axis so it survives the 256 wrap. */
vec3 diveChaos(vec3 p) {
    if (gDiveChaosAmp > 1e-4) {
        float t = gDiveChaosT;
        p += gDiveChaosAmp * sin(p.yzx * 1.5707963 + vec3(t, 1.7 + 0.7 * t, 3.1 - 0.5 * t));
    }
    return p;
}

float diveMap(vec3 p) {
    vec3 q = p;
    q.z = gDiveEye.z + (q.z - gDiveEye.z) * gDiveBreath;
    q = diveChaos(q);
    float d;
    if (gDiveScene == DIVE_LATTICE) d = diveLattice(q);
    else if (gDiveScene == DIVE_JEWEL) d = diveJewel(q);
    else if (gDiveScene == DIVE_MUSHROOM) d = diveMushrooms(q);
    else if (gDiveScene == DIVE_GYRO) d = diveGyro(q);
    else d = diveMandala(q);
    // Both remaps stretch space; divide by their bound so the march never overshoots.
    d /= max(gDiveBreath, 1.0) * (1.0 + gDiveChaosAmp * 2.83);
    return max(d, DIVE_EYE_R - length(p - gDiveEye));
}

vec3 diveNormal(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * diveMap(p + k.xyy * e) + k.yyx * diveMap(p + k.yyx * e)
                   + k.yxy * diveMap(p + k.yxy * e) + k.xxx * diveMap(p + k.xxx * e));
}

/** Hit epsilon at distance t, doubled on the fast budget (QUALITY 0). Also the normal epsilon. */
float diveEps(float t) {
    return (uSteps < 80.0 ? 0.002 : 0.001) * t + 0.0006;
}

/**
 * Marches from ro along rd. Returns the hit distance or -1, the steps spent in
 * `used`, and copies the hit's part into gDiveHitMat / gDiveHitId.
 */
float diveMarch(vec3 ro, vec3 rd, out float used) {
    float stride = uSteps < 80.0 ? 0.9 : 0.8;
    float t = 0.02;
    used = 0.0;
    for (int i = 0; i < DIVE_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        if (t > DIVE_FAR) break;
        used = float(i);
        float d = diveMap(ro + rd * t);
        float eps = diveEps(t);
        if (d < eps) {
            gDiveHitMat = gDiveMat;
            gDiveHitId = gDiveId;
            return t;
        }
        t += max(d * stride, eps * 0.5);
    }
    return -1.0;
}

/** Occlusion from the step count: a ray that crept along a crevice used most of the budget. */
float diveStepAO(float used) {
    return clamp(1.0 - used / max(uSteps, 1.0) * 1.2, 0.2, 1.0);
}

// ---- the camera -----------------------------------------------------------------

/** The eye on path `mode` at local dive distance s. Wobble periods 16 and 32 divide the 256 world. */
vec3 divePathPoint(int mode, float s) {
    if (mode == DIVE_CAM_SERPENT) {
        return vec3(gDiveSway * sin(s * DIVE_TAU / 16.0), 0.3 * gDiveSway * sin(s * DIVE_TAU / 32.0), s);
    }
    if (mode == DIVE_CAM_HELIX) {
        float a = s * DIVE_TAU / 16.0;
        return vec3(gDiveHelixR * cos(a), gDiveHelixR * sin(a), s);
    }
    return vec3(0.0, 0.0, s);
}

/**
 * The eye and ray for path `mode` (DIVE_CAM_*): ro rides the path, the frame
 * looks along its tangent, and the rig's look is applied on top. Accordion
 * flies straight (the breathing is gDiveBreath, set by the style). Orbit flies
 * straight and swings the look side to side by up to gDiveSwing radians.
 * Also sets gDiveEye, which diveMap() needs.
 */
void diveCamera(int mode, vec2 uv, out vec3 ro, out vec3 rd) {
    float s = diveTravel();
    ro = divePathPoint(mode, s);
    vec3 fwd = normalize(divePathPoint(mode, s + 1.5) - ro);
    float roll = 0.0;
    if (mode == DIVE_CAM_HELIX) roll = 0.3 * sin(s * DIVE_TAU / 16.0);
    if (mode == DIVE_CAM_ORBIT) {
        float t = diveTime();
        float yaw = gDiveSwing * sin(t * 0.31);
        float pitch = 0.3 * gDiveSwing * sin(t * 0.19 + 1.0);
        fwd = vec3(sin(yaw) * cos(pitch), sin(pitch), cos(yaw) * cos(pitch));
    }
    vec3 up = vec3(sin(roll), cos(roll), 0.0);
    vec3 right = normalize(cross(up, fwd));
    mat3 frame = mat3(right, cross(fwd, right), fwd);
    rd = normalize(frame * camRayDir(uv));
    gDiveEye = ro;
}

// ---- shading ----------------------------------------------------------------------

/** What a ray that hits nothing sees, and the fog everything fades into: dark haze brightening toward the far end of the dive. */
vec3 diveBackdrop(vec3 rd) {
    float end = pow(max(rd.z, 0.0), 6.0);
    return diveColor(0.55) * (0.04 + 0.2 * end);
}

/** A surface's base colour from its part, its id and its depth from the eye. */
vec3 diveMaterial(float mat, float id, float depth) {
    return diveColor(0.12 + 0.28 * mat + 0.35 * id + 0.015 * depth);
}

/**
 * A headlamp riding with the eye (falling off with distance), a specular
 * glint, a rim, step-count occlusion, then fog. The light moves with the
 * camera, so the dive is lit where it looks and nothing needs a shadow ray.
 */
vec3 diveShade(vec3 n, vec3 rd, float hitT, float used, vec3 base, float shine) {
    float lamp = max(dot(n, -rd), 0.0);
    float fall = 1.0 / (1.0 + 0.02 * hitT * hitT);
    float spec = pow(max(dot(reflect(rd, n), -rd), 0.0), shine);
    float rim = pow(1.0 - lamp, 3.0);
    vec3 col = base * (0.08 + 0.92 * lamp * fall) + vec3(0.35 * spec * fall) + base * rim * 0.22;
    col *= diveStepAO(used);
    return mix(diveBackdrop(rd), col, exp(-0.055 * hitT));
}

/** The show clock's segment: which scene (and in dive_show which path) is on. */
float diveSegment() {
    return floor(diveTime() / DIVE_SCENE_SECONDS);
}

/**
 * The iris between segments: 1 open, closing to 0 (dark) over the last 0.8 s
 * of a segment and reopening over the first 0.8 s of the next, so the scene
 * and the path change while the screen is dark. A darkening, never a flash.
 */
float diveIris() {
    float ph = mod(diveTime(), DIVE_SCENE_SECONDS);
    float open = smoothstep(0.0, 0.8, min(ph, DIVE_SCENE_SECONDS - ph));
    vec2 s = vUv * 2.0 - 1.0;
    s.x *= uResolution.x / uResolution.y;
    float R = open * 2.4;
    return 1.0 - smoothstep(R - 0.35, R, length(s));
}
