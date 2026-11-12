// The Fold family: the slice march, which calls the style's foldMap, defined between
// lib_fold and this file. The fractal is never marched at all. The ray is met
// with a stack of thin cuts fixed in the world, planes across the way
// (foldSlicePlanes) or spheres nested about the origin (foldSliceShells), each
// meeting found exactly, and at each the style's four-dimensional fractal is
// read once: the cut shows its section there, filled where the point is
// inside, outlined where the edge passes, ruled by the trap seams. The
// sections are laid over each other front to back, each hiding a share of
// what lies behind it, so the whole stack reads as one translucent body built
// of its own cross-sections. One foldMap per cut, and no normals.
//
// INCLUDE ORDER: after lib_fold and after the style's foldMap.

/** One section at p, seen from t: its light, and in a its opacity. */
vec4 foldSection(vec3 p, float t) {
    vec4 m = foldMap(foldBend(p));
    float w = 2.0 * foldWidth(t);
    float fill = 1.0 - smoothstep(-w, w, m.x);
    float edge = w * w / (m.x * m.x + w * w);
    float hue = gFoldTone + m.y + 0.2 * t / gFoldFar;
    vec3 col = foldCol(hue) * fill * 0.3 + foldCol(hue + 0.45) * edge;
    col += foldCol(hue + 0.25) * foldSeams(m.zw, w) * gFoldBuild * gFoldSeam * fill;
    // Nothing lights right at the eye: flying through a cut must not fill the frame.
    float fade = smoothstep(0.05, 0.7, t) * exp(-2.4 * t / gFoldFar);
    return vec4(col * fade, clamp(gFoldDense * fill + 0.4 * edge, 0.0, 0.9) * fade);
}

/** The light along one ray through planes across z, gFoldGap apart. */
vec3 foldSlicePlanes(vec3 ro, vec3 rd) {
    layerSetup(gFoldPal0, gFoldPal1, gFoldPal2, gFoldPal3, gFoldPal4, gFoldTone, gFoldFlow, gFoldSwell);
    float n = FOLD_SLICE_STEPS;
    float dir = rd.z < 0.0 ? -1.0 : 1.0;
    // A ray almost along the planes meets them far apart; its cuts are spaced as if it met them at a slant.
    float az = max(abs(rd.z), 0.05);
    float z0 = dir > 0.0 ? (floor(ro.z / gFoldGap) + 1.0) * gFoldGap : (ceil(ro.z / gFoldGap) - 1.0) * gFoldGap;
    float t = abs(z0 - ro.z) / az;
    float dt = gFoldGap / az;
    vec3 acc = vec3(0.0);
    float through = 1.0;
    for (int i = 0; i < FOLD_SLICE_MAX; i++) {
        if (float(i) >= n || t > gFoldFar || through < 0.02) break;
        vec4 s = foldSection(ro + rd * t, t);
        acc += through * s.rgb;
        through *= 1.0 - s.a;
        t += dt;
    }
    // The haze in front, the far layer through what the sections let by (lib_layers).
    return layerCompose(acc, ro, rd, gFoldFar, gFoldFar, through);
}

/** The light along one ray through gFoldShells spheres about the origin, the outermost gFoldShellR across: every near side first, outermost in, then every far side, innermost out. */
vec3 foldSliceShells(vec3 ro, vec3 rd) {
    layerSetup(gFoldPal0, gFoldPal1, gFoldPal2, gFoldPal3, gFoldPal4, gFoldTone, gFoldFlow, gFoldSwell);
    float n = min(FOLD_SLICE_STEPS, 2.0 * gFoldShells);
    float gap = gFoldShellR / max(gFoldShells, 1.0);
    float b = dot(ro, rd);
    float c0 = dot(ro, ro);
    vec3 acc = vec3(0.0);
    float through = 1.0;
    for (int i = 0; i < FOLD_SLICE_MAX; i++) {
        if (float(i) >= n || through < 0.02) break;
        float k = float(i);
        bool behind = k >= gFoldShells;
        float j = behind ? 2.0 * gFoldShells - 1.0 - k : k;
        float r = gFoldShellR - j * gap;
        float h = b * b - (c0 - r * r);
        // A shell the ray misses: so are all the smaller ones on this side.
        if (h <= 0.0) continue;
        float t = behind ? -b + sqrt(h) : -b - sqrt(h);
        if (t < 0.02 || t > gFoldFar) continue;
        vec4 s = foldSection(ro + rd * t, t);
        acc += through * s.rgb;
        through *= 1.0 - s.a;
    }
    return layerCompose(acc, ro, rd, gFoldFar, gFoldFar, through);
}
