// The Fold family's soft shadow (lib_shade): the part of the shading pass that reads the style's foldMap,
// so it comes after the map, in the three surface marches (lib_fold_march_cone, _pair and _probe include it
// at their top). No shader may declare a function ahead of its body (style_reads_test), so it cannot sit in
// lib_fold, before the map; the glow and slice marches have no surface to shadow and do not include it.
//
// From a hit p a shadow ray goes toward FOLD_KEY (lib_fold), the key foldLit lights by, through the same
// field the march read (FOLD_AT: bent, grown and opened), and keeps its closest pass over the distance gone.
// Its darkness is Soft shadows (uShadow); its budget lib_shade's, so Detail reaches it.
// On a probe style the field is no distance, only about a unit per unit near the surface (lib_fold), so the
// ray's steps are held short (shadeShadowStep) and inside reads as shadow.
//
// INCLUDE ORDER: after lib_fold and after the style's foldMap, at the top of a surface march.

/** How far a shadow ray looks for the fractal, world units. */
#define FOLD_SHADOW_REACH 2.5

/** The key's light at p on a surface of normal nor, hit within eps (lib_shade); 1 without a ray at Soft shadows 0. */
float foldShadow(vec3 p, vec3 nor, float eps) {
    if (!shadeShadowOn()) return 1.0;
    // A loop bound from a uniform (lib_shade): the driver keeps one loop and one field call, never unrolled.
    int count = shadeShadowCount();
    vec3 o = p + nor * max(0.01, 3.0 * eps);
    float res = 1.0;
    float t = 0.02;
    for (int i = 0; i < count; i++) {
        if (t > FOLD_SHADOW_REACH) break;
        float h = FOLD_AT((o + FOLD_KEY * t)).x;
        res = shadePenumbra(res, h, t);
        if (res < 0.01) break;
        t += shadeShadowStep(h * gFoldStride * gFoldSlack, FOLD_SHADOW_REACH);
    }
    return shadeLight(res, 1.0);
}
