// lib_world: Morph World's space, one world that is many (core/viz/world/WorldGenome).
//
// THE IDEA. A morphing 3D environment works because its space is a chain of continuous domain
// operators ending in a simple primitive, and the shape changes when the operators' parameters
// move. Nothing is swapped; everything slides. So every operator here takes a strength that fades
// it from the identity (0) to full (1), and the whole chain reads its numbers from uWorld, the
// World Genome the C++ morph driver (core/viz/world/MorphDriver) eases from one world toward the
// next every frame, with the music's wobble already summed and clamped on top.
//
// THE CHAIN (world()): mirrored z repeat (always: travel is endless) -> mirrored x/y repeat (WG_REP_XY fades it,
// WG_CELL_SHIFT moves the cells off the axis) -> a twist along travel (a sine over one z period,
// so it stays periodic) -> scale -> mirror fold -> up to five rotate-fold IFS steps (fractional:
// the last one fades in) -> sphere inversion -> abs-inversion -> six primitives blended by weight.
// The orbit trap is gathered on the way (gWTrap) and colours the world for free: its channels,
// weighted by the genome, are the palette coordinate, and its radius is the cheap occlusion.
//
// THE CORRIDOR (worldCarved()): the world minus a tube along the travel axis, a bubble around
// the eye and a capsule along where the eye is heading (the wake), joined smoothly. The smooth
// max never lies below max(), so the eye always has at least WG_EYE_RADIUS of air: no genome,
// however far it morphs, traps the camera in matter. core/tests/world_genome_test checks that on
// a CPU port of this exact function (WorldGenome.cpp, sdf()); keep the two in step.
//
// THREE WAYS TO SEE IT, all over the same world(): worldSurface() sphere-traces a hard surface
// (normals, light, trap occlusion, fog); worldGlow() marches through the surfaces on |d| with a
// minimum step and gathers light near them (translucent glowing plates); worldDensity() gathers a
// thin iso-band around the surfaces as glowing sheets. The style blends them by the genome's
// mode weights and skips a mode whose weight is nil, so a steady world pays for one march.
//
// Colour only through pal() (lib_palette) and the style's grade(). Include after lib_camera,
// lib_palette and lib_scene_grade.
//
// Provenance: written from scratch for Geode from textbook maths (signed distance functions,
// domain repetition, reflection folds, iterated function systems, sphere inversion, orbit traps,
// sphere tracing, emission-absorption volume rendering). No code was taken from any shader.

/** The genome and the driver's runtime values; WorldGenome.hpp's Field and Runtime order. */
uniform vec4 uWorld[12];

#define WG_REP_XY uWorld[0].x
#define WG_CELL_XY uWorld[0].y
#define WG_CELL_Z uWorld[0].z
#define WG_CELL_SHIFT uWorld[0].w
#define WG_MIRROR uWorld[1].x
#define WG_MIRROR_OFF uWorld[1].y
#define WG_IFS uWorld[1].z
#define WG_IFS_ITER uWorld[1].w
#define WG_IFS_SCALE uWorld[2].x
#define WG_IFS_ANG_A uWorld[2].y
#define WG_IFS_ANG_B uWorld[2].z
#define WG_IFS_OFF_X uWorld[2].w
#define WG_IFS_OFF_Y uWorld[3].x
#define WG_IFS_OFF_Z uWorld[3].y
#define WG_INVERT uWorld[3].z
#define WG_INV_RADIUS uWorld[3].w
#define WG_ABS_INV uWorld[4].x
#define WG_ABS_INV_C uWorld[4].y
#define WG_TWIST uWorld[4].z
#define WG_SCALE uWorld[4].w
#define WG_P_BOX uWorld[5].x
#define WG_P_BAR uWorld[5].y
#define WG_P_TUBE uWorld[5].z
#define WG_P_TORUS uWorld[5].w
#define WG_P_SHELL uWorld[6].x
#define WG_P_SLAB uWorld[6].y
#define WG_PRIM_SIZE uWorld[6].z
#define WG_PRIM_THICK uWorld[6].w
#define WG_M_SURFACE uWorld[7].x
#define WG_M_GLOW uWorld[7].y
#define WG_M_DENSITY uWorld[7].z
#define WG_DENS_BAND uWorld[7].w
#define WG_TRAP_X uWorld[8].x
#define WG_TRAP_Y uWorld[8].y
#define WG_TRAP_Z uWorld[8].z
#define WG_TRAP_R uWorld[8].w
#define WG_PAL_OFFSET uWorld[9].x
#define WG_PAL_SPAN uWorld[9].y
#define WG_EMISSION uWorld[9].z
#define WG_FOG uWorld[9].w
#define WG_CORRIDOR uWorld[10].x
#define WG_Z_PHASE uWorld[10].y
#define WG_GAIN uWorld[10].z
#define WG_ITER_CAP uWorld[10].w
#define WG_HUE_TURN uWorld[11].x
#define WG_WAKE_X uWorld[11].y
#define WG_WAKE_Y uWorld[11].z
#define WG_WAKE_Z uWorld[11].w

