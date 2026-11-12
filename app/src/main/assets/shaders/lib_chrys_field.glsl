// The Chrysanthemum family's one field: the part of lib_chrys that calls the
// style's chrysMap, which the style defines between the two includes, and the
// normal that differentiates it.
//
// Why a second file: no style in the app declares a function ahead of its
// body (style_reads_test: noStyleDeclaresAFunctionAhead), so the field that
// calls chrysMap comes after the style has defined it, as in Prism, Petal and
// the other families with a march include.
//
// Every Chrysanthemum style marches, shades and differentiates through
// chrysField and chrysNormal, never its map directly, so whatever wraps the
// field wraps the whole style once.
//
// INCLUDE ORDER: after lib_chrys and after the style's ChrysHit chrysMap(vec3 p).

/**
 * GROW AND FORMULA HOOK. Where lib_grow (Spawn, Fractal, Front and Back layer)
 * and uFormula will wrap the style's map F at p once they land: today the map
 * itself. Swap this one line at the merge.
 */
#define CHRYS_GROW(p, F) F(p)

/** The family's field at world point p: the style's flowers, the distance and what was found there. It writes no global. */
ChrysHit chrysField(vec3 p) {
    return CHRYS_GROW(p, chrysMap);
}

/** The field's gradient at p, unit length: four taps of chrysField's distance. */
vec3 chrysNormal(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * chrysField(p + k.xyy * eps).d + k.yyx * chrysField(p + k.yyx * eps).d
                   + k.yxy * chrysField(p + k.yxy * eps).d + k.xxx * chrysField(p + k.xxx * eps).d);
}
