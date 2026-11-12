// The Nebula family's field: the part of lib_nebula that calls the style's
// nebMap, which a volume style defines between the two includes. Its march and
// its light taps read the medium through here.
//
// Why a second file: no shader in the app declares a function ahead (a
// prototype) and calls it before its body (style_reads_test:
// noStyleDeclaresAFunctionAhead), so the field comes after the style's map, as
// lib_shard_march does for the Shard family.
//
// INCLUDE ORDER: after lib_nebula, the style's `struct NebHit { float dens;
// ... }` and its `NebHit nebMap(vec3 p, float oct)`.

/**
 * THE FIELD: the style's medium at p with an octave budget `oct`, through the
 * merge hook NEB_GROW (lib_grow's Spawn, Fractal and layers, once that branch
 * merges; the map itself today). .dens is the density the march integrates;
 * the rest is what the style reads there. Writes no global.
 */
NebHit nebField(vec3 p, float oct) {
    return NEB_GROW(p, oct, nebMap);
}