/** The air always kept around the eye (WorldGenome.hpp kEyeRadius). */
#define WG_EYE_RADIUS 0.9
#define WG_TAU 6.2831853
#define WG_MAX_IFS 5

/** The orbit trap of the last world() call: |x|, |y|, |z| and |p|^2 at their smallest. */
vec4 gWTrap;

float wSmin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}
/** Never below max(a, b): carving with it can only add air. */
float wSmax(float a, float b, float k) { return -wSmin(-a, -b, k); }
/**
 * The mirrored cell: each cell is its neighbour's reflection, so the field is continuous across
 * every wall (a translated repeat cuts any shape that reaches the wall in two). Period 2 * size,
 * range -size/2 .. size/2, the identity near 0.
 */
float wCell(float x, float size) { return 0.5 * size - abs(mod(x + 0.5 * size, 2.0 * size) - size); }
vec2 wRot(vec2 v, float a) {
    float c = cos(a);
    float s = sin(a);
    return vec2(c * v.x - s * v.y, s * v.x + c * v.y);
}

float wBox(vec3 q, float b) {
    vec3 d = abs(q) - b;
    return length(max(d, 0.0)) + min(max(d.x, max(d.y, d.z)), 0.0);
}

/** The six primitives, blended by weight (the weights sum to one). Tube and torus ring the travel axis. */
float wPrims(vec3 q) {
    float s = WG_PRIM_SIZE;
    float t = WG_PRIM_THICK;
    vec3 a = abs(q);
    float d = 0.0;
    if (WG_P_BOX > 0.0) d += WG_P_BOX * (wBox(q, s * 0.6) - t * 0.3);
    if (WG_P_BAR > 0.0) d += WG_P_BAR * (min(max(a.x, a.y), min(max(a.y, a.z), max(a.z, a.x))) - t);
    if (WG_P_TUBE > 0.0) d += WG_P_TUBE * (abs(length(q.xy) - s) - t);
    if (WG_P_TORUS > 0.0) d += WG_P_TORUS * (length(vec2(length(q.xy) - s, q.z)) - t);
    if (WG_P_SHELL > 0.0) d += WG_P_SHELL * (abs(length(q) - s) - t);
    if (WG_P_SLAB > 0.0) d += WG_P_SLAB * (a.y - t);
    return d;
}

