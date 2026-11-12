// The Terrain family's field: the part of lib_terra that calls the style's
// terraMap, which the style defines between the two includes. The one height
// every march, normal and shading read in the family goes through here.
//
// Why a second file: no shader in the app declares a function ahead (a
// prototype) and calls it before its body (style_reads_test:
// noStyleDeclaresAFunctionAhead), so the field comes after the style's map, as
// lib_shard_march does for the Shard family.
//
// INCLUDE ORDER: after lib_terra and after the style's
// `float terraMap(vec2 xz)`, its height at world xz (unwrapped: the map folds
// it with terraWrap itself, where it tiles).

/**
 * THE FIELD: the land's height at world xz. The style's map, through the merge
 * hook TERRA_GROW (lib_grow's Spawn, Fractal and layers, once that branch
 * merges; the map itself today), times the relief (lib_terra's shell: 1 at
 * rest). Writes no global.
 */
float terraField(vec2 xz) {
    return gTerraRelief * TERRA_GROW(xz, terraMap);
}

/**
 * The terrain normal by central differences on the field at world xz, step e
 * (grow it with distance: 0.002 * t + 0.01 or so).
 */
vec3 terraNormal(vec2 xz, float e) {
    return normalize(vec3(terraField(xz - vec2(e, 0.0)) - terraField(xz + vec2(e, 0.0)), 2.0 * e,
                          terraField(xz - vec2(0.0, e)) - terraField(xz + vec2(0.0, e))));
}
