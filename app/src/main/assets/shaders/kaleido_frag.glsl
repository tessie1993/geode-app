#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uSoundTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_dmt

// Kaleidoscope: a flight down the inside of a real kaleidoscope.
//
// A kaleidoscope is a tube of mirrors, and what you see down it is the few
// objects at its end repeated round the axis by the reflections. This draws
// exactly that, in 3D: the cross-section of space is folded into N mirrored
// wedges (the fold IS the mirrors: every point is reflected into one wedge),
// the N flat mirrors make an N-sided prism round the flight axis, and inside
// it ride crystals that each mirror repeats into a ring. The camera flies down
// the prism on the shared rig, the prism bends along lib_dmt's flight path, so
// the tube turns left, right, up and down ahead of you, and it never ends.
//
// The crystals are fractal: a kaleidoscopic IFS (fold the space onto one
// octant, sort the axes, scale by two and pull back, three times) round an
// octahedron, the classic Knighty/Syntopia KIFS. Each cell of the tube holds
// one, seeded by the cell, so no two in a row are the same, and each GROWS IN
// out of nothing as it comes out of the distance and lives on its own
// birth-hold-dissolve clock (dmtLife), so new objects keep arriving and none
// pops. The mirror walls reflect the mandala sky (dmtChrysanthemum) and are
// engraved with a flower-of-life lattice that lights up on the kick.
//
// WHOLE FOLD COUNTS ONLY. atan jumps by 2*pi at the negative x axis; an integer
// count of wedges makes that jump a whole number of wedges, so the seam lands
// on a mirror the fold already treats as one.
//
// ---- audio ------------------------------------------------------------------
//
//   - a section change re-picks the mirror count (4 to 9, hashed from
//     SND_CUT_COUNT) and the bank of the camera, and crossfades both from the
//     old section's over the section envelope (SND_HELD_HARD_CUT): the map is
//     evaluated at both counts for that second and mixed, so nothing pops.
//   - the crystals turn on the mids' clock (SND_MID_TIME, still in silence) and
//     swell on the relative bass beat (SND_BASS over its average SND_BASS_ATT,
//     MilkDrop's beat idiom) and the kick envelope.
//   - the flower-of-life lines flare on the kick (SND_KICK_ENV); hats
//     (SND_HAT_ENV) light a hashed scatter of lattice nodes, re-rolled per onset.
//   - the melody colours it: the crystal hue walks with SND_PITCH_CLASS while a
//     pitch is held (SND_PITCH_CONFIDENCE); treble sharpens the thin-film rim.
//   - the mirrors swing (never spin) on the bass clock, and the light at the
//     end of the tube breathes with the low end.
//
// ---- macros -----------------------------------------------------------------
//
// Speed and Space: the rig (fly speed, look, dolly, FOV). Complexity (uMorph)
// moves the IFS offset, so the crystals change species. Chaos (uWarp,
// uTurbulence) engraves the mirrors deeper. Energy: uIntensity. Colour: pal().
// Detail: uSteps.

#define KAL_TAU 6.2831853
/** Compile-time ceiling on the march: MarchBudget.MAX_STEPS. uSteps is the runtime budget. */
#define KAL_MAX_STEPS 128
#define KAL_FAR 34.0
/** One crystal per cell along the tube. Divides 256, so cell ids wrap cleanly. */
#define KAL_CELL 4.0
/** The prism's apothem: how far each mirror is from the axis. */
#define KAL_PRISM 2.3
/** How far off the axis the crystals ride; the flight channel down the middle stays open. */
#define KAL_ORBIT 1.45
/**
 * Crystal size, in IFS units: three halvings of an offset of at most 1 put the
 * outermost copy about 1.0 from the centre, so 0.42 keeps a crystal inside its
 * wedge even at nine mirrors (1.45 * sin(pi / 9) = 0.50).
 */
#define KAL_SIZE 0.42
#define KAL_KIFS 3
#define KAL_FOG 0.05
/** The engraving and the tunnel warp both stretch space; the step divides both out. */
const float KAL_STEP = 0.75 / DMT_TUNNEL_LIP;

/** PCG integer hash: exact on every device, unlike a sin() hash. */
uint kalHash(uint h) {
    h = h * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    return (h >> 22u) ^ h;
}

/** The mirror count for section number `n`: 4..9. */
float kalFolds(float n) {
    return 4.0 + float(kalHash(uint(max(n, 0.0))) % 6u);
}

/** The camera's bank for section number `n`: -0.45..0.45 radians, a turn that holds for the section. */
float kalBank(float n) {
    return (float(kalHash(uint(max(n, 0.0)) + 977u) & 0xffffu) / 65535.0 - 0.5) * 0.9;
}

float gFly;        // the eye's depth down the tube
float gFoldNew;    // this section's mirror count
float gFoldOld;    // the last section's
float gW;          // 0 = the old section, 1 = this one
float gSwing;      // the mirrors' bounded swing, radians
float gTurn;       // the crystals' turn, on the mids' clock
float gSwell;      // the beat swell
float gMorph;      // Complexity
float gEngrave;    // Chaos
// What the last map() call hit, for shading.
float gCrys;
float gTrap;
float gCellH;
float gLifeK;
vec2 gWall;        // the wall's own 2D coordinate, for the engraving

