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
// mirrored x/y repeat (WG_LATTICE_TILT and WG_LATTICE_YAW first turn its lattice off the view,
// WG_REP_XY fades it, WG_CELL_SHIFT moves the cells off the axis) -> a twist along travel (a sine over one z period,
// so it stays periodic) -> scale -> mirror fold -> up to five rotate-fold IFS steps (fractional:
// the last one fades in) -> sphere inversion -> abs-inversion -> sixteen primitives blended by weight:
// six simple ones, a baked CC0 model (uWorldShapes), and nine fractals (gyroid, Menger sponge,
// Mandelbulb, Mandelbox, Sierpinski tetrahedron, Apollonian packing, quaternion Julia set,
// pseudo-Kleinian limit set, octahedral kaleidoscopic IFS). Grey CC0 material maps (uWorldTex) add surface
// detail and pattern the light; their colour never reaches the screen, pal() still owns it.
// Then the landscape (WG_TERRAIN, wTerrain()): a ridged fBm height field in world space, not
// through the chain, rising out of the ground at FLOOR_Y and joined to the chain's matter by min.
// Its walls rise to the sides, so the travel line runs down a valley, and the corridor carves
// a canyon through any ridge that crosses the way.
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
// THREE WAYS TO SEE IT, all over the same world() (worldRender()): a sphere-traced hard surface
// (normals, light, trap occlusion, fog); a glow-through march on |d| with a minimum step that
// gathers light near the surfaces (translucent glowing plates); a density that gathers a thin
// iso-band around the surfaces as glowing sheets. They are blended by the genome's mode weights,
// a mode whose weight is nil is skipped, and all of them run in one loop around one world() call.
//
// THE CAMERA'S FEEL (worldCamera()): the eye is the shared rig's, but each world banks and
// focuses its own way: WG_ROLL_ANGLE turns the view about its forward axis (a slow barrel roll,
// a sway, a lean into sideways motion) and WG_LENS_NOW scales the focal length (a wide lens for
// vast halls, a long one for close detail, a punch on the kick). Only the ray turns: the eye and
// its corridor stay where they are, so no roll or lens can put the camera in matter. After the
// roll the ray also looks around (the gaze): WG_GAZE_Y pitches it about the view's right axis and
// WG_GAZE_X yaws it about its up axis, within the world's WG_GAZE_PITCH and WG_GAZE_YAW (the driver
// keeps it there); still only the ray turns.
//
// Colour only through pal() (lib_palette) and the style's grade(). Include after lib_camera,
// lib_palette and lib_scene_grade.
//
// Provenance: written from scratch for Geode from textbook maths (signed distance functions,
// domain repetition, reflection folds, iterated function systems, sphere inversion, orbit traps,
// sphere tracing, emission-absorption volume rendering, value noise, fractional Brownian motion,
// ridged noise, the Lipschitz bound of a height field). No code was taken from any shader. The
// baked models and material maps are CC0 assets from BlenderKit (docs/morph-world-assets.md).

