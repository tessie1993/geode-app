// The Fold family: the glow march, which calls the style's foldMap, defined between
// lib_fold and this file. No surface is ever found: the ray gathers the light of
// the orbit traps' threads (foldMap's zw, distances to a trap LINE, so each lit
// set is a web of curves through space) wherever the fractal is near, and a dim
// haze inside it that also dims what lies behind.
//
// A thread far finer than the step would be hit or missed by chance and
// sparkle. So each sample widens a thread to at least half its step and dims
// it by as much: the light it adds along the ray is the same, a near thread is
// sharp, a far one a soft line. The step is the distance to the fractal, held
// between a third and two and a half times the plain step, so clear air is
// crossed fast and the lit volume slowly.
//
// INCLUDE ORDER: after lib_fold and after the style's foldMap.

/** The light gathered along one ray. */
vec3 foldGlow(vec3 ro, vec3 rd) {
    layerSetup(gFoldPal0, gFoldPal1, gFoldPal2, gFoldPal3, gFoldPal4, gFoldTone, gFoldFlow, gFoldSwell);
    float n = FOLD_GLOW_STEPS;
    // The step that reaches gFoldFar in n steps.
    float pace = gFoldFar / n;
    vec3 acc = vec3(0.0);
    float through = 1.0;
    float t = pace * (0.2 + 0.5 * foldDither());
    for (int i = 0; i < FOLD_GLOW_MAX; i++) {
        if (float(i) >= n || t > gFoldFar || through < 0.03) break;
        vec4 m = foldMap(foldBend(ro + rd * t));
        float span = clamp(m.x * gFoldStride * gFoldSlack, 0.35 * pace, 2.5 * pace);
        float w = foldWidth(t);
        float wide = max(w, 0.5 * span);
        // The thread profile over its own width: about pi of light per thread crossed, whatever the width.
        vec2 prof = wide / (m.zw * m.zw + wide * wide);
        float near = 1.0 - smoothstep(0.0, gFoldEdge, m.x);
        // Nothing lights right at the eye: flying through a thread must not fill the frame.
        float fade = smoothstep(0.05, 0.8, t) * exp(-2.4 * t / gFoldFar) * near * span;
        float hue = gFoldTone + m.y + 0.2 * t / gFoldFar;
        vec3 e = foldCol(hue) * prof.x + foldCol(hue + 0.35) * 0.5 * prof.y;
        acc += through * fade * (0.07 * gFoldBuild * e + 0.06 * foldCol(hue + 0.6));
        through *= exp(-gFoldDense * near * span);
        t += span;
    }
    // The haze in front, the far layer through what the threads' haze lets by (lib_layers).
    return layerCompose(acc, ro, rd, gFoldFar, gFoldFar, through);
}
