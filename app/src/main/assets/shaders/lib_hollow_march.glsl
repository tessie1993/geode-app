// The Hollows family: the part of lib_hollow that calls the style's hollowMap, which the style
// defines between the two includes. The field and the trace to the walls.
//
// Why a second file: lib_hollow used to declare hollowMap ahead (a prototype) and
// call it before the style defined it. No other style in the app declares a
// function ahead, and only these three families froze and crashed on the
// owner's phone, so every style now defines a function before any call
// (style_reads_test: noStyleDeclaresAFunctionAhead).
//
// INCLUDE ORDER: after lib_hollow and after the style's hollowMap.

vec2 hollowField(vec3 p) {
    vec3 q = hollowBend(p);
    vec2 m = hollowMap(q);
    vec3 s = sin(q * 4.0 + uTime * HOLLOW_RATE(1500) + vec3(0.0, 2.1, 4.2));
    m.x += gHollowChaos * 0.05 * (s.x * s.y + s.z);
    m.x += gHollowGroove * hollowCut(m.y);
    return m;
}

/** The light along one ray. */
vec3 hollowTrace(vec3 ro, vec3 rd) {
    float n = min(uSteps, float(HOLLOW_MAX_STEPS));
    float t = 0.02 + 0.05 * hollowDither();
    vec2 m = vec2(1.0, 0.0);
    bool hit = false;
    float near = 1.0e3;
    for (int i = 0; i < HOLLOW_MAX_STEPS; i++) {
        if (float(i) >= n || t > gHollowFar) break;
        m = hollowField(ro + rd * t);
        near = min(near, m.x / (0.4 + 0.25 * t));
        if (m.x < 0.0015 * (1.0 + t)) {
            hit = true;
            break;
        }
        t += m.x * gHollowStride;
    }
    t = min(t, gHollowFar);
    float depth = t / gHollowFar;
    float hue = gHollowTone + 0.4 * depth;
    vec3 col = vec3(0.0);
    if (hit) {
        vec3 p = ro + rd * t;
        vec2 e = vec2(1.0, -1.0) * 0.003 * (1.0 + t);
        vec3 nor = normalize(
            e.xyy * hollowField(p + e.xyy).x + e.yyx * hollowField(p + e.yyx).x +
            e.yxy * hollowField(p + e.yxy).x + e.xxx * hollowField(p + e.xxx).x
        );
        float ndv = max(dot(nor, -rd), 0.0);
        float rim = pow(1.0 - ndv, 3.0);
        float occ = clamp(hollowField(p + nor * 0.25).x / 0.25, 0.0, 1.0);
        float lamp = ndv / (1.0 + 0.1 * t * t);
        col = hollowCol(hue) * (0.03 + 0.55 * lamp) * (0.25 + 0.75 * occ);
        col += hollowCol(hue + 0.45) * rim * (0.15 + 0.75 * occ);
        float groove = hollowCut(m.y);
        col += hollowCol(hue + 0.25 + 0.381966 * floor(m.y)) * groove * gHollowGlow * 1.5 * (0.5 + 0.5 * occ);
        col += hollowCol(hue + 0.6) * pow(max(dot(reflect(rd, nor), normalize(vec3(0.3, 0.7, 0.6))), 0.0), 24.0) * 0.5;
    }
    float air = exp(-3.2 * depth);
    // Light skims every wall the ray passes close to, and the far end of the way is never quite dark.
    col = col * air + hollowCol(hue + 0.5) * (0.03 * (1.0 - air) + 0.02 * exp(-6.0 * max(near, 0.0)));
    return col;
}