/** The genome and the driver's runtime values; WorldGenome.hpp's Field and Runtime order. */
uniform vec4 uWorld[24];

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
#define WG_P_JULIA uWorld[9].x
#define WG_P_KLEIN uWorld[9].y
#define WG_P_KIFS uWorld[9].z
#define WG_PRIM_SIZE uWorld[9].w
#define WG_PRIM_THICK uWorld[10].x
#define WG_MESH_PICK uWorld[10].y
#define WG_MESH_SIZE uWorld[10].z
#define WG_BULB_POWER uWorld[10].w
#define WG_GYRO_SCALE uWorld[11].x
#define WG_BOX_SCALE uWorld[11].y
#define WG_M_SURFACE uWorld[11].z
#define WG_M_GLOW uWorld[11].w
#define WG_M_DENSITY uWorld[12].x
#define WG_DENS_BAND uWorld[12].y
#define WG_TRAP_X uWorld[12].z
#define WG_TRAP_Y uWorld[12].w
#define WG_TRAP_Z uWorld[13].x
#define WG_TRAP_R uWorld[13].y
#define WG_PAL_OFFSET uWorld[13].z
#define WG_PAL_SPAN uWorld[13].w
#define WG_EMISSION uWorld[14].x
#define WG_FOG uWorld[14].y
#define WG_CORRIDOR uWorld[14].z
#define WG_TEX_AMOUNT uWorld[14].w
#define WG_TEX_PICK uWorld[15].x
#define WG_TEX_SCALE uWorld[15].y
#define WG_TEX_EMIT uWorld[15].z
#define WG_ROLL uWorld[15].w
#define WG_LENS uWorld[16].x
#define WG_FLOOR uWorld[16].y
#define WG_FLOOR_Y uWorld[16].z
#define WG_FLOOR_GLOSS uWorld[16].w
#define WG_SKY uWorld[17].x
#define WG_SKY_HUE uWorld[17].y
#define WG_HORIZON uWorld[17].z
#define WG_TERRAIN uWorld[17].w
#define WG_TERRAIN_HEIGHT uWorld[18].x
#define WG_TERRAIN_SCALE uWorld[18].y
#define WG_LATTICE_TILT uWorld[18].z
#define WG_LATTICE_YAW uWorld[18].w
#define WG_GAZE_PITCH uWorld[19].x
#define WG_GAZE_YAW uWorld[19].y
#define WG_JULIA_CX uWorld[19].z
#define WG_JULIA_CY uWorld[19].w
#define WG_JULIA_CZ uWorld[20].x
#define WG_JULIA_CW uWorld[20].y
#define WG_KLEIN_BOX uWorld[20].z
#define WG_KIFS_SCALE uWorld[20].w
#define WG_Z_PHASE uWorld[21].x
#define WG_GAIN uWorld[21].y
#define WG_ITER_CAP uWorld[21].z
#define WG_HUE_TURN uWorld[21].w
#define WG_WAKE_X uWorld[22].x
#define WG_WAKE_Y uWorld[22].y
#define WG_WAKE_Z uWorld[22].z
#define WG_ASSETS uWorld[22].w
#define WG_ROLL_ANGLE uWorld[23].x
#define WG_LENS_NOW uWorld[23].y
#define WG_GAZE_X uWorld[23].z
#define WG_GAZE_Y uWorld[23].w

/**
 * The optional parts compiled in (MorphDriver::parts(), ShaderScene builds the style with them): bit i
 * the primitive WG_P_* number i in uWorld order (1 the box ... 32768 the KIFS), 65536 the landscape.
 * A part left out has no weight in the world now or the one it glides to. Without it, every part.
 */
#ifndef WG_PARTS
#define WG_PARTS 131071
#endif

/**
 * The baked CC0 assets (core/viz/world/WorldAssets, tools/morph_world_assets): twenty-four models as signed
 * distance volumes, one 32^3 tile each in a 6 x 4 atlas side by side in x and y (192 x 128 x 32, R8,
 * linear, clamped; ShaderScene repacks the file's z-stacked slabs at upload so no side passes the
 * 256 GLES 3.0 promises), and eight grey material detail maps (128^2 layers, mirrored repeat).
 * Read only while WG_ASSETS is 1.
 */
uniform highp sampler3D uWorldShapes;
uniform highp sampler2DArray uWorldTex;

/** The rig's eye and this world's view ray: rolled about the view axis, through this world's lens, turned by the gaze. */
void worldCamera(vec2 uv, out vec3 ro, out vec3 rd) {
    float c = cos(WG_ROLL_ANGLE);
    float s = sin(WG_ROLL_ANGLE);
    vec2 turned = vec2(c * uv.x - s * uv.y, s * uv.x + c * uv.y);
    vec3 v = vec3(turned, uCamFocal * WG_LENS_NOW);
    // The gaze, in the view's own frame (uCamBasis' right, up, forward): pitch about right (+ looks
    // up), then yaw about up (+ looks right).
    float cp = cos(WG_GAZE_Y);
    float sp = sin(WG_GAZE_Y);
    v = vec3(v.x, cp * v.y + sp * v.z, cp * v.z - sp * v.y);
    float cy = cos(WG_GAZE_X);
    float sy = sin(WG_GAZE_X);
    v = vec3(cy * v.x + sy * v.z, v.y, cy * v.z - sy * v.x);
    ro = uCamPos;
    rd = normalize(uCamBasis * v);
}
#define WG_SHAPES 24.0
#define WG_SHAPE_RES 32.0
/** The atlas grid (WorldAssets kAtlasCols, kAtlasRows): shape i at tile (i mod cols, i / cols). */
#define WG_ATLAS_COLS 6.0
#define WG_ATLAS_ROWS 4.0
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
/** v turned by the angle whose cos and sin are cs. */
vec2 wTurn(vec2 v, vec2 cs) { return vec2(cs.x * v.x - cs.y * v.y, cs.y * v.x + cs.x * v.y); }

