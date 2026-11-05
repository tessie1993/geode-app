// lib_world: Morph World's space, one world that is many (core/viz/world/WorldGenome).
//
// THE IDEA. A morphing 3D environment works because its space is a chain of continuous domain
// operators ending in a simple primitive, and the shape changes when the operators' parameters
// move. Nothing is swapped; everything slides. So every operator here takes a strength that fades
// it from the identity (0) to full (1), and the whole chain reads its numbers from uWorld, the
// World Genome the C++ morph driver (core/viz/world/MorphDriver) eases from one world toward the
// next every frame, with the music's wobble already summed and clamped on top.
//
// THE CHAIN (world()): mirrored z repeat (always: travel is endless) -> a polar kaleidoscope about
// the travel axis (WG_POLAR, N-fold with a spiral: round tunnels instead of the 4-way cross) ->
// mirrored x/y repeat (WG_REP_XY fades it,
// WG_CELL_SHIFT moves the cells off the axis) -> a twist along travel (a sine over one z period,
// so it stays periodic) -> scale -> mirror fold -> up to five rotate-fold IFS steps (fractional:
// the last one fades in) -> sphere inversion -> abs-inversion -> ten primitives blended by weight:
// six simple ones, a baked CC0 model (uWorldShapes), and six fractals (gyroid, Menger sponge,
// Mandelbulb, Mandelbox, Sierpinski tetrahedron, Apollonian packing). Grey CC0 material maps (uWorldTex) add surface
// detail and pattern the light; their colour never reaches the screen, pal() still owns it.
// The orbit trap is gathered on the way (gWTrap) and colours the world for free: its channels,
// weighted by the genome, are the palette coordinate, and its radius is the cheap occlusion.
//
// THE GROUND (end of world()): a plane at WG_FLOOR_Y joined to the world by a smooth min, then
// faded in by WG_FLOOR (a mix between the world alone and the world on its ground), so a morph can
// raise a floor or sink it away with no pop: at a small weight the joined surface lies far under
// the plane and rises to it as the weight grows. The ground is in world(), so the corridor carves
// it like any matter; FloorY's bounds keep it under the deepest eye every rig path flies, bubble
// included, so in practice it never needs to. gWFloor says how much of the last point was ground.
//
// THE SKY (worldFogColour()): where a ray escapes, and what the fog fades toward, is one colour by
// direction: the dark air of the world's own palette, crossfaded by WG_SKY into a gradient from
// pal(WG_SKY_HUE) (brightest at the horizon, darker up high and below), plus a glow band at the
// horizon (WG_HORIZON). Far ground fogs into that band, so the floor meets the sky without a seam.
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
// THE CAMERA'S FEEL (worldCamera()): the eye is the shared rig's, but each world banks and
// focuses its own way: WG_ROLL_ANGLE turns the view about its forward axis (a slow barrel roll,
// a sway, a lean into sideways motion) and WG_LENS_NOW scales the focal length (a wide lens for
// vast halls, a long one for close detail, a punch on the kick). Only the ray turns: the eye and
// its corridor stay where they are, so no roll or lens can put the camera in matter.
//
// Colour only through pal() (lib_palette) and the style's grade(). Include after lib_camera,
// lib_palette and lib_scene_grade.
//
// Provenance: written from scratch for Geode from textbook maths (signed distance functions,
// domain repetition, reflection folds, iterated function systems, sphere inversion, orbit traps,
// sphere tracing, emission-absorption volume rendering). No code was taken from any shader. The
// baked models and material maps are CC0 assets from BlenderKit (docs/morph-world-assets.md).