// Fold the cross-section into `n` mirrored wedges.
vec2 kalFold(vec2 q, float n) {
    float r = length(q);
    float a = (r > 1e-5 ? atan(q.y, q.x) : 0.0) + gSwing;
    float seg = KAL_TAU / n;
    a = abs(mod(a, seg) - 0.5 * seg);
    return vec2(cos(a), sin(a)) * r;
}

// The fractal crystal, in IFS units (distance 1 = KAL_SIZE): a KIFS round an
// octahedron. Each fold-sort-scale step is a rigid map followed by an exact
// scale, so dividing by the accumulated scale keeps the estimate a bound.
float kalCrystal(vec3 c, float h, out float trap) {
    c = rotAxis(normalize(vec3(0.3 + h, 1.0, 0.6 - h)), gTurn + h * KAL_TAU) * c;
    vec3 off = vec3(1.0, 0.45 + 0.4 * gMorph + 0.15 * h, 0.2 + 0.35 * fract(h * 7.31));
    float s = 1.0;
    trap = 1e9;
    for (int i = 0; i < KAL_KIFS; i++) {
        c = abs(c);
        if (c.x < c.y) c.xy = c.yx;
        if (c.x < c.z) c.xz = c.zx;
        if (c.y < c.z) c.yz = c.zy;
        c = c * 2.0 - off;
        s *= 2.0;
        trap = min(trap, dot(c, c));
    }
    // A small octahedron per copy: the gaps between the copies are what make it read as a crystal
    // lattice rather than a ball.
    return sdOctahedron(c, 0.62) / s;
}

// Lattice coordinate along a mirror: across it (the folded y) and down the tube.
float kalScene(vec3 q, float n) {
    vec2 f = kalFold(q.xy, n);
    gWall = vec2(f.y, q.z);
    // The mirrors: in the folded wedge, the wedge's own mirror is the plane x = PRISM.
    float wall = KAL_PRISM - f.x;
    wall -= gEngrave * 0.035 * (sin(f.y * 6.0) * sin(q.z * 2.25) + 1.0);

    float cz = floor(q.z / KAL_CELL);
    float zc = q.z - (cz + 0.5) * KAL_CELL;
    float h = hash11(mod(cz, 64.0) * 3.17 + 0.5);
    // Grows out of nothing as the cell comes out of the far fog, and lives on
    // its own clock: new crystals keep arriving, none appears in one frame.
    float ahead = (cz + 0.5) * KAL_CELL - gFly;
    float grow = smoothstep(KAL_FAR, KAL_FAR * 0.55, ahead);
    float life = mix(0.3, 1.0, dmtLife(h, 10.0));
    float size = KAL_SIZE * grow * life * (1.0 + 0.2 * gSwell);
    vec3 c = vec3(f.x - KAL_ORBIT, f.y, zc);
    float bound = length(c) - size * 1.1;
    float crys = bound;
    float trap = 1.0;
    // Only march into the IFS once the ray is near it: the bound sphere is a
    // valid distance outside, and most rays never get close.
    if (bound < 0.15 && size > 0.004) crys = max(kalCrystal(c / size, h, trap) * size, bound);

    gCrys = crys;
    gTrap = trap;
    gCellH = h;
    gLifeK = life;
    return min(wall, crys);
}

