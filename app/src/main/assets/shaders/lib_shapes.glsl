// lib_shapes: the shape graph (core/viz/ShapeGraph) drawn as a distance field, and melted into a raymarched
// style's own form by Shapes (uShapeMix; Customize › Shape › Grow › Shapes).
//
// WHAT IT DRAWS. Up to eight primitives in a one-level CSG list the native graph keeps (lib_scene_uniforms'
// uShapes: four vec4 a shape, uShapeCount, uShapeFold, uShapeMelt; ShapeGraph.hpp has the layout). A head opens
// a group, the children after it combine onto the group, and the finished group lands on the scene by the
// head's operation (union, smooth blend, cut, mask). Everything that moves (a glide, a primitive or operation
// crossfade, a shape growing in or shrinking away, the drift) moves natively, once a frame, on the renderer's
// clocks: this file only reads the numbers, so live, an export, the wallpaper and the dream draw the same graph.
//
// THE HOST MIRROR. ShapeGraph::sample and ShapeGraph::sampleInto are this file's maths, line for line, so the
// host tests (core/tests/shape_graph_test) hold the shader's rules: presence 0 leaves no trace, the field is
// 1-Lipschitz, the colour blends by the smooth min's own factor. A change here changes them in the same commit.
//
// COLOUR. A shape carries a colour INDEX, 0..1 along the style's own palette, never a colour: the style reads
// it through its pal(), from its own hue on (shapesHue), so the palette, saturation and brightness stay the
// style's and the user's. The smooth operations carry the index with the same blend factor as the distance
// (sminCol), so a seam between two shapes takes the colour between them exactly where the surfaces melt.
//
// NO TURN, NO CLOCK, NO SOUND. Nothing here rotates (the fold is a mirror repeat about the vertical axis, a
// fixed reflection, not a turn), nothing reads uTime, and nothing reads the sound: the music reaches the graph
// only through the params it is a function of (the reaction matrix, the waves, the envelopes, the macro knobs
// and the Auto Director), natively.
//
// THE DEFAULT IS THE OLD PICTURE. shapesInto returns its distance and leaves the colour untouched, without
// reading anything else, while uShapeMix is 0 or the graph is empty: a style at the default draws exactly
// what it drew.
//
// HOW A STYLE USES IT. Include after lib_scene_uniforms. In the style's map, after its own field:
//
//     gShapeHue = 0.0;
//     return shapesInto(ownDistance, p, gShapeHue);
//
// and at the hit (after the map that restores the hit's own globals) read the hue its palette draws with as
//
//     float hue = shapesHue(ownHue, baseHue, gShapeHue, gShapeShare);
//
// A style clipped to a bounding sphere about the origin widens it to SHAPES_REACH while uShapeMix > 0.
// No prototypes (style_reads_test): every function is defined before its first call.

#define SHAPES_MAX 8
/** ShapeGraph's kFar: the distance with nothing present. */
#define SHAPES_FAR 1000.0
#define SHAPES_TAU 6.2831853
/**
 * ShapeGraph's kReach: no present shape reaches past this radius about the origin, folded or not (the farthest
 * head, a child beside it and its largest primitive, the drift, the smooth blends' swell stacked at every level
 * of the list and the melt into the style come to under 2.5 at the very worst), so a style that clips its march
 * to a sphere about its subject widens the sphere to this.
 */
#define SHAPES_REACH 2.75

/**
 * How much of the colour at the last shapesInto is the graph's: 0 the style's own, 1 the graph's alone.
 * shapesInto handed a colour of 0.0 leaves exactly this times the graph's index in it, so shapesHue can put
 * the style's own hue back at the hit without a second evaluation.
 */
float gShapeShare = 0.0;

/**
 * The polynomial smooth minimum of two (distance, colour index) pairs, the colour blended by the same factor
 * as the distance: h is a's weight, 1 deep inside a, 0 deep inside b, a half on the seam. ShapeGraph's sminCol.
 */
vec2 sminCol(vec2 a, vec2 b, float k) {
    k = max(k, 1e-4);
    float h = clamp(0.5 + 0.5 * (b.x - a.x) / k, 0.0, 1.0);
    return vec2(mix(b.x, a.x, h) - k * h * (1.0 - h), mix(b.y, a.y, h));
}