/** The genome and the driver's runtime values; WorldGenome.hpp's Field and Runtime order. */
uniform vec4 uWorld[22];

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
#define WG_POLAR uWorld[4].w
#define WG_POLAR_N uWorld[5].x
#define WG_POLAR_TWIST uWorld[5].y
#define WG_SCALE uWorld[5].z
#define WG_P_BOX uWorld[5].w
#define WG_P_BAR uWorld[6].x
#define WG_P_TUBE uWorld[6].y
#define WG_P_TORUS uWorld[6].z
#define WG_P_SHELL uWorld[6].w
#define WG_P_SLAB uWorld[7].x
#define WG_P_MESH uWorld[7].y
#define WG_P_GYROID uWorld[7].z
#define WG_P_MENGER uWorld[7].w
#define WG_P_BULB uWorld[8].x
#define WG_P_MANDELBOX uWorld[8].y
#define WG_P_SIERP uWorld[8].z
#define WG_P_APOLLO uWorld[8].w
#define WG_PRIM_SIZE uWorld[9].x
#define WG_PRIM_THICK uWorld[9].y
#define WG_MESH_PICK uWorld[9].z
#define WG_MESH_SIZE uWorld[9].w
#define WG_BULB_POWER uWorld[10].x
#define WG_GYRO_SCALE uWorld[10].y
#define WG_BOX_SCALE uWorld[10].z
#define WG_M_SURFACE uWorld[10].w
#define WG_M_GLOW uWorld[11].x
#define WG_M_DENSITY uWorld[11].y
#define WG_DENS_BAND uWorld[11].z
#define WG_TRAP_X uWorld[11].w
#define WG_TRAP_Y uWorld[12].x
#define WG_TRAP_Z uWorld[12].y
#define WG_TRAP_R uWorld[12].z
#define WG_PAL_OFFSET uWorld[12].w
#define WG_PAL_SPAN uWorld[13].x
#define WG_EMISSION uWorld[13].y
#define WG_FOG uWorld[13].z
#define WG_CORRIDOR uWorld[13].w
#define WG_TEX_AMOUNT uWorld[14].x
#define WG_TEX_PICK uWorld[14].y
#define WG_TEX_SCALE uWorld[14].z
#define WG_TEX_EMIT uWorld[14].w
#define WG_ROLL uWorld[15].x
#define WG_LENS uWorld[15].y
#define WG_FLOOR uWorld[15].z
#define WG_FLOOR_Y uWorld[15].w
#define WG_FLOOR_GLOSS uWorld[16].x
#define WG_SKY uWorld[16].y
#define WG_SKY_HUE uWorld[16].z
#define WG_HORIZON uWorld[16].w
#define WG_TERRAIN uWorld[17].x
#define WG_TERRAIN_HEIGHT uWorld[17].y
#define WG_TERRAIN_SCALE uWorld[17].z
#define WG_LATTICE_TILT uWorld[17].w
#define WG_LATTICE_YAW uWorld[18].x
#define WG_GAZE_PITCH uWorld[18].y
#define WG_GAZE_YAW uWorld[18].z
#define WG_Z_PHASE uWorld[18].w
#define WG_GAIN uWorld[19].x
#define WG_ITER_CAP uWorld[19].y
#define WG_HUE_TURN uWorld[19].z
#define WG_WAKE_X uWorld[19].w
#define WG_WAKE_Y uWorld[20].x
#define WG_WAKE_Z uWorld[20].y
#define WG_ASSETS uWorld[20].z
#define WG_ROLL_ANGLE uWorld[20].w
#define WG_LENS_NOW uWorld[21].x
#define WG_GAZE_X uWorld[21].y
#define WG_GAZE_Y uWorld[21].z
#define WG_SPARE uWorld[21].w

/**
 * The baked CC0 assets (core/viz/world/WorldAssets, tools/morph_world_assets): sixteen models as signed
 * distance volumes, one 32^3 slab each stacked along z (32 x 32 x 512, R8, linear, clamped), and
 * eight grey material detail maps (128^2 layers, mirrored repeat). Read only while WG_ASSETS is 1.
 */
uniform highp sampler3D uWorldShapes;
uniform highp sampler2DArray uWorldTex;

/** The rig's eye and this world's view ray: rolled about the view axis, through this world's lens. */
void worldCamera(vec2 uv, out vec3 ro, out vec3 rd) {
    float c = cos(WG_ROLL_ANGLE);
    float s = sin(WG_ROLL_ANGLE);
    vec2 turned = vec2(c * uv.x - s * uv.y, s * uv.x + c * uv.y);
    ro = uCamPos;
    rd = normalize(uCamBasis * vec3(turned, uCamFocal * WG_LENS_NOW));
}
#define WG_SHAPES 16.0
#define WG_SHAPE_RES 32.0
#define WG_SHAPE_LO -0.25
#define WG_SHAPE_HI 0.75

