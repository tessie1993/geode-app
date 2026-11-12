// The Fold family: the cone march, which calls the style's foldMap, defined between
// lib_fold and this file. The fractal's surface, sphere traced through folded
// space. A pixel sees a cone, not a line: the hit distance is the cone's width
// where the ray is (gFoldCone per unit from the eye), so near detail is traced
// to the pixel and far detail, finer than a pixel, ends the ray early instead
// of spending the budget creeping up on a crust nobody can see.
//
// INCLUDE ORDER: after lib_fold and after the style's foldMap.

//#include lib_fold_at

/** The surface's normal at p, unit length; straight back along the ray where the field is flat. */
vec3 foldConeNormal(vec3 p, vec3 rd, float eps) {
    vec2 e = vec2(1.0, -1.0) * eps;
    // The four taps in one loop the compiler cannot unroll (it starts at a uniform), so the field is
    // compiled once here, not four times: the inlined copies were past the GPU's compile budget.
    vec3 g = vec3(0.0);
    for (int i = min(int(uSteps), 0); i < 4; i++) {
        vec3 s = e.x * (2.0 * vec3(float(((i + 3) >> 1) & 1), float((i >> 1) & 1), float(i & 1)) - 1.0);
        g += s * FOLD_AT((p + s)).x;
    }
    float l = length(g);
    return l > 1.0e-9 ? g / l : -rd;
}

/** The light along one ray: traced to the surface within the pixel's cone, then lit. */
vec3 foldCone(vec3 ro, vec3 rd) {
    layerSetup(gFoldPal0, gFoldPal1, gFoldPal2, gFoldPal3, gFoldPal4, gFoldTone, gFoldFlow, gFoldSwell);
    float n = FOLD_CONE_STEPS;
    float t = 0.02 + 0.05 * foldDither();
    vec4 m = vec4(1.0, 0.0, 1.0, 1.0);
    bool hit = false;
    float near = 1.0e3;
    for (int i = 0; i < FOLD_CONE_MAX; i++) {
        if (float(i) >= n || t > gFoldFar) break;
        m = FOLD_AT((ro + rd * t));
        float eps = gFoldCone * t + 2.0e-4;
        near = min(near, m.x / (0.4 + 0.25 * t));
        if (m.x < eps) {
            hit = true;
            break;
        }
        t += max(m.x * gFoldStride * gFoldSlack, 0.5 * eps);
    }
    t = min(t, gFoldFar);
    vec3 col = vec3(0.0);
    if (hit) {
        vec3 p = ro + rd * t;
        vec3 nor = foldConeNormal(p, rd, 1.5 * (gFoldCone * t + 2.0e-4));
        float occ = clamp(FOLD_AT((p + nor * 0.1)).x / 0.1, 0.0, 1.0);
        // Nothing lights right at the eye: flying into the crust must not fill the frame.
        col = foldLit(nor, rd, t, m, occ) * smoothstep(0.02, 0.3, t);
    }
    // The haze in front of the surface, the far layer where nothing was hit (lib_layers).
    return layerCompose(foldAir(col, t, near), ro, rd, t, gFoldFar, hit ? 0.0 : 1.0);
}
