// The Shard family: broken glass hung in the dark. Splinters, panes, spires,
// plates and rods, every piece flat-faced with sharp edges; the camera rig
// flies through them or turns about them.
//
// WHAT IS DRAWN. Each style gives one signed distance, shardMap(p), with two
// things beside it: the piece's own number and the distance to its nearest
// edge. A piece is an intersection of planes (shardTop keeps the two nearest),
// so its faces are flat and the edge distance is exact where two faces meet.
// The ray is sphere-traced to the nearest face, shaded, and carried on
// through: up to SHARD_HITS faces along one ray, each dimmer than the last.
// A face is a thin film on glass: its colour runs through the palette with
// the film's depth and the angle the light crosses it at, so each flat face
// holds one sheen that shifts as the piece turns. A face also mirrors a soft
// sky and one key light, so a piece flashes when it turns into the light.
// At every edge the light splits: three fine lines a little apart, each a
// different place in the palette. A ray that passes close to an edge without
// touching it picks up a faint line of the same light; between the pieces
// the dark stays dark.
//
// COST. One loop of SHARD_STEPS steps (80 at the default Detail, 100 at
// most), each one shardMap; a face adds four more for its normal, so at most
// 100 + 4 * SHARD_HITS = 112 shardMap per pixel.
// The maps are a few planes per piece, one or two pieces per call; no noise
// and no texture. The palette is read five times per pixel before the loop
// (shardSetup) and mixed after.
//
// THE CONTROLS, read normalized through lib_neon:
//
//   Morph       gShardForm   the pieces' own metamorphosis, named by each style
//   Warp        gShardFlow   shardBend(): space twists about the way ahead,
//                            bows and shears along it, so the pieces flex
//   Turbulence  gShardChaos  a shiver over every face; the edges stay straight
//   Intensity   neonGlow()   the emission
//   k0 PULSE the emission, k1 TEXTURE the edge lines' width, k2 FLOW the bend,
//   k3 TONE the palette position, k4 BUILD the film's bands, k5 SPACE the reach.
//
// THE SOUND, heard the way the older families hear it (lib_dive, lib_neon):
// lib_sound's shaped reads, in shardSetup(), on the motion only, never on a
// colour, a brightness or a beat.
//
//   gShardSwell  the low band, sndShaped(SND_LOW), deepens the Warp's bow,
//   never past the Warp's own reach.
//
// It is times SND_DRIVE, the music's absolute magnitude (lib_sound): how
// MUCH moves follows how loud and dense the music is, never the song's own
// average, and the band only shapes the push. sndShaped scales it by Audio
// drive (uSoundDrive). Like Dive and Glass, no style here has a spin rate
// (catalogue::spinRate, owner 2026-11-12), so Spin drift does not turn it.
// No sound band turns it either: the mids' push round the way ahead is gone
// (owner 2026-11-12, no style spins to the music).
//
// What moves on a hit is the reaction matrix, through the params above. uTime
// stops in silence and SND_DRIVE falls to 0, so everything here comes to rest.
//
// The world repeats along z every SHARD_PERIOD (16, dividing the rig's 8192),
// and every clock phase is whole cycles per uTime wrap (SHARD_RATE).
//
// GROW AND LAYERS (lib_grow, shardField): Spawn slides a second hoard of
// glass off the first along the way, Fractal splinters glass a third the size
// off every face, Front layer opens the glass round the eye and Back layer
// the glass down the way. Each costs nothing at 0.
//
// INCLUDE ORDER: after lib_scene_uniforms (with VIEW_OWNS_SHAPE),
// lib_scene_motion, lib_camera, lib_palette, lib_scene_grade and lib_neon.
// The style then defines vec3 shardMap(vec3 p), its pieces at p (x the signed
// distance to the nearest face, y the piece's own number 0..1, z the distance
// to that piece's nearest edge), and after it includes lib_shard_march, which
// calls it.

#define SHARD_TAU 6.2831853
#define SHARD_MAX_STEPS 100
/** uSteps (64..128 by Detail, 102 at its default) on this loop, as the Opal march spends it. */
#define SHARD_STEPS (min(uSteps * 0.78, float(SHARD_MAX_STEPS)))
#define SHARD_HITS 3
#define SHARD_PERIOD 16.0
/** k whole cycles per uTime wrap (7100 s, ShaderScene). */
#define SHARD_RATE(k) (SHARD_TAU * float(k) / 7100.0)

float gShardForm;
float gShardFlow;
float gShardChaos;
/** The low band's swell of the bend, 0..1.5 (see the header). */
float gShardSwell;
/** How far the trace reaches, world units. */
float gShardFar;
/** How many times a face's film runs through the palette as its depth goes from thin to thick. */
float gShardBands;
/** An edge line's half-width at the eye, world units; it widens with distance. */
float gShardEdge;
float gShardTone;
/** The share of a step taken: under 1 where a style's distance is only a bound. */
float gShardStride;
/** How far the trace jumps on past a face, so a thin pane's back face is not counted as a second piece. */
float gShardSkip;
vec3 gShardPal0;
vec3 gShardPal1;
vec3 gShardPal2;
vec3 gShardPal3;
vec3 gShardPal4;

mat2 shardRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

float shardHash(vec2 c) {
    return fract(sin(dot(c, vec2(127.1, 311.7))) * 43758.5453);
}

/**
 * Keeps the two largest plane values seen in top (x the largest). A convex piece is
 * the largest of its planes' values; where the two largest are both near 0 two faces
 * meet, so length(top) is the distance to the nearest edge. Written with max and min, not a
 * choice between two vectors, so the trace's loop (a piece takes six of these, a field several
 * pieces, a step five fields) holds no branch per plane: CI's software GPU stalled on the first
 * Shard style while this was a choice (glslcheck, 2026-11-12).
 */
