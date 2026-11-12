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
 * GROW AND LAYERS (lib_grow) for a medium, a density rather than a distance:
 * the layers open the gas about the subject at the origin (or about the axis a
 * flown eye rides) by up to 18%; Spawn adds a second gas slid off the first (1
 * along the way, or aside of a subject), Spawn times its density; Fractal adds
 * the gas again a third the size, repeated every 2, Fractal times its density,
 * only inside the first's gas, so the finer copies grow in the clouds and not
 * in the clear air. The rest of the hit is the first copy's. No turn and no
 * colour; while all four rest NEB_GROW reads the map alone, bit for bit.
 */
NebHit nebGrow(vec3 p, float oct) {
    growFrame();
    float k = growOpen(p, 0.18);
    vec3 c = growCentre();
    vec3 q = c + growOpened(p - c, k);
    NebHit h = nebMap(q, oct);
    float first = h.dens;
    if (uSpawn > 0.0) {
        h.dens += clamp(uSpawn, 0.0, 1.0) * nebMap(q - growSpawnShift(vec3(0.0, 0.0, 1.0), 1.0), oct).dens;
    }
    if (uFractal > 0.0) {
        h.dens += clamp(uFractal, 0.0, 1.0) * nebMap(growChildSpace(q, 3.0, 2.0), oct).dens * clamp(4.0 * first, 0.0, 1.0);
    }
    return h;
}

/**
 * THE FIELD: the style's medium at p with an octave budget `oct`, through
 * NEB_GROW (lib_grow's Spawn, Fractal and layers, nebGrow above; the map itself
 * while they rest). .dens is the density the march integrates;
 * the rest is what the style reads there. Writes no global.
 */
NebHit nebField(vec3 p, float oct) {
    return NEB_GROW(p, oct, nebMap);
}