/** The bare world at p: the operator chain, then the primitives. Writes gWTrap. */
float world(vec3 p) {
    vec3 w = p;
    float pz = WG_CELL_Z;
    // Eye-relative along travel plus the driver's phase: a period that morphs stretches the world
    // about the eye instead of racing it past (MorphDriver::travel).
    w.z = wCell(p.z - uCamPos.z + WG_Z_PHASE, pz);
    float sxy = WG_CELL_XY / max(WG_REP_XY, 0.08);
    float shift = 0.5 * WG_CELL_XY * WG_CELL_SHIFT;
    w.x = wCell(w.x + shift, sxy);
    w.y = wCell(w.y + shift, sxy);
    w.xy = wRot(w.xy, WG_TWIST * sin(WG_TAU * w.z / pz));
    vec3 q = w / WG_SCALE;
    float k = 1.0 / WG_SCALE;
    q = mix(q, abs(q) - WG_MIRROR_OFF, WG_MIRROR);
    gWTrap = vec4(abs(q), dot(q, q));
    float steps = min(WG_IFS_ITER, WG_ITER_CAP);
    float sc = WG_IFS_SCALE;
    vec3 off = vec3(WG_IFS_OFF_X, WG_IFS_OFF_Y, WG_IFS_OFF_Z);
    for (int i = 0; i < WG_MAX_IFS; i++) {
        float f = WG_IFS * clamp(steps - float(i), 0.0, 1.0);
        if (f <= 0.0) break;
        vec3 r = abs(q);
        r.xy = wRot(r.xy, WG_IFS_ANG_A);
        r.yz = wRot(r.yz, WG_IFS_ANG_B);
        r = r * sc - off * (sc - 1.0);
        q = mix(q, r, f);
        k *= 1.0 + f * (sc - 1.0);
        gWTrap = min(gWTrap, vec4(abs(q), dot(q, q)));
    }
    float rr = WG_INV_RADIUS * WG_INV_RADIUS;
    float m = mix(1.0, clamp(rr / max(dot(q, q), 1e-4), 0.25, 4.0), WG_INVERT);
    q *= m;
    k *= m;
    float r2 = max(dot(q, q), 0.25);
    q = mix(q, abs(q) / r2 - WG_ABS_INV_C, WG_ABS_INV);
    k *= mix(1.0, 1.0 / r2, WG_ABS_INV);
    gWTrap = min(gWTrap, vec4(abs(q), dot(q, q)));
    return wPrims(q) / k;
}

/** The open way: the travel tube, the eye's bubble, the wake ahead. Negative inside the air. */
float worldCorridor(vec3 p) {
    float tube = length(p.xy) - WG_CORRIDOR;
    vec3 e = p - uCamPos;
    float eye = length(e) - WG_EYE_RADIUS;
    vec3 wake = vec3(WG_WAKE_X, WG_WAKE_Y, WG_WAKE_Z);
    float ww = dot(wake, wake);
    float h = ww > 1e-6 ? clamp(dot(e, wake) / ww, 0.0, 1.0) : 0.0;
    float cap = length(e - wake * h) - WG_EYE_RADIUS * 0.85;
    return wSmin(wSmin(tube, eye, 0.5), cap, 0.5);
}

/** The world the camera flies through: matter minus the corridor. */
float worldCarved(vec3 p) { return wSmax(world(p), -worldCorridor(p), 0.5); }

/** The palette coordinate an orbit trap gives (unwrapped: pal() is periodic). */
float worldHue(vec4 trap) {
    float c = WG_TRAP_X * trap.x + WG_TRAP_Y * trap.y + WG_TRAP_Z * trap.z + WG_TRAP_R * sqrt(trap.w);
    return WG_PAL_OFFSET + WG_HUE_TURN + WG_PAL_SPAN * c * 0.6;
}

/** How far the eye sees: the fog's visibility, capped so a march ends. */
float worldFar() { return min(4.0 / max(WG_FOG, 0.015), 70.0); }

vec3 worldNormal(vec3 p, float t) {
    float e = 0.0015 * (1.0 + t);
    vec2 h = vec2(1.0, -1.0);
    return normalize(h.xyy * worldCarved(p + h.xyy * e) + h.yyx * worldCarved(p + h.yyx * e)
                     + h.yxy * worldCarved(p + h.yxy * e) + h.xxx * worldCarved(p + h.xxx * e));
}

/** The air's colour: a dark tint of the world's own palette. */
vec3 worldFogColour(vec3 rd) {
    return pal(WG_PAL_OFFSET + WG_HUE_TURN + 0.5 + 0.1 * rd.y) * 0.035;
}

