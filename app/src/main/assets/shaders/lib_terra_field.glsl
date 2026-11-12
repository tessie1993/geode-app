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
 * GROW AND LAYERS (lib_grow) for a land, a height rather than a distance: the
 * layers open the ground round the eye where it stands on it (round the
 * subject at the origin for a style that turns about one) by up to 18%, so the
 * near hills stand back or close in and the far ones with the back layer;
 * Spawn raises a second range slid off the first (2 along the way, or aside of
 * a subject), the higher of the two, faded in; Fractal grows the land again a
 * third the size on its own slopes, at most 0.3 high at full. No turn and no
 * colour; while all four rest TERRA_GROW reads the map alone, bit for bit.
 */
float terraGrow(vec2 xz) {
    growFrame();
    vec2 c = gGrowAround ? vec2(0.0) : gGrowEye.xz;
    float k = growOpen(vec3(xz.x, gGrowEye.y, xz.y), 0.18);
    vec2 q = c + (xz - c) / k;
    float h = terraMap(q);
    if (uSpawn > 0.0) {
        vec3 shift = growSpawnShift(vec3(0.0, 0.0, 1.0), 2.0);
        h = mix(h, max(h, terraMap(q - shift.xz)), clamp(uSpawn, 0.0, 1.0));
    }
    float reach = growFractalReach(0.3);
    if (reach > 0.0) {
        // A third the size and a third the height: the same slopes, so the march's step holds.
        h += min(max(terraMap(3.0 * q) / 3.0, 0.0), reach);
    }
    return h;
}

/**
 * THE FIELD: the land's height at world xz. The style's map, through
 * TERRA_GROW (lib_grow's Spawn, Fractal and layers, terraGrow above; the map
 * itself while they rest), times the relief (lib_terra's shell: 1 at
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
