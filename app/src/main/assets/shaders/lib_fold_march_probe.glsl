// The Fold family: the probe march, which calls the style's foldMap, defined between
// lib_fold and this file. For a fractal with no distance estimate at all (a
// square that is not conformal, so the running derivative is no bound): the
// field only says inside or outside. The ray strides, short by the eye and
// longer with distance, until the field changes sign, then halves in on the
// crossing five times. A part thinner than a stride can be stepped over; the
// strides are sized so the styles' fractals have none at the scale it shows.
//
// INCLUDE ORDER: after lib_fold and after the style's foldMap.

/** The field's gradient at p, unit length; straight back along the ray where the field is flat. */
vec3 foldProbeNormal(vec3 p, vec3 rd, float eps) {
    vec2 e = vec2(1.0, -1.0) * eps;
    vec3 g = e.xyy * FOLD_AT((p + e.xyy)).x + e.yyx * FOLD_AT((p + e.yyx)).x +
        e.yxy * FOLD_AT((p + e.yxy)).x + e.xxx * FOLD_AT((p + e.xxx)).x;
    float l = length(g);
    return l > 1.0e-9 ? g / l : -rd;
}

/** The light along one ray: strides to the first change of sign, the crossing halved in on, then the surface lit. */
vec3 foldProbe(vec3 ro, vec3 rd) {
    float n = FOLD_PROBE_STEPS;
    float pace = gFoldFar / n;
    float t = pace * (0.1 + 0.4 * foldDither());
    float tPrev = t;
    vec4 m = vec4(1.0, 0.0, 1.0, 1.0);
    bool hit = false;
    float near = 1.0e3;
    for (int i = 0; i < FOLD_PROBE_MAX; i++) {
        if (float(i) >= n || t > gFoldFar) break;
        m = FOLD_AT((ro + rd * t));
        if (m.x < 0.0) {
            hit = true;
            break;
        }
        near = min(near, m.x / (0.4 + 0.25 * t));
        tPrev = t;
        // Short strides by the eye, longer far off: n of them reach about gFoldFar.
        t += pace * (0.6 + 1.2 * t / gFoldFar);
    }
    vec3 col = vec3(0.0);
    if (hit) {
        float a = tPrev;
        float b = t;
        for (int j = 0; j < 5; j++) {
            float mid = 0.5 * (a + b);
            vec4 mm = FOLD_AT((ro + rd * mid));
            if (mm.x < 0.0) {
                b = mid;
                m = mm;
            } else {
                a = mid;
            }
        }
        t = b;
        vec3 p = ro + rd * t;
        vec3 nor = foldProbeNormal(p, rd, 0.002 * (1.0 + t));
        float occ = clamp(FOLD_AT((p + nor * 0.1)).x / 0.1, 0.0, 1.0);
        // Nothing lights right at the eye: flying into the crust must not fill the frame.
        col = foldLit(nor, rd, t, m, occ) * smoothstep(0.02, 0.3, t);
    }
    return foldAir(col, min(t, gFoldFar), near);
}
