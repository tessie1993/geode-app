// The shading pass the raymarched families share (Shard, Prism, Fluor, Loom, Petal, Fold and Bulb): soft
// shadows, the step-count glow and the escape colour. Only the rules live here, never a call to a map: a
// family's own shadow loop sits after its field (lib_<family>_march, lib_fold_shade, lib_bulb_shade), since no
// shader may declare a function ahead of its body (style_reads_test), and spends the helpers below.
//
// SOFT SHADOWS. From a lit point a second ray marches toward the key light (or a family's lamp) through the
// same field, and keeps the closest it came to anything as k * d / t: d the field there, t how far along. A
// ray that brushes past an edge keeps a small ratio and lands in the penumbra; one that meets nothing keeps 1.
// The penumbra widens with distance from what casts it, as under a light of size about 1 / k. The darkness of
// the shadow is Soft shadows (uShadow, ParamTable.inc shadow): 0 lights everything as if nothing stood in the
// way and skips the ray, 1 lets the shadow take all of the key's light. Only the key's own light is shadowed:
// a family's emission (paint, threads, seams, edges, fibres) shines in the dark as it did.
//
// COST. The shadow ray runs only at Detail's full path: uSteps 80 and over (the line lib_bulb's bulbIters and
// bulbEps draw between lean and full), so the lowest Detail skips it whole, and so does Soft shadows 0.
// On the full path it takes uSteps * 0.16 steps: 16 at
// the default Detail (102), SHADE_SHADOW_MAX (20) at the highest, once per pixel whose ray met a lit surface
// (the first face for a family that traces on through). Its loop is bounded by that count, an int from a
// uniform, not a constant, and calls the family's field from one site, so the compiler keeps one loop and
// never pastes the map once per step (CI's ES 3.00 compile is already near its budget on these families).
//
// STEP GLOW. A ray that skims a surface creeps along it and spends many steps; one that meets a face square
// on, or crosses open air, spends few. shadeStepGlow() turns the share of its budget a ray spent before it
// met something into light, added in the family's own palette, so every silhouette and fold gathers a halo.
// It follows Bloom (uBloom): the same Customize › Look slider, the Glow macro, the auto-director's Energy and
// any route or wave on Bloom drive it, and at Bloom 0 (its default) it adds nothing. It reads no sound: the
// music reaches it only through Bloom.
//
// ESCAPE COLOUR. A fractal fold's escape (which iteration the orbit left at, smoothed) or an orbit trap (how
// near it came to a point, a plane or a line) is a number per point; shadeEscape() makes it a move along the
// family's own palette, as far as Escape colour (uEscapeColor, ParamTable.inc escapeColor) says: 0 none, 1 a
// whole sweep of the palette from the lowest value to the highest. The colour is always the palette's: no hue
// of its own, no narrowing, and the user's Hue range, stops and colour maps shape it as they shape the rest.
// It reads no sound either.
//
// INCLUDE ORDER: after lib_scene_uniforms (uSteps, uBloom, uShadow, uEscapeColor). lib_strata, lib_loom,
// lib_petal, lib_fold and lib_bulb include it at their top, once per shader.

#define SHADE_SHADOW_MAX 20
/** The penumbra's sharpness k: the key light seen as a disc about 1 / k across. */
#define SHADE_SOFT 8.0
/** How much of the palette a whole escape range sweeps at Escape colour 1. */
#define SHADE_ESCAPE_SPAN 1.0

/** Whether a shadow ray is traced at all: Soft shadows above 0, and Detail's full path (see COST). */
bool shadeShadowOn() {
    return uShadow > 0.0 && uSteps >= 80.0;
}

/**
 * The shadow ray's step count, the bound of its loop: an int from uSteps (Detail), so
 * the driver cannot unroll the loop and paste the field once per step. 0 where shadeShadowOn() says no.
 */
int shadeShadowCount() {
    return shadeShadowOn() ? clamp(int(uSteps * 0.16), 1, SHADE_SHADOW_MAX) : 0;
}

/** One step of the shadow ray: res the light let through so far, h the field t along the ray. */
float shadePenumbra(float res, float h, float t) {
    return min(res, SHADE_SOFT * h / max(t, 1.0e-4));
}

/** How far the shadow ray moves on from a field value h: never a stall, never a leap past a thin part. */
float shadeShadowStep(float h, float reach) {
    return clamp(h, 0.01 + 0.004 * reach, 0.08 * reach + 0.05);
}

/**
 * The key's light at a point whose shadow ray kept res, at the family's darkest `deep` (1 for an opaque body,
 * less for one that lets light through): 1 out of shadow, down to 1 - deep * uShadow in its core.
 */
float shadeLight(float res, float deep) {
    float r = clamp(res, 0.0, 1.0);
    return 1.0 - clamp(uShadow, 0.0, 1.0) * deep * (1.0 - r * r * (3.0 - 2.0 * r));
}

/**
 * The step glow of a ray that spent `used` of its `budget` steps before it met something (or gave up): 0 for
 * a quick ray, up to uBloom for one that crept all the way. Squared, so only the skimming rays glow.
 */
float shadeStepGlow(float used, float budget) {
    float f = clamp(used / max(budget, 1.0), 0.0, 1.0);
    return clamp(uBloom, 0.0, 1.0) * f * f;
}

/** A palette move from an escape or trap value e (0..1, clamped): Escape colour's share of SHADE_ESCAPE_SPAN. */
float shadeEscape(float e) {
    return clamp(uEscapeColor, 0.0, 1.0) * SHADE_ESCAPE_SPAN * clamp(e, 0.0, 1.0);
}

/** An orbit trap's least squared distance to its point as a value 0..1: 0 on the point, toward 1 far off. */
float shadeTrapPoint(float d2) {
    float s = sqrt(max(d2, 0.0));
    return s / (1.0 + s);
}

/** Two orbit traps' distances as one value 0..1: 0 on the first trap's set, 1 on the second's, scale free. */
float shadeTrapPair(vec2 traps) {
    vec2 v = max(traps, vec2(0.0));
    return v.x / (v.x + v.y + 1.0e-5);
}