/** The cos and sin of the genome's fixed turns: once per pixel (worldPrepare()), not in every world() call. */
vec2 gWIfsA;
vec2 gWIfsB;
vec2 gWTilt;
vec2 gWYaw;
void worldPrepare() {
    gWIfsA = vec2(cos(WG_IFS_ANG_A), sin(WG_IFS_ANG_A));
    gWIfsB = vec2(cos(WG_IFS_ANG_B), sin(WG_IFS_ANG_B));
    gWTilt = vec2(cos(WG_LATTICE_TILT), sin(WG_LATTICE_TILT));
    gWYaw = vec2(cos(WG_LATTICE_YAW), sin(WG_LATTICE_YAW));
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

/**
 * A quaternion Julia set, z -> z^2 + c with c the genome's (WG_JULIA_C*), on the slice w = 0 and
 * filling about s (WorldGenome.cpp julia()). The real part is x. The square of (a, v) is
 * (a^2 - |v|^2, 2 a v); the running derivative is 2 |z| dr; the distance is the Hubbard-Douady
 * bound 0.5 |z| log |z| / |dz|. Inside the set the bound is held at one unit deep: matter, not a
 * number that drowns the other primitives' weights.
 */
float wJulia(vec3 q, float s, vec4 c) {
    float k = 0.7 * s;
    vec4 z = vec4(q / k, 0.0);
    float dr = 1.0;
    for (int i = 0; i < 8; i++) {
        float r2 = dot(z, z);
        if (r2 > 16.0) break;
        dr = 2.0 * sqrt(r2) * dr;
        z = vec4(z.x * z.x - dot(z.yzw, z.yzw), 2.0 * z.x * z.yzw) + c;
    }
    float r = max(length(z), 1e-6);
    return max(0.5 * log(r) * r / max(dr, 1e-6), -1.0) * k;
}

/**
 * A pseudo-Kleinian limit set (WorldGenome.cpp klein()): the space folded into the box of half-size
 * b by its face reflections (the mirrored cell), then inverted in the unit sphere (the inversion's
 * stretch capped at 16, so the middle scales instead of flying off), seven times; the last fold
 * then meets a slab of half-thickness t, whose images under the group are the nested discs and
 * caps. Held in a ball of 1.3 s, so the foam is a thing in its cell, not a wall.
 */
float wKlein(vec3 q, float s, float b, float t) {
    float k = 0.5 * s;
    vec3 z = q / k;
    float dr = 1.0;
    for (int i = 0; i < 7; i++) {
        z = vec3(wCell(z.x, 2.0 * b), wCell(z.y, 2.0 * b), wCell(z.z, 2.0 * b));
        float m = clamp(1.0 / max(dot(z, z), 1e-4), 1.0, 16.0);
        z *= m;
        dr *= m;
    }
    z = vec3(wCell(z.x, 2.0 * b), wCell(z.y, 2.0 * b), wCell(z.z, 2.0 * b));
    float d = (abs(z.y) - t) / dr * k;
    return max(d, length(q) - 1.3 * s);
}

/**
 * An octahedral kaleidoscopic IFS (WorldGenome.cpp kifs()): fold across the octahedron's nine
 * mirror planes (the three coordinate planes, then x = y, x = z, y = z), scale by sc about the
 * vertex (1, 0, 0), seven times; the distance is to the unit octahedron at the last level.
 * Its hull is the octahedron of radius 1.2 s.
 */
float wKifs(vec3 q, float s, float sc) {
    float k = 1.2 * s;
    vec3 z = q / k;
    float scale = 1.0;
    for (int i = 0; i < 7; i++) {
        z = abs(z);
        if (z.x < z.y) z.xy = z.yx;
        if (z.x < z.z) z.xz = z.zx;
        if (z.y < z.z) z.yz = z.zy;
        z = z * sc - vec3(sc - 1.0, 0.0, 0.0);
        scale *= sc;
    }
    return (abs(z.x) + abs(z.y) + abs(z.z) - 1.0) * 0.57735 / scale * k;
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
    // The tile's own texel centres only, so linear filtering never reads a neighbouring model
    // (WorldAssets::atlasDistance is this addressing on the CPU).
    float i = clamp(floor(index + 0.5), 0.0, WG_SHAPES - 1.0);
    float row = floor((i + 0.5) / WG_ATLAS_COLS);
    vec3 origin = vec3(i - row * WG_ATLAS_COLS, row, 0.0) * WG_SHAPE_RES;
    vec3 inTile = clamp(uvw * WG_SHAPE_RES, 0.5, WG_SHAPE_RES - 0.5);
    vec3 size = vec3(WG_ATLAS_COLS, WG_ATLAS_ROWS, 1.0) * WG_SHAPE_RES;
    float d = mix(WG_SHAPE_LO, WG_SHAPE_HI, texture(uWorldShapes, (origin + inTile) / size).r);
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
 * The sixteen primitives, blended by weight (the weights sum to one): six simple ones (tube and
 * torus ring the travel axis), a baked model, and nine fractals: a gyroid, a Menger sponge, a
 * Mandelbulb, a Mandelbox, a Sierpinski tetrahedron, an Apollonian packing, a quaternion Julia
 * set, a pseudo-Kleinian limit set and an octahedral kaleidoscopic IFS.
 */
float wPrims(vec3 q) {
    float s = WG_PRIM_SIZE;
    float t = WG_PRIM_THICK;
    vec3 a = abs(q);
    float d = 0.0;
#if (WG_PARTS & 1) != 0
    if (WG_P_BOX > 0.0) d += WG_P_BOX * (wBox(q, s * 0.6) - t * 0.3);
#endif
#if (WG_PARTS & 2) != 0
    if (WG_P_BAR > 0.0) d += WG_P_BAR * (min(max(a.x, a.y), min(max(a.y, a.z), max(a.z, a.x))) - t);
#endif
#if (WG_PARTS & 4) != 0
    if (WG_P_TUBE > 0.0) d += WG_P_TUBE * (abs(length(q.xy) - s) - t);
#endif
#if (WG_PARTS & 8) != 0
    if (WG_P_TORUS > 0.0) d += WG_P_TORUS * (length(vec2(length(q.xy) - s, q.z)) - t);
#endif
#if (WG_PARTS & 16) != 0
    if (WG_P_SHELL > 0.0) d += WG_P_SHELL * (abs(length(q) - s) - t);
#endif
#if (WG_PARTS & 32) != 0
    if (WG_P_SLAB > 0.0) d += WG_P_SLAB * (a.y - t);
#endif
#if (WG_PARTS & 64) != 0
    if (WG_P_MESH > 0.0) d += WG_P_MESH * wMesh(q);
#endif
#if (WG_PARTS & 128) != 0
    if (WG_P_GYROID > 0.0) d += WG_P_GYROID * wGyroid(q, WG_GYRO_SCALE, t);
#endif
#if (WG_PARTS & 256) != 0
    if (WG_P_MENGER > 0.0) d += WG_P_MENGER * wMenger(q, s);
#endif
#if (WG_PARTS & 512) != 0
    if (WG_P_BULB > 0.0) d += WG_P_BULB * wBulb(q, s, WG_BULB_POWER);
#endif
#if (WG_PARTS & 1024) != 0
    if (WG_P_MANDELBOX > 0.0) d += WG_P_MANDELBOX * wMandelbox(q, s, WG_BOX_SCALE);
#endif
#if (WG_PARTS & 2048) != 0
    if (WG_P_SIERP > 0.0) d += WG_P_SIERP * wSierp(q, s);
#endif
#if (WG_PARTS & 4096) != 0
    if (WG_P_APOLLO > 0.0) d += WG_P_APOLLO * wApollo(q, s);
#endif
#if (WG_PARTS & 8192) != 0
    if (WG_P_JULIA > 0.0) d += WG_P_JULIA * wJulia(q, s, vec4(WG_JULIA_CX, WG_JULIA_CY, WG_JULIA_CZ, WG_JULIA_CW));
#endif
#if (WG_PARTS & 16384) != 0
    if (WG_P_KLEIN > 0.0) d += WG_P_KLEIN * wKlein(q, s, WG_KLEIN_BOX, t);
#endif
#if (WG_PARTS & 32768) != 0
    if (WG_P_KIFS > 0.0) d += WG_P_KIFS * wKifs(q, s, WG_KIFS_SCALE);
#endif
    return d;
}

// ---- the landscape (WG_TERRAIN; WorldGenome.cpp terrain(), the same numbers) --------------------
//
// A height field y = base + relief * (ridged(x, z) + WALL * valley(x)), in world space. Its
// distance is the vertical gap times the textbook bound for a height field, 1 / sqrt(1 + s^2),
// where s is the steepest slope the field can have, so the sphere tracer never overshoots it; above
// the field's highest possible point the gap to that plane is a bound too, and the larger wins.
// The noise is integer-hashed value noise (no sin, no float hash), so the GPU and the CPU port
// build the same ground. The rig's travel wraps at 8192: the last stretch before the wrap blends
// into the ground found 8192 behind, so the eye crosses the wrap onto the same ground.

#define WG_TERRAIN_OCTAVES 5
#define WG_TERRAIN_GAIN 0.45
/**
 * The ridged sum's steepest slope per unit of base frequency and relief: value noise rises at most
 * 3 per lattice unit (the cubic fade's slope, 1.5, times a corner difference of at most 2), ridging
 * keeps that, and the octaves add sum(gain^i 2^i) / sum(gain^i) = 2.295 of it. 3 x 2.295, rounded up.
 */
#define WG_TERRAIN_SLOPE 6.9
/** The valley walls: up to twice the relief, half-way up at WIDTH from the travel line. */
#define WG_TERRAIN_WALL 2.0
#define WG_TERRAIN_WIDTH 8.0
/** The steepest the walls get per unit of relief: WALL x max(d/dx x^2 / (x^2 + W^2)) = WALL x 0.6495 / W. */
#define WG_TERRAIN_WALL_SLOPE 0.1625
/** How far under FLOOR_Y the ground waits while Terrain is off, and the distance it reads as then. */
#define WG_TERRAIN_SINK 8.0
#define WG_TERRAIN_FAR 80.0
/** CameraRig::kTravelWrap, and the stretch before it that blends into the ground behind the wrap. */
#define WG_TRAVEL_WRAP 8192.0
#define WG_WRAP_LO 7692.0
#define WG_WRAP_HI 8092.0
/** The blend's slope per unit of relief: smoothstep's 1.5 over the 400-unit stretch, rounded up. */
#define WG_WRAP_SLOPE 0.004

/** A lattice point's hash: Knuth's multiplicative hash per axis, then xor-shift-multiply rounds. */
uint wHash(int x, int y) {
    uint h = (uint(x) * 0x9E3779B1u) ^ (uint(y) * 0x85EBCA77u);
    h ^= h >> 15u;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12u;
    h *= 0x297A2D39u;
    h ^= h >> 15u;
    return h;
}

/** The lattice value at (x, y): the hash's top 24 bits as -1..1, exact in any float. */
float wLattice(int x, int y) { return float(wHash(x, y) >> 8u) * (1.0 / 8388608.0) - 1.0; }

/** Smooth value noise, -1..1, lattice unit 1, cubic fade: continuous, at most 3 per unit steep. */
float wValue(vec2 s) {
    vec2 i = floor(s);
    vec2 f = s - i;
    vec2 u = f * f * (3.0 - 2.0 * f);
    int x = int(i.x);
    int y = int(i.y);
    float a = wLattice(x, y);
    float b = wLattice(x + 1, y);
    float c = wLattice(x, y + 1);
    float d = wLattice(x + 1, y + 1);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

/**
 * Ridged fBm at s, 0..1: each octave's 1 - |n| is a crest where the noise crosses zero; the next
 * octave is turned (a 3-4-5 rotation) and twice as fine, so no lattice axis shows. `crest` gets
 * the smallest |n|: how near the point is to a ridge line, for the colour.
 */
float wRidged(vec2 s, out float crest) {
    float sum = 0.0;
    float norm = 0.0;
    float amp = 1.0;
    crest = 1.0;
    for (int i = 0; i < WG_TERRAIN_OCTAVES; i++) {
        float n = wValue(s);
        sum += amp * (1.0 - abs(n));
        norm += amp;
        crest = min(crest, abs(n));
        amp *= WG_TERRAIN_GAIN;
        s = vec2(0.8 * s.x - 0.6 * s.y, 0.6 * s.x + 0.8 * s.y) * 2.0 + vec2(5.3, -2.9);
    }
    return sum / norm;
}

/** The ridged ground at world (x, z), the wrap's last stretch blended into the ground behind it. */
float wLandscape(vec2 xz, out float crest) {
    float h = wRidged(xz * WG_TERRAIN_SCALE, crest);
    float w = smoothstep(WG_WRAP_LO, WG_WRAP_HI, xz.y);
    if (w > 0.0) {
        float c2;
        float h2 = wRidged(vec2(xz.x, xz.y - WG_TRAVEL_WRAP) * WG_TERRAIN_SCALE, c2);
        h = mix(h, h2, w);
        crest = mix(crest, c2, w);
    }
    return h;
}

/** The valley across the way: 0 on the travel line, toward 1 far out to the sides. */
float wValley(float x) { return x * x / (x * x + WG_TERRAIN_WIDTH * WG_TERRAIN_WIDTH); }

/**
 * The landscape's distance at p and its orbit trap (height, wall, crest, and a radius that darkens
 * the valley floors). As Terrain fades in, the ground rises from SINK under FLOOR_Y and its relief
 * grows; near 0 the distance itself fades to FAR, so the layer leaves and arrives without a pop.
 */
float wTerrain(vec3 p, out vec4 trap) {
    float on = WG_TERRAIN;
    float relief = on * WG_TERRAIN_HEIGHT;
    float base = WG_FLOOR_Y - (1.0 - on) * WG_TERRAIN_SINK;
    float crest;
    float h = wLandscape(p.xz, crest);
    float v = wValley(p.x);
    float y = base + relief * (h + WG_TERRAIN_WALL * v);
    float slope = relief * (WG_TERRAIN_SLOPE * WG_TERRAIN_SCALE + WG_TERRAIN_WALL_SLOPE + WG_WRAP_SLOPE);
    float d = (p.y - y) / sqrt(1.0 + slope * slope);
    d = max(d, p.y - (base + relief * (1.0 + WG_TERRAIN_WALL)));
    trap = vec4(h, v, crest, (0.4 + 1.2 * h) * (0.4 + 1.2 * h));
    return mix(WG_TERRAIN_FAR, d, min(on * 8.0, 1.0));
}

/** The chain's matter at distance d joined with the landscape; the nearer one owns the trap. */
float wWithTerrain(vec3 p, float d) {
    vec4 trap;
    float t = wTerrain(p, trap);
    if (t < d) gWTrap = trap;
    return min(d, t);
}

/** The bare world at p: the operator chain, then the primitives, then the landscape. Writes gWTrap. */
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
    if (WG_LATTICE_TILT != 0.0 || WG_LATTICE_YAW != 0.0) {
        // The lattice turned off the view: tipped about x, then turned about y, so its rows no
        // longer meet on the travel axis as a 4-way cross. A rotation is an isometry, so distances
        // stay exact. It turns the folded z too, and that turned z is folded again on the same
        // period, so the tipped lattice fills the whole slab instead of a band; everything here
        // reads only the folded z, so the travel axis' repeat above stays periodic. The corridor
        // is carved in unrotated eye space (worldCarved()), so the way stays open.
        w.yz = wTurn(w.yz, gWTilt);
        w.zx = wTurn(w.zx, gWYaw);
        w.z = wCell(w.z, pz);
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
        r.xy = wTurn(r.xy, gWIfsA);
        r.yz = wTurn(r.yz, gWIfsB);
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
#if (WG_PARTS & 65536) != 0
    if (WG_TERRAIN > 0.0) d = wWithTerrain(p, d);
#endif
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

/** Where worldRender() is: a mode's march, the hit's four normal taps, or the glossy ground's mirror. */
#define WR_SURFACE 0
#define WR_NORMAL 1
#define WR_MIRROR 2
#define WR_GLOW 3
#define WR_DENSITY 4
#define WR_DONE 5
/** The most evaluations one pixel can take: the surface march, 4 taps, the mirror, the glow and the density. */
#define WR_MAX_STEPS 252

/** The first mode at or after `from` that has weight, else WR_DONE. */
int worldNextMode(int from, float wS, float wG, float wD) {
    if (from <= WR_SURFACE && wS > 0.01) return WR_SURFACE;
    if (from <= WR_GLOW && wG > 0.01) return WR_GLOW;
    if (from <= WR_DENSITY && wD > 0.01) return WR_DENSITY;
    return WR_DONE;
}

/**
 * The world seen three ways, crossfaded by the genome's mode weights (a weight under 0.01 is not
 * marched, the rest are rescaled so the skip never dims the frame):
 *
 * - the hard surface: sphere-traced to a hit (`budget` steps), lit by a light that rides above the
 *   eye, a rim and an edge glow, occluded by the trap and the step count, fogged; a glossy ground
 *   mirrors the world along the reflected ray (a short trace, a quarter of the budget, at most 24);
 * - glow-through: marched on |d| with a minimum step, so the ray passes through matter and gathers
 *   light from every surface it nears (translucent glowing plates), half the budget, at most 56;
 * - emissive density: a thin band about the iso-surface (WG_DENS_BAND wide) as glowing sheets,
 *   emission-absorption at even steps from a jittered start, 0.35 of the budget, at most 40.
 *
 * All of it is one loop around one worldCarved() call: each pass evaluates the world once, where
 * the stage it is in asks, then the stage steps or hands over. A GPU compiler inlines world() at every
 * call site, and world() is the whole chain and sixteen primitives: nine call sites (three marches,
 * four normal taps, the hit's trap, the mirror) made a shader ten times the size of any other style's.
 */
vec3 worldRender(vec3 ro, vec3 rd, float budget, float jitter) {
    float wS = WG_M_SURFACE;
    float wG = WG_M_GLOW;
    float wD = WG_M_DENSITY;
    float far = worldFar();
    vec3 fogCol = worldFogColour(rd);
    vec3 col = vec3(0.0);

    int mode = worldNextMode(WR_SURFACE, wS, wG, wD);
    int i = 0;          // the stage's step
    float t = 0.0;      // the stage's distance along its ray
    vec3 o = ro;        // the stage's ray
    vec3 dir = rd;
    // The surface: its hit, its normal, its colour before the mirror.
    float tHit = 0.0;
    float used = 0.0;
    vec3 n = vec3(0.0);
    vec3 surf = vec3(0.0);
    vec4 trap = vec4(0.0);
    float ground = 0.0;
    float mix_ = 0.0;
    // The volume modes.
    float stepCap = 0.0;
    float dt = 0.0;
    float trans = 1.0;
    vec3 acc = vec3(0.0);
    vec2 h = vec2(1.0, -1.0);
    bool enter = true;
    worldPrepare();

    for (int k = 0; k < WR_MAX_STEPS; k++) {
        if (mode == WR_DONE) break;
        if (enter) {
            // A mode's march starts on the eye's ray (the volumes from a jittered start).
            enter = false;
            o = ro;
            dir = rd;
            i = 0;
            acc = vec3(0.0);
            if (mode == WR_SURFACE) {
                t = 0.05;
            } else if (mode == WR_GLOW) {
                t = 0.1 + 0.15 * jitter;
            } else {
                stepCap = max(min(budget * 0.35, 40.0), 8.0);
                dt = far * 0.6 / stepCap;
                t = 0.1 + dt * jitter;
                trans = 1.0;
            }
        }
        vec3 p = o + dir * t;
        float d = worldCarved(p);
        int next = mode;

        if (mode == WR_SURFACE) {
            used = float(i);
            if (d < 0.0012 * (1.0 + t)) {
                // A hit: keep what world() said here (the trap, the ground), then take the normal.
                tHit = t;
                trap = gWTrap;
                ground = gWFloor;
                o = p;
                next = WR_NORMAL;
            } else {
                t += d * 0.75;
                i++;
                if (t > far || float(i) >= min(budget, 128.0)) {
                    col += wS * fogCol;
                    next = worldNextMode(WR_GLOW, wS, wG, wD);
                }
            }
            if (next == WR_NORMAL) {
                i = 0;
                n = vec3(0.0);
                t = 0.0015 * (1.0 + tHit);
                dir = h.xyy;
            }
        } else if (mode == WR_NORMAL) {
            n += dir * d;
            i++;
            if (i < 4) {
                dir = i == 1 ? h.yyx : (i == 2 ? h.yxy : h.xxx);
            } else {
                n = normalize(n);
                vec3 hp = o;
                float detail = worldDetail(hp, n);
                vec3 base = pal(worldHue(trap)) * mix(1.0, 0.3 + 1.4 * detail, WG_TEX_AMOUNT);
                // The ground takes the world's palette, darker, so the structures stand on it, not in it.
                base *= 1.0 - 0.45 * ground;
                float pattern = mix(1.0, 2.0 * detail, WG_TEX_EMIT);
                vec3 l = normalize(uCamBasis[1] * 0.8 - rd * 0.6 + vec3(0.0, 0.3, 0.0));
                float diff = max(dot(n, l), 0.0);
                float rim = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
                float occ = clamp(1.0 - used / max(budget, 1.0), 0.15, 1.0) * clamp(0.35 + sqrt(trap.w) * 0.5, 0.25, 1.0);
                // The far ground is seen at a grazing angle, where the rim would light all of it: damp it there.
                surf = base * (0.12 + 1.0 * diff) * occ + base * rim * 0.45 * WG_EMISSION * pattern * (1.0 - 0.7 * ground);
                float spec = pow(max(dot(reflect(rd, n), l), 0.0), 24.0);
                surf += vec3(spec) * 0.25 * occ;
                float gloss = ground * WG_FLOOR_GLOSS;
                if (gloss > 0.02) {
                    // A glossy ground: Schlick's fresnel weighs what it mirrors, more at grazing angles.
                    float fres = 0.04 + 0.96 * pow(1.0 - max(dot(n, -rd), 0.0), 5.0);
                    mix_ = gloss * (0.25 + 0.75 * fres);
                    o = hp + n * 0.01;
                    dir = reflect(rd, n);
                    t = 0.08;
                    i = 0;
                    next = WR_MIRROR;
                } else {
                    col += wS * mix(surf, fogCol, 1.0 - exp(-tHit * WG_FOG));
                    next = worldNextMode(WR_GLOW, wS, wG, wD);
                }
            }
        } else if (mode == WR_MIRROR) {
            // What the glossy ground mirrors: the world's colour where the reflected ray lands (lit
            // flat, fogged), else the air.
            bool landed = d < 0.004 * (1.0 + t);
            if (!landed) {
                t += d * 0.85;
                i++;
            }
            if (landed || t > far || float(i) >= min(budget * 0.25, 24.0)) {
                vec3 seen = worldFogColour(dir);
                if (landed) seen = mix(pal(worldHue(gWTrap)) * (0.2 + 0.4 * WG_EMISSION), seen, 1.0 - exp(-t * WG_FOG));
                col += wS * mix(mix(surf, seen, mix_), fogCol, 1.0 - exp(-tHit * WG_FOG));
                next = worldNextMode(WR_GLOW, wS, wG, wD);
            }
        } else if (mode == WR_GLOW) {
            float ad = abs(d);
            float stepLen = max(ad * 0.6, 0.06 + t * 0.012);
            float pattern = WG_TEX_EMIT > 0.01 ? mix(1.0, 2.0 * worldDetailFlat(p), WG_TEX_EMIT) : 1.0;
            acc += pal(worldHue(gWTrap)) * exp(-ad * 9.0) * stepLen * exp(-t * WG_FOG) * pattern;
            t += stepLen;
            i++;
            if (t > far * 0.8 || float(i) >= min(budget * 0.5, 56.0)) {
                col += wG * (acc * 0.6 * WG_EMISSION + fogCol);
                next = worldNextMode(WR_DENSITY, wS, wG, wD);
            }
        } else {
            float band = WG_DENS_BAND;
            float dens = exp(-(d * d) / (band * band)) * (0.6 + 0.6 * clamp(1.0 - sqrt(gWTrap.w) * 0.3, 0.0, 1.0));
            if (WG_TEX_EMIT > 0.01) dens *= mix(1.0, 2.0 * worldDetailFlat(p), WG_TEX_EMIT);
            float a = 1.0 - exp(-dens * dt * 1.4);
            acc += trans * a * pal(worldHue(gWTrap)) * exp(-t * WG_FOG * 0.5);
            trans *= 1.0 - a * 0.5;
            t += dt;
            i++;
            if (trans < 0.03 || float(i) >= stepCap) {
                col += wD * (acc * 0.9 * WG_EMISSION + fogCol * trans);
                next = WR_DONE;
            }
        }

        enter = next != mode && next != WR_NORMAL && next != WR_MIRROR;
        mode = next;
    }
    float kept = (wS > 0.01 ? wS : 0.0) + (wG > 0.01 ? wG : 0.0) + (wD > 0.01 ? wD : 0.0);
    return col / max(kept, 0.01);
}