/** The smooth maximum, sminCol reflected (intersect; a cut is smaxCol against the negated cutter). */
vec2 smaxCol(vec2 a, vec2 b, float k) {
    vec2 r = sminCol(vec2(-a.x, a.y), vec2(-b.x, b.y), k);
    return vec2(-r.x, r.y);
}

/** An axis-aligned block of half extents e. */
float shpBlock(vec3 q, vec3 e) {
    vec3 d = abs(q) - e;
    return length(max(d, 0.0)) + min(max(d.x, max(d.y, d.z)), 0.0);
}

/**
 * Primitive `kind` (ShapeGraph's Prim) at q, sizes e: ball, block, rounded block, ring about y, capsule along y,
 * diamond (the octahedron's bound, scaled to stay 1-Lipschitz), drum along y. Every one is a 1-Lipschitz bound.
 */
float shpPrim(int kind, vec3 q, vec3 e) {
    if (kind == 0) return length(q) - e.x;
    if (kind == 1) return shpBlock(q, e);
    if (kind == 2) {
        float r = 0.25 * min(e.x, min(e.y, e.z));
        return shpBlock(q, e - r) - r;
    }
    if (kind == 3) return length(vec2(length(q.xz) - e.x, q.y)) - e.y;
    if (kind == 4) {
        float y = q.y - clamp(q.y, -e.y, e.y);
        return length(vec3(q.x, y, q.z)) - e.x;
    }
    if (kind == 5) return (abs(q.x) + abs(q.y) + abs(q.z) - e.x) * 0.57735027;
    float dr = length(q.xz) - e.x;
    float dh = abs(q.y) - e.y;
    return min(max(dr, dh), 0.0) + length(max(vec2(dr, dh), 0.0));
}

/** A shape's distance at q: primitive A, crossfaded toward B by the shape's weight (a convex mix of two bounds). */
float shpPrimMix(vec4 size, vec4 prims, vec3 q) {
    float a = shpPrim(int(prims.x + 0.5), q, size.xyz);
    if (prims.z <= 0.0) return a;
    return mix(a, shpPrim(int(prims.y + 0.5), q, size.xyz), prims.z);
}

/**
 * Shape s onto what came before it, acc, by operation `op` (union, blend, cut, mask) at blend radius k. A shape
 * on its way in or out is lifted off by up to one and a half blend radii, past the reach of its own and its
 * group's blends, so at presence 0 it touches nothing; a mask, which only removes, is faded by presence instead.
 */
vec2 shpOpOnto(vec2 acc, vec2 s, int op, float k, float presence) {
    vec2 lifted = vec2(s.x + (1.0 - presence) * 1.5 * max(k, 1e-4), s.y);
    if (op == 0) return lifted.x < acc.x ? lifted : acc;
    if (op == 1) return sminCol(acc, lifted, k);
    if (op == 2) return smaxCol(acc, vec2(-lifted.x, lifted.y), k);
    vec2 m = smaxCol(acc, s, k);
    return mix(acc, m, presence);
}

/** The shape's operation A, crossfaded toward B by its weight; ops is the shape's fourth vec4. */
vec2 shpOpsOnto(vec2 acc, vec2 s, vec4 ops, float presence) {
    vec2 a = shpOpOnto(acc, s, int(ops.x + 0.5), ops.w, presence);
    if (ops.z <= 0.0) return a;
    vec2 b = shpOpOnto(acc, s, int(ops.y + 0.5), ops.w, presence);
    return mix(a, b, ops.z);
}

/**
 * An angle about the vertical axis folded into one of `folds` mirrored sectors, into [0, half a sector]: a
 * reflection, so the graph repeats round the axis without anything turning. Continuous all the way round for a
 * whole count, with a slope of one either way.
 */
float shpFoldAngle(float ang, float folds) {
    float n = max(folds, 1.0);
    float sector = SHAPES_TAU / n;
    float m = ang - sector * floor(ang / sector);
    return abs(m - 0.5 * sector);
}

/**
 * p folded by uShapeFold's counts, the count crossfaded A→B on the folded ANGLE: both folded angles lie within a
 * quarter turn, so their mix crosses no seam, keeps the radius and the height and only draws the angle in (a
 * 1-Lipschitz map of space). The axis itself has no angle (atan is undefined at the origin on some drivers),
 * and any angle lands it on the axis, so it reads as 0.
 */
vec3 shpFoldSpace(vec3 p) {
    float r = length(p.xz);
    float ang = r > 0.0 ? atan(p.z, p.x) : 0.0;
    float a = shpFoldAngle(ang, uShapeFold.x);
    if (uShapeFold.z > 0.0) a = mix(a, shpFoldAngle(ang, uShapeFold.y), uShapeFold.z);
    return vec3(r * cos(a), p.y, r * sin(a));
}