void shardTop(inout vec2 top, float v) {
    top = vec2(max(top.x, v), max(top.y, min(top.x, v)));
}

/**
 * One broken piece of pane in its own frame: a slab `thick` either side of z = 0, cut
 * by five straight breaks whose distances from the middle `h` (0..1) picks, so no two
 * pieces share a shape. Every corner lies within 1.5 * size of the middle. `edge` is
 * the distance to its nearest edge.
 */
float shardPiece(vec3 q, float h, float size, float thick, out float edge) {
    vec2 top = vec2(-1.0e3);
    shardTop(top, abs(q.z) - thick);
    shardTop(top, q.x - size * (0.4 + 0.6 * fract(h * 7.31)));
    shardTop(top, dot(q.xy, vec2(0.1736, 0.9848)) - size * (0.4 + 0.6 * fract(h * 13.7)));
    shardTop(top, dot(q.xy, vec2(-0.9962, 0.0872)) - size * (0.4 + 0.6 * fract(h * 21.3)));
    shardTop(top, dot(q.xy, vec2(-0.0872, -0.9962)) - size * (0.4 + 0.6 * fract(h * 31.9)));
    // The fifth break only ever takes off a corner.
    shardTop(top, dot(q.xy, vec2(0.7071068, -0.7071068)) - size * (0.6 + 0.9 * fract(h * 43.1)));
    edge = length(top);
    return top.x;
}

/** The user's palette at t, from the five reads of shardSetup(): there and back, so no seam. */
vec3 shardCol(float t) {
    float x = 4.0 * abs(2.0 * fract(0.5 * t) - 1.0);
    vec3 c = mix(gShardPal0, gShardPal1, smoothstep(0.0, 1.0, clamp(x, 0.0, 1.0)));
    c = mix(c, gShardPal2, smoothstep(0.0, 1.0, clamp(x - 1.0, 0.0, 1.0)));
    c = mix(c, gShardPal3, smoothstep(0.0, 1.0, clamp(x - 2.0, 0.0, 1.0)));
    return mix(c, gShardPal4, smoothstep(0.0, 1.0, clamp(x - 3.0, 0.0, 1.0)));
}

void shardSetup() {
    gShardForm = neonForm();
    gShardFlow = clamp(neonFlow() + 0.5 * neonKnob(2), 0.0, 1.0);
    gShardChaos = neonChaos();
    gShardFar = 12.0 * (1.0 + 0.4 * neonKnob(5));
    gShardBands = 1.4 * (1.0 + 0.8 * neonKnob(4));
    gShardEdge = 0.012 * (1.0 + neonKnob(1));
    gShardTone = 0.6 * neonKnob(3);
    gShardStride = 0.8;
    gShardSkip = 0.06;
    // The sound (see the header): the absolute drive sets how much, the band only the shape.
    float drive = clamp(SND_DRIVE, 0.0, 1.0);
    gShardSwell = sndShaped(SND_LOW) * drive;
    gShardPal0 = pal(0.0);
    gShardPal1 = pal(0.25);
    gShardPal2 = pal(0.5);
    gShardPal3 = pal(0.75);
    gShardPal4 = pal(1.0);
}

/** Warp on space: a twist about z that comes and goes along the way, an upward bow (and the lows'), a shear along the way. */
vec3 shardBend(vec3 p) {
    float zc = p.z * (SHARD_TAU / SHARD_PERIOD);
    p.xy = shardRot(gShardFlow * 0.8 * sin(zc + 0.5 * p.x + uTime * SHARD_RATE(130))) * p.xy;
    p.y += 0.45 * min(gShardFlow + 0.3 * gShardSwell, 1.0) * sin(2.0 * zc + 1.2 * p.x + uTime * SHARD_RATE(200));
    p.z += gShardFlow * 0.35 * sin(zc + 1.1 * p.y);
    return p;
}

/** The eye, wrapped into one period and held within `bore` of the axis, for a style that keeps the way open only there. */
vec3 shardEye(vec3 ro, float bore) {
    ro.z = mod(ro.z, SHARD_PERIOD);
    float r = length(ro.xy);
    if (r > bore) ro.xy *= bore / r;
    // Grow and layers measure their depth from here (lib_grow).
    growEye(ro);
    return ro;
}

float shardDither() {
    vec2 f = gl_FragCoord.xy;
    return fract(52.9829189 * fract(dot(f, vec2(0.06711056, 0.00583715))));
}

/** `exposure` is the style's own; Intensity and PULSE scale the emission. */
vec4 shardFinish(vec3 col, float exposure) {
    col *= exposure * neonGlow() * (1.0 + 0.5 * neonKnob(0));
    col = max(mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, 1.2), 0.0);
    col = grade(neonAces(col));
    // No pixel leaves as a NaN or an infinity: the feedback pass would carry it for good.
    if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);
    return vec4(col, 1.0);
}

/**
 * A piece seen through lib_strata's fold of kind k (k < 0: no fold), `iters` folds about the
 * corner c, in units of `span` world units: one piece becomes a cluster of its own mirror images,
 * each fold's smaller. x the distance, y the edge's, both back in world units. The planes stay
 * planes, so the faces stay flat and the edges exact. Needs lib_strata before this library.
 */
vec2 shardFolded(vec3 q, float k, float iters, float span, vec3 c, float h, float size, float thick) {
    float dr = 1.0;
    if (k >= 0.0) {
        dr = 1.0 / span;
        q = strataFold(q * dr, k, iters, c, shardRot(0.5 + 0.6 * h), dr);
    }
    float edge;
    float d = shardPiece(q, h, size, thick, edge);
    return vec2(d, edge) / dr;
}
