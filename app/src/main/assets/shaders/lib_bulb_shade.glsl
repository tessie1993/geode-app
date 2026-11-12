// The Bulb family's soft shadow (lib_shade): the part of the shading pass that reads the family's field,
// so it comes after lib_bulb_field, which comes after the style's bulbMap. No shader may declare a function
// ahead of its body (style_reads_test), so it cannot sit in lib_bulb.
//
// From a lit point a shadow ray goes along the unit way l toward the member's key (or its lamp, at most
// `reach` off), through bulbField, and keeps its closest pass over the distance gone. Its darkness is Soft
// shadows (uShadow); its budget lib_shade's, so Detail and the hot-phone cut reach it. It costs nothing at
// Soft shadows 0, and one field per step otherwise, once per lit pixel.
//
// INCLUDE ORDER: after lib_bulb_field. A LIT member includes it (Classic, Ink, Shell).

/** The key's light at p (on a surface of normal n, hit within eps) from the unit way l, `reach` at most. */
float bulbShadow(vec3 p, vec3 n, vec3 l, float reach, float eps) {
    if (!shadeShadowOn()) return 1.0;
    // A loop bound from a uniform (lib_shade): the driver keeps one loop and one field call, never unrolled.
    int count = shadeShadowCount();
    vec3 o = p + n * max(0.004, 3.0 * eps);
    float res = 1.0;
    float t = 0.01;
    for (int i = 0; i < count; i++) {
        if (t > reach) break;
        float h = bulbField(o + l * t).d;
        res = shadePenumbra(res, h, t);
        if (res < 0.01) break;
        t += shadeShadowStep(0.9 * h, reach);
    }
    return shadeLight(res, 1.0);
}