/**
 * The graph's list walked at q: x its distance (SHAPES_FAR with nothing present), y its colour index, z 1 once
 * a present group was drawn. ShapeGraph's graphAt, step for step.
 */
vec3 shpGraph(vec3 q) {
    vec2 scene = vec2(SHAPES_FAR, 0.0);
    vec2 group = vec2(SHAPES_FAR, 0.0);
    vec4 land = vec4(0.0);
    float landPresence = 0.0;
    bool inGroup = false;
    bool drew = false;
    for (int i = 0; i < SHAPES_MAX; i++) {
        if (i >= uShapeCount) break;
        vec4 place = uShapes[4 * i];
        vec4 size = uShapes[4 * i + 1];
        vec4 prims = uShapes[4 * i + 2];
        vec4 ops = uShapes[4 * i + 3];
        float presence = prims.w;
        if (place.w < 0.5) {
            // A head: the group before it lands, and a present head opens the next.
            if (inGroup) scene = shpOpsOnto(scene, group, land, landPresence);
            inGroup = presence > 0.0;
            if (!inGroup) continue;
            drew = true;
            group = vec2(shpPrimMix(size, prims, q - place.xyz), size.w);
            land = ops;
            landPresence = presence;
        } else {
            if (!inGroup || presence <= 0.0) continue;
            group = shpOpsOnto(group, vec2(shpPrimMix(size, prims, q - place.xyz), size.w), ops, presence);
        }
    }
    if (inGroup) scene = shpOpsOnto(scene, group, land, landPresence);
    return vec3(scene, drew ? 1.0 : 0.0);
}

/**
 * The graph alone at p: x its distance (SHAPES_FAR with nothing present), y its colour index, z 1 once a
 * present group was drawn (0: the graph is empty), w 0. Folded all the way, it is the graph in folded space;
 * partway, the plain and the folded graph's distances crossfaded (two evaluations, only while the fold is
 * moving or Fractal holds it partway), never the two points, whose mix would pull far space in onto the axis
 * and draw the graph again out there. ShapeGraph::sample, step for step.
 */
vec4 shapesScene(vec3 p) {
    if (uShapeCount <= 0) return vec4(SHAPES_FAR, 0.0, 0.0, 0.0);
    float amount = uShapeFold.w;
    if (amount <= 0.0) return vec4(shpGraph(p), 0.0);
    vec3 folded = shpGraph(shpFoldSpace(p));
    if (amount >= 1.0) return vec4(folded, 0.0);
    vec3 plain = shpGraph(p);
    return vec4(mix(plain.xy, folded.xy, amount), folded.z, 0.0);
}

/**
 * A style's own distance d and colour index col at p, with the graph melted in by uShapeMix: the smooth union
 * of the two at uShapeMelt, mixed over the style's own by uShapeMix (a convex mix of two 1-Lipschitz fields,
 * so a march that was safe stays safe). d and col untouched while uShapeMix is 0 or the graph is empty.
 * Leaves gShapeShare. ShapeGraph::sampleInto.
 */
float shapesInto(float d, vec3 p, inout float col) {
    gShapeShare = 0.0;
    if (!(uShapeMix > 0.0) || uShapeCount <= 0) return d;
    vec4 s = shapesScene(p);
    vec2 melted = sminCol(vec2(d, col), s.xy, uShapeMelt);
    // sminCol's own factor for the style's side: the colour moves toward the graph's by (1 - h) of the melt.
    float h = clamp(0.5 + 0.5 * (s.x - d) / max(uShapeMelt, 1e-4), 0.0, 1.0);
    gShapeShare = uShapeMix * (1.0 - h);
    col = mix(col, melted.y, uShapeMix);
    return mix(d, melted.x, uShapeMix);
}

/**
 * The palette index a style draws its hit with: `own` its own index there, the graph's colour read from the
 * style's `base` hue on. `graph` and `share` are what shapesInto left in a colour handed 0.0 and in
 * gShapeShare at the hit, so this is what shapesInto would have returned had it been handed `own`, with the
 * graph's index offset by `base`. Exactly `own` while the graph is off (both 0).
 */
float shapesHue(float own, float base, float graph, float share) {
    return own + share * (base - own) + graph;
}