/** The air always kept around the eye (WorldGenome.hpp kEyeRadius). */
#define WG_EYE_RADIUS 0.9
/** How softly the ground joins the world's matter (WorldGenome.cpp kFloorBlend). */
#define WG_FLOOR_BLEND 0.4
#define WG_TAU 6.2831853
#define WG_MAX_IFS 5

/** The orbit trap of the last world() call: |x|, |y|, |z| and |p|^2 at their smallest. */
vec4 gWTrap;
/** How much of the last world() point was the ground, 0..1 (WG_FLOOR times the smooth min's share). */
float gWFloor;

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

/**
/** GLSL's mod, spelled as WorldGenome.cpp spells it, for the Menger sponge and the kaleidoscope. */
float wMod(float x, float y) { return x - y * floor(x / y); }

/** An endless gyroid sheet of thickness t at frequency k (WorldGenome.cpp gyroid()). */
float wGyroid(vec3 q, float k, float t) {
    float g = (sin(q.x * k) * cos(q.y * k) + sin(q.y * k) * cos(q.z * k) + sin(q.z * k) * cos(q.x * k)) / k;
    return abs(g) * 0.55 - t * 0.5;
}

/** A Menger sponge of half-size s, three levels (WorldGenome.cpp menger()). */
float wMenger(vec3 q, float s) {
    vec3 p = q / s;
    vec3 b = abs(p) - 1.0;
    float d = length(max(b, 0.0)) + min(max(b.x, max(b.y, b.z)), 0.0);
    float scale = 1.0;
    for (int m = 0; m < 3; m++) {
        vec3 a = vec3(wMod(p.x * scale, 2.0), wMod(p.y * scale, 2.0), wMod(p.z * scale, 2.0)) - 1.0;
        scale *= 3.0;
        vec3 rr = abs(1.0 - 3.0 * abs(a));
        float da = max(rr.x, rr.y);
        float db = max(rr.y, rr.z);
        float dc = max(rr.z, rr.x);
        d = max(d, (min(da, min(db, dc)) - 1.0) / scale);
    }
    return d * s;
}

/** A Mandelbulb of power n filling about s, five iterations (WorldGenome.cpp bulb()). */
float wBulb(vec3 q, float s, float n) {
    float k = 0.8 * s;
    vec3 p = q / k;
    vec3 z = p;
    float dr = 1.0;
    float r = length(z);
    for (int i = 0; i < 5; i++) {
        r = length(z);
        if (r > 2.0) break;
        float rs = max(r, 1e-6);
        float theta = acos(clamp(z.z / rs, -1.0, 1.0)) * n;
        float phi = atan(z.y, z.x) * n;
        dr = pow(rs, n - 1.0) * n * dr + 1.0;
        float zr = pow(rs, n);
        z = vec3(zr * sin(theta) * cos(phi), zr * sin(phi) * sin(theta), zr * cos(theta)) + p;
    }
    r = max(r, 1e-6);
    return 0.5 * log(r) * r / dr * k;
}

/** A Mandelbox of scale -b, six iterations, extent brought to about 2 s (WorldGenome.cpp mandelbox()). */
float wMandelbox(vec3 q, float s, float b) {
    float k = 0.35 * s;
    vec3 p = q / k;
    float scale = -b;
    vec3 z = p;
    float dr = 1.0;
    for (int i = 0; i < 6; i++) {
        z = clamp(z, -1.0, 1.0) * 2.0 - z;
        float r2 = dot(z, z);
        if (r2 < 0.25) {
            z *= 4.0;
            dr *= 4.0;
        } else if (r2 < 1.0) {
            z /= r2;
            dr /= r2;
        }
        z = z * scale + p;
        dr = dr * abs(scale) + 1.0;
    }
    return length(z) / abs(dr) * k;
}

