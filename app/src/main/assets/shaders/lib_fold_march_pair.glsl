// The Fold family: the pair march, which calls the style's foldMap, defined between
// lib_fold and this file. For a fractal whose two fold rules take turns: each
// rule stretches space its own way, so the running scale is right only on
// average and the distance it gives is a local bound, sometimes too generous.
// So no step is longer than gFoldCap, and a step that lands inside (the
// distance negative past the hit width) is taken back: the ray returns to the
// last point it knew was outside and goes on in half the steps. The cap grows
// again in open air. Where the cap falls under the hit width, the surface is
// there.
//
// INCLUDE ORDER: after lib_fold and after the style's foldMap.

/** The surface's normal at p, unit length; straight back along the ray where the field is flat. */
vec3 foldPairNormal(vec3 p, vec3 rd, float eps) {
    vec2 e = vec2(1.0, -1.0) * eps;
    vec3 g = e.xyy * FOLD_AT((p + e.xyy)).x + e.yyx * FOLD_AT((p + e.yyx)).x +
        e.yxy * FOLD_AT((p + e.yxy)).x + e.xxx * FOLD_AT((p + e.xxx)).x;
    float l = length(g);
    return l > 1.0e-9 ? g / l : -rd;
}

/** The light along one ray: capped steps, a step back from inside, then the surface lit. */
vec3 foldPair(vec3 ro, vec3 rd) {
    layerSetup(gFoldPal0, gFoldPal1, gFoldPal2, gFoldPal3, gFoldPal4, gFoldTone, gFoldFlow, gFoldSwell);
    float n = FOLD_PAIR_STEPS;
    float t = 0.02 + 0.05 * foldDither();
    // The last point known to be outside, and the longest step allowed from here.
    float back = t;
    float cap = gFoldCap;
    vec4 m = vec4(1.0, 0.0, 1.0, 1.0);
    bool hit = false;
    float near = 1.0e3;
    for (int i = 0; i < FOLD_PAIR_MAX; i++) {
        if (float(i) >= n || t > gFoldFar) break;
        m = FOLD_AT((ro + rd * t));
        float eps = gFoldCone * t + 2.0e-4;
        if (m.x < -eps) {
            t = back;
            cap *= 0.5;
            if (cap < eps) {
                hit = true;
                break;
            }
            t += cap;
            continue;
        }
        near = min(near, m.x / (0.4 + 0.25 * t));
        if (m.x < eps) {
            hit = true;
            break;
        }
        back = t;
        t += clamp(m.x * gFoldStride * gFoldSlack, 0.5 * eps, cap);
        cap = min(cap * 1.3, gFoldCap);
    }
    t = min(t, gFoldFar);
    vec3 col = vec3(0.0);
    if (hit) {
        vec3 p = ro + rd * t;
        vec3 nor = foldPairNormal(p, rd, 1.5 * (gFoldCone * t + 2.0e-4));
        float occ = clamp(FOLD_AT((p + nor * 0.1)).x / 0.1, 0.0, 1.0);
        // Nothing lights right at the eye: flying into the crust must not fill the frame.
        col = foldLit(nor, rd, t, m, occ) * smoothstep(0.02, 0.3, t);
    }
    // The haze in front of the surface, the far layer where nothing was hit (lib_layers).
    return layerCompose(foldAir(col, t, near), ro, rd, t, gFoldFar, hit ? 0.0 : 1.0);
}
