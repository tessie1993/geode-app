// The Bulb family's one field: the part of lib_bulb that calls the style's
// bulbMap, which the style defines between the two includes, and the normal
// that differentiates it.
//
// Why a second file: no style in the app declares a function ahead of its
// body (style_reads_test: noStyleDeclaresAFunctionAhead), so the field that
// calls bulbMap comes after the style has defined it, as in Prism, Petal and
// the other families with a march include.
//
// Every Bulb style marches and differentiates through bulbField and
// bulbNormal, never its map directly, so whatever wraps the field wraps the
// whole style once.
//
// INCLUDE ORDER: after lib_bulb and after the style's BulbHit bulbMap(vec3 p).

/**
 * GROW AND FORMULA HOOK. Where lib_grow (Spawn, Fractal, Front and Back layer)
 * and uFormula will wrap the style's map F at p once they land: today the map
 * itself. Swap this one line at the merge.
 */
#define BULB_GROW(p, F) F(p)

/** The family's field at world point p: the style's body, its distance, orbit trap and part. It writes no global. */
BulbHit bulbField(vec3 p) {
    return BULB_GROW(p, bulbMap);
}

/** The field's gradient at p, unit length: four taps of bulbField's distance. */
vec3 bulbNormal(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * bulbField(p + k.xyy * e).d + k.yyx * bulbField(p + k.yyx * e).d
                   + k.yxy * bulbField(p + k.yxy * e).d + k.xxx * bulbField(p + k.xxx * e).d);
}