/**
 * The hard surface: sphere-traced to a hit, lit by a light that rides above the eye, a rim and an
 * edge glow, occluded by the trap and the step count, fogged. `budget` is the step budget.
 */
vec3 worldSurface(vec3 ro, vec3 rd, float budget) {
    float far = worldFar();
    float t = 0.05;
    bool hit = false;
    float used = 0.0;
    for (int i = 0; i < 128; i++) {
        if (float(i) >= budget) break;
        float d = worldCarved(ro + rd * t);
        used = float(i);
        if (d < 0.0012 * (1.0 + t)) {
            hit = true;
            break;
        }
        t += d * 0.75;
        if (t > far) break;
    }
    vec3 fogCol = worldFogColour(rd);
    if (!hit) return fogCol;
    vec3 p = ro + rd * t;
    vec3 n = worldNormal(p, t);
    world(p);
    vec4 trap = gWTrap;
    vec3 base = pal(worldHue(trap));
    vec3 l = normalize(uCamBasis[1] * 0.8 - rd * 0.6 + vec3(0.0, 0.3, 0.0));
    float diff = max(dot(n, l), 0.0);
    float rim = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
    float occ = clamp(1.0 - used / max(budget, 1.0), 0.15, 1.0) * clamp(0.35 + sqrt(trap.w) * 0.5, 0.25, 1.0);
    vec3 col = base * (0.08 + 0.75 * diff) * occ + base * rim * 0.45 * WG_EMISSION;
    float spec = pow(max(dot(reflect(rd, n), l), 0.0), 24.0);
    col += vec3(spec) * 0.25 * occ;
    return mix(col, fogCol, 1.0 - exp(-t * WG_FOG));
}

/**
 * Glow-through: marched on |d| with a minimum step, so the ray passes through matter and gathers
 * light from every surface it nears: translucent glowing plates. `budget` steps.
 */
vec3 worldGlow(vec3 ro, vec3 rd, float budget, float jitter) {
    float far = worldFar() * 0.8;
    float t = 0.1 + 0.15 * jitter;
    vec3 acc = vec3(0.0);
    for (int i = 0; i < 64; i++) {
        if (float(i) >= budget) break;
        vec3 p = ro + rd * t;
        float d = abs(worldCarved(p));
        float stepLen = max(d * 0.6, 0.06 + t * 0.012);
        acc += pal(worldHue(gWTrap)) * exp(-d * 9.0) * stepLen * exp(-t * WG_FOG);
        t += stepLen;
        if (t > far) break;
    }
    return acc * 0.6 * WG_EMISSION + worldFogColour(rd);
}

/**
 * Emissive density: a thin band about the iso-surface (WG_DENS_BAND wide) as glowing sheets,
 * emission-absorption at even steps from a jittered start. `budget` steps.
 */
vec3 worldDensity(vec3 ro, vec3 rd, float budget, float jitter) {
    float far = worldFar() * 0.6;
    float n = max(budget, 8.0);
    float dt = far / n;
    float t = 0.1 + dt * jitter;
    float trans = 1.0;
    vec3 acc = vec3(0.0);
    float band = WG_DENS_BAND;
    for (int i = 0; i < 48; i++) {
        if (float(i) >= n) break;
        vec3 p = ro + rd * t;
        float d = worldCarved(p);
        float dens = exp(-(d * d) / (band * band)) * (0.6 + 0.6 * clamp(1.0 - sqrt(gWTrap.w) * 0.3, 0.0, 1.0));
        float a = 1.0 - exp(-dens * dt * 1.4);
        acc += trans * a * pal(worldHue(gWTrap)) * exp(-t * WG_FOG * 0.5);
        trans *= 1.0 - a * 0.5;
        t += dt;
        if (trans < 0.03) break;
    }
    return acc * 0.9 * WG_EMISSION + worldFogColour(rd) * trans;
}