/** A Sierpinski tetrahedron, seven folds toward the nearest corner (WorldGenome.cpp sierpinski()). */
float wSierp(vec3 q, float s) {
    vec3 z = q / s;
    float scale = 1.0;
    for (int i = 0; i < 7; i++) {
        if (z.x + z.y < 0.0) z.xy = -z.yx;
        if (z.x + z.z < 0.0) z.xz = -z.zx;
        if (z.y + z.z < 0.0) z.yz = -z.zy;
        z = z * 2.0 - 1.0;
        scale *= 2.0;
    }
    return (length(z) - 1.0) / scale * s;
}

/** An Apollonian packing, seven inversions in a lattice of unit cells (WorldGenome.cpp apollonian()). */
float wApollo(vec3 q, float s) {
    vec3 p = q / s;
    float scale = 1.0;
    for (int i = 0; i < 7; i++) {
        vec3 h = 0.5 * p + 0.5;
        p = -1.0 + 2.0 * (h - floor(h));
        float k = 1.2 / max(dot(p, p), 1e-4);
        p *= k;
        scale *= k;
    }
    return 0.25 * abs(p.y) / scale * s;
}

/** The plane about the travel axis folded into one mirrored wedge of n (WorldGenome.cpp polarFold()). */
vec2 wPolar(vec2 v, float n) {
    float r = length(v);
    float seg = WG_TAU / n;
    float a = abs(wMod(atan(v.y, v.x) + seg * 0.5, seg) - seg * 0.5);
    return r * vec2(cos(a), sin(a));
}

/**
 * A baked model's signed distance at cube point m, in cube units (WorldAssets::shapeDistance):
 * the volume's sample inside the cube; outside, never more than the true distance.
 */
float wShape(float index, vec3 m) {
    if (WG_ASSETS < 0.5) return length(m) - 0.6;
    vec3 c = clamp(m, -1.0, 1.0);
    float out_ = length(m - c);
    vec3 uvw = c * 0.5 + 0.5;
    // The slab's own texel rows only, so linear filtering never reads the next model.
    float z = (index * WG_SHAPE_RES + clamp(uvw.z * WG_SHAPE_RES, 0.5, WG_SHAPE_RES - 0.5)) / (WG_SHAPES * WG_SHAPE_RES);
    float d = mix(WG_SHAPE_LO, WG_SHAPE_HI, texture(uWorldShapes, vec3(uvw.xy, z)).r);
    return out_ > 0.0 ? max(out_, d - out_) : d;
}

/** Two neighbouring baked models, blended by the pick's fraction, at the genome's size. */
float wMesh(vec3 q) {
    float size = WG_MESH_SIZE;
    float a = floor(WG_MESH_PICK);
    float f = WG_MESH_PICK - a;
    vec3 m = q / size;
    float da = wShape(a, m);
    float db = f > 0.0 ? wShape(min(a + 1.0, WG_SHAPES - 1.0), m) : da;
    return mix(da, db, f) * size;
}

/**
 * The thirteen primitives, blended by weight (the weights sum to one): six simple ones (tube and
 * torus ring the travel axis), a baked model, and six fractals: a gyroid, a Menger sponge, a
 * Mandelbulb, a Mandelbox, a Sierpinski tetrahedron and an Apollonian packing.
 */
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
    if (WG_P_MESH > 0.0) d += WG_P_MESH * wMesh(q);
    if (WG_P_GYROID > 0.0) d += WG_P_GYROID * wGyroid(q, WG_GYRO_SCALE, t);
    if (WG_P_MENGER > 0.0) d += WG_P_MENGER * wMenger(q, s);
    if (WG_P_BULB > 0.0) d += WG_P_BULB * wBulb(q, s, WG_BULB_POWER);
    if (WG_P_MANDELBOX > 0.0) d += WG_P_MANDELBOX * wMandelbox(q, s, WG_BOX_SCALE);
    if (WG_P_SIERP > 0.0) d += WG_P_SIERP * wSierp(q, s);
    if (WG_P_APOLLO > 0.0) d += WG_P_APOLLO * wApollo(q, s);
    return d;
}