float map(vec3 p) {
    vec3 q = dmtTunnelWarp(p);
    if (gW > 0.999) return kalScene(q, gFoldNew);
    // The section crossfade: both mirror counts, mixed. The bookkeeping globals
    // end up describing the new count, which is what the shading wants once it
    // dominates.
    float dOld = kalScene(q, gFoldOld);
    float dNew = kalScene(q, gFoldNew);
    return mix(dOld, dNew, gW);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

// Flower of life on a mirror: unit circles round the nodes of a triangular
// lattice. The two nearest nodes (one per rectangular sub-grid) give the arcs
// that read as the pattern; returns the distance to the nearest arc and, in
// `node`, the nearest node's id for the hat scatter.
float kalFlower(vec2 w, out vec2 node) {
    const vec2 s = vec2(1.0, 1.7320508);
    vec2 a = mod(w, s) - 0.5 * s;
    vec2 b = mod(w - 0.5 * s, s) - 0.5 * s;
    bool nearA = dot(a, a) < dot(b, b);
    node = nearA ? floor(w / s) : floor((w - 0.5 * s) / s) + 0.5;
    return min(abs(length(a) - 0.5), abs(length(b) - 0.5));
}

void main() {
    vec2 uv = view();
    float bass = min(sndShaped(SND_LOW), 1.3);
    float treb = min(SND_TREBLE_PRESENCE, 1.3);
    float energy = min((SND_BASS_PRESENCE + SND_MID_PRESENCE + SND_TREBLE_PRESENCE) / 3.0, 1.3);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    // MilkDrop's beat: the level over its own average, so quiet and loud songs both pulse.
    float relBeat = clamp((SND_BASS - SND_BASS_ATT) * 2.0, 0.0, 1.0);

    // Section crossfade weight, no frame-to-frame state: SND_CUT_COUNT steps and
    // SND_HELD_HARD_CUT jumps to 1 on the same frame, then the envelope falls.
    float w = 1.0 - clamp(SND_HELD_HARD_CUT, 0.0, 1.0);
    gW = w * w * (3.0 - 2.0 * w);
    gFoldNew = kalFolds(SND_CUT_COUNT);
    gFoldOld = kalFolds(SND_CUT_COUNT - 1.0);
    gSwing = 0.35 * sin(SND_BASS_TIME * 0.21);
    gTurn = SND_MID_TIME * 0.45;
    gSwell = max(relBeat, kick);
    gMorph = clamp(uMorph, 0.0, 1.0);
    gEngrave = clamp(0.4 + uWarp + 0.5 * uTurbulence, 0.0, 1.5);

    // The rig's eye and ray; the depth down the tube is the rig's, the eye goes
    // back onto the bent tube's centreline there, and the view turns onto the
    // bend's tangent, banked by the section's own lean.
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    gFly = ro.z;
    ro.xy = dmtTunnelPath(gFly) + vec2(0.2 * sin(uTime * 0.29), 0.2 * cos(uTime * 0.23));
    float bank = mix(kalBank(SND_CUT_COUNT - 1.0), kalBank(SND_CUT_COUNT), gW);
    mat3 flight = dmtFlightBasis(gFly, bank);
    rd = flight * rd;

    float t = 0.05;
    float hitT = -1.0;
    float glow = 0.0;
    for (int i = 0; i < KAL_MAX_STEPS; i++) {
        if (float(i) >= uSteps || t > KAL_FAR) break;
        float d = map(ro + rd * t);
        // Neon glow off the crystals the ray slides past (the Shadertoy
        // volumetric trick: the near-misses accumulate light).
        glow += exp(-gCrys * 9.0) / (1.0 + 0.03 * t * t);
        float eps = 0.0007 * t + 0.0005;
        if (d < eps) {
            hitT = t;
            break;
        }
        t += max(d * KAL_STEP, eps);
    }

    float pitchHue = SND_PITCH_CLASS * clamp(SND_PITCH_CONFIDENCE * 1.5, 0.0, 1.0);
    float hue = 0.12 + 0.6 * pitchHue;
    vec3 rdLocal = rd * flight;
    vec3 sky = dmtChrysanthemum(rdLocal, hue + 0.5, energy);

    vec3 col;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        map(p);
        bool crystal = gCrys < 0.01 + 0.002 * hitT;
        float h = gCellH;
        float trap = gTrap;
        float life = gLifeK;
        vec2 wallUv = gWall;
        vec3 n = normalAt(p, max(0.0012 * hitT, 0.0006));
        if (crystal) {
            float band = sqrt(trap);
            col = dmtShade(n, rd, hue + 0.6 * h, band * 2.5, 0.9, 1.0 - life, treb);
            // A core that burns with the beat, inside the nested shells.
            col += pal(hue + 0.5 + 0.2 * h) * exp(-band * 2.2) * (0.25 + 0.9 * gSwell);
        } else {
            // The mirror: the reflected sky, stronger at grazing angles, and
            // the engraved flower of life glowing in it.
            vec3 refl = reflect(rd, n);
            float fres = pow(1.0 - max(dot(-rd, n), 0.0), 3.0);
            col = dmtChrysanthemum(refl * flight, hue + 0.25, energy) * (0.3 + 0.7 * fres);
            vec2 node;
            float arc = kalFlower(wallUv * 1.6, node);
            float lines = exp(-arc * 60.0);
            // The lines run through the palette down the tube, so the walls are a rainbow in depth.
            col += pal(hue + 0.1 + 0.035 * (wallUv.y - gFly)) * lines * (0.35 + 1.3 * kick) * exp(-0.05 * hitT);
            float pick = float(kalHash(uint(int(node.x * 2.0) + 4096) ^ kalHash(uint(int(node.y * 2.0) + 4096)
                               ^ kalHash(uint(max(SND_ONSET_COUNT, 0.0))))) & 0xffffu) / 65535.0;
            col += vec3(1.0) * step(0.93, pick) * exp(-arc * 25.0) * clamp(SND_HAT_ENV, 0.0, 1.0) * 0.8;
        }
        col = mix(col, sky, 1.0 - exp(-hitT * KAL_FOG));
    } else {
        col = sky;
    }
    col += pal(hue + 0.55) * glow * 0.035 * (0.5 + gSwell);
    // The light at the end of the tube.
    float onAxis = pow(max(rdLocal.z, 0.0), 26.0);
    col += pal(hue + 0.05) * onAxis * (0.5 + 1.1 * bass);

    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.6, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