/** The bare world at p: the operator chain, then the primitives. Writes gWTrap. */
float world(vec3 p) {
    vec3 w = p;
    float pz = WG_CELL_Z;
    // Eye-relative along travel plus the driver's phase: a period that morphs stretches the world
    // about the eye instead of racing it past (MorphDriver::travel).
    w.z = wCell(p.z - uCamPos.z + WG_Z_PHASE, pz);
    if (WG_POLAR > 0.0) {
        // The kaleidoscope: both neighbouring segment counts, blended by the fraction, then faded in.
        vec2 v = wRot(w.xy, WG_POLAR_TWIST * w.z);
        float n0 = floor(WG_POLAR_N);
        float fn = WG_POLAR_N - n0;
        vec2 a = wPolar(v, n0);
        vec2 b = fn > 0.0 ? wPolar(v, n0 + 1.0) : a;
        w.xy = mix(w.xy, mix(a, b, fn), WG_POLAR);
    }
    float sxy = WG_CELL_XY / max(WG_REP_XY, 0.08);
    float shift = 0.5 * WG_CELL_XY * WG_CELL_SHIFT;
    // The shift moves the cells' content out along +x, the side the kaleidoscope folds onto, so a
    // polar world rings its content round the axis at that radius; y moves only without the fold.
    w.x = wCell(w.x - shift, sxy);
    w.y = wCell(w.y + shift * (1.0 - WG_POLAR), sxy);
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
    float d = wPrims(q) / k;
    gWFloor = 0.0;
    if (WG_FLOOR > 0.0) {
        // The ground, joined smoothly, then faded in: continuous in WG_FLOOR, the identity at 0.
        float fd = p.y - WG_FLOOR_Y;
        float h = clamp(0.5 + 0.5 * (fd - d) / WG_FLOOR_BLEND, 0.0, 1.0);
        gWFloor = WG_FLOOR * (1.0 - h);
        d = mix(d, wSmin(d, fd, WG_FLOOR_BLEND), WG_FLOOR);
    }
    return d;
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

/** One detail map, or the blend of two neighbours by the pick's fraction, at uv. 0..1, grey. */
float worldDetailAt(vec2 uv) {
    float a = floor(WG_TEX_PICK);
    float f = WG_TEX_PICK - a;
    float va = texture(uWorldTex, vec3(uv, a)).r;
    return f > 0.0 ? mix(va, texture(uWorldTex, vec3(uv, min(a + 1.0, 7.0))).r, f) : va;
}

/** The surface detail at p with normal n: the map projected along each axis, weighted by the normal. */
float worldDetail(vec3 p, vec3 n) {
    if (WG_ASSETS < 0.5) return 0.5;
    vec3 w = pow(abs(n), vec3(4.0));
    w /= w.x + w.y + w.z;
    vec3 s = p * WG_TEX_SCALE;
    return w.x * worldDetailAt(s.yz) + w.y * worldDetailAt(s.xz) + w.z * worldDetailAt(s.xy);
}

/** The same, cheaply, for the volume modes: one projection, across the travel axis. */
float worldDetailFlat(vec3 p) {
    if (WG_ASSETS < 0.5) return 0.5;
    return worldDetailAt(p.xy * WG_TEX_SCALE + vec2(0.0, p.z * 0.37 * WG_TEX_SCALE));
}

/** How far the eye sees: the fog's visibility, capped so a march ends. */
float worldFar() { return min(4.0 / max(WG_FOG, 0.015), 70.0); }

vec3 worldNormal(vec3 p, float t) {
    float e = 0.0015 * (1.0 + t);
    vec2 h = vec2(1.0, -1.0);
    return normalize(h.xyy * worldCarved(p + h.xyy * e) + h.yyx * worldCarved(p + h.yyx * e)
                     + h.yxy * worldCarved(p + h.yxy * e) + h.xxx * worldCarved(p + h.xxx * e));
}

/**
 * The air's colour along rd: what an escaped ray shows and what the fog fades toward. A dark tint
 * of the world's own palette, crossfaded by WG_SKY into the sky's gradient (brightest at the
 * horizon, dimmer toward the zenith, dimmer still below, continuous at rd.y = 0), plus the
 * horizon's glow band (WG_HORIZON), narrow and symmetric about the horizon.
 */
vec3 worldFogColour(vec3 rd) {
    vec3 air = pal(WG_PAL_OFFSET + WG_HUE_TURN + 0.5 + 0.1 * rd.y) * 0.035;
    float up = clamp(rd.y, -1.0, 1.0);
    vec3 col = air;
    if (WG_SKY > 0.0) {
        float level = up >= 0.0 ? 0.06 + 0.22 * exp(-3.5 * up) : 0.04 + 0.24 * exp(5.0 * up);
        vec3 sky = pal(WG_SKY_HUE + WG_HUE_TURN + 0.15 * up) * level;
        col = mix(air, sky, WG_SKY);
    }
    if (WG_HORIZON > 0.0) col += pal(WG_SKY_HUE + WG_HUE_TURN + 0.08) * exp(-abs(up) * 18.0) * 0.45 * WG_HORIZON;
    return col;
}

/**
 * What the glossy ground mirrors: a short sphere trace along the reflected ray r from p, the
 * world's colour where it lands (lit flat, fogged), else the air. `budget` steps, at most 24.
 */
vec3 worldMirror(vec3 p, vec3 r, float budget) {
    float far = worldFar();
    float t = 0.08;
    for (int i = 0; i < 24; i++) {
        if (float(i) >= budget) break;
        float d = worldCarved(p + r * t);
        if (d < 0.004 * (1.0 + t)) {
            vec3 c = pal(worldHue(gWTrap)) * (0.2 + 0.4 * WG_EMISSION);
            return mix(c, worldFogColour(r), 1.0 - exp(-t * WG_FOG));
        }
        t += d * 0.85;
        if (t > far) break;
    }
    return worldFogColour(r);
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
    float ground = gWFloor;
    float detail = worldDetail(p, n);
    vec3 base = pal(worldHue(trap)) * mix(1.0, 0.3 + 1.4 * detail, WG_TEX_AMOUNT);
    // The ground takes the world's palette, darker, so the structures stand on it, not in it.
    base *= 1.0 - 0.45 * ground;
    float pattern = mix(1.0, 2.0 * detail, WG_TEX_EMIT);
    vec3 l = normalize(uCamBasis[1] * 0.8 - rd * 0.6 + vec3(0.0, 0.3, 0.0));
    float diff = max(dot(n, l), 0.0);
    float rim = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
    float occ = clamp(1.0 - used / max(budget, 1.0), 0.15, 1.0) * clamp(0.35 + sqrt(trap.w) * 0.5, 0.25, 1.0);
    // The far ground is seen at a grazing angle, where the rim would light all of it: damp it there.
    vec3 col = base * (0.12 + 1.0 * diff) * occ + base * rim * 0.45 * WG_EMISSION * pattern * (1.0 - 0.7 * ground);
    float spec = pow(max(dot(reflect(rd, n), l), 0.0), 24.0);
    col += vec3(spec) * 0.25 * occ;
    float gloss = ground * WG_FLOOR_GLOSS;
    if (gloss > 0.02) {
        // A glossy ground: Schlick's fresnel weighs what it mirrors, more at grazing angles.
        vec3 r = reflect(rd, n);
        float fres = 0.04 + 0.96 * pow(1.0 - max(dot(n, -rd), 0.0), 5.0);
        col = mix(col, worldMirror(p + n * 0.01, r, budget * 0.25), gloss * (0.25 + 0.75 * fres));
    }
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
        float pattern = WG_TEX_EMIT > 0.01 ? mix(1.0, 2.0 * worldDetailFlat(p), WG_TEX_EMIT) : 1.0;
        acc += pal(worldHue(gWTrap)) * exp(-d * 9.0) * stepLen * exp(-t * WG_FOG) * pattern;
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
        if (WG_TEX_EMIT > 0.01) dens *= mix(1.0, 2.0 * worldDetailFlat(p), WG_TEX_EMIT);
        float a = 1.0 - exp(-dens * dt * 1.4);
        acc += trans * a * pal(worldHue(gWTrap)) * exp(-t * WG_FOG * 0.5);
        trans *= 1.0 - a * 0.5;
        t += dt;
        if (trans < 0.03) break;
    }
    return acc * 0.9 * WG_EMISSION + worldFogColour(rd) * trans;
}
