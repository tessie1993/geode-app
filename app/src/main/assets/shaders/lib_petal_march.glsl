// The Petal family: the part of lib_petal that calls the style's petalMap, which the style
// defines between the two includes. The field, a sheet's shading and the trace through the sheets.
//
// Why a second file: no style in the app declares a function ahead of its
// body (style_reads_test: noStyleDeclaresAFunctionAhead), so the trace that
// calls petalMap comes after the style has defined it, as in Flame, Opal and
// Hollows.
//
// INCLUDE ORDER: after lib_petal and after the style's petalMap.

vec3 petalField(vec3 p) {
    // A domain warp over the breeze: always a little, more with Warp (and k2 FLOW) and Turbulence (lib_layers).
    float amt = 0.04 + 0.12 * gPetalFlow + 0.06 * gPetalChaos;
    vec3 q = layerWarp(petalBend(p), amt);
    vec3 m = petalMap(q);
    m.x *= layerWarpSlack(amt);
    vec3 s = sin(q * 4.5 + uTime * PETAL_RATE(1600) + vec3(0.0, 2.1, 4.2));
    m.x += gPetalChaos * 0.04 * (s.x * s.y + s.z);
    return m;
}

/** The field's gradient at p, unit length; straight back along the ray where the field is flat. */
vec3 petalNormal(vec3 p, vec3 rd, float eps) {
    vec2 e = vec2(1.0, -1.0) * eps;
    vec3 g = e.xyy * petalField(p + e.xyy).x + e.yyx * petalField(p + e.yyx).x +
        e.yxy * petalField(p + e.yxy).x + e.xxx * petalField(p + e.xxx).x;
    float l = length(g);
    return l > 1.0e-9 ? g / l : -rd;
}

/** One sheet, seen along rd at p; m its (distance, rim, vein). ndv is how square on it is seen, 1 face on. */
vec3 petalSheet(vec3 p, vec3 rd, vec3 m, float eps, out float ndv) {
    vec3 n = petalNormal(p, rd, eps);
    // The side of the sheet that faces the eye.
    if (dot(n, rd) > 0.0) n = -n;
    ndv = clamp(-dot(n, rd), 0.0, 1.0);
    vec3 l = petalLampAt(p) - p;
    float dl = length(l);
    l = dl > 1.0e-4 ? l / dl : -rd;
    float near = 1.0 / (1.0 + 0.2 * dl * dl);
    float rim = clamp(m.y, 0.0, 1.0);
    float hue = gPetalTone + 0.5 * rim + 0.1 * (1.0 - ndv);
    // The lamp on the eye's side lights the face; behind the sheet, its light comes through,
    // most where the sheet is seen square on and the lamp shines toward the eye.
    float front = clamp(dot(n, l), 0.0, 1.0);
    float back = clamp(-dot(n, l), 0.0, 1.0);
    float ahead = clamp(dot(rd, l), 0.0, 1.0);
    vec3 col = petalCol(hue) * (0.05 + 0.45 * front * near);
    col += petalCol(hue + 0.12) * 1.4 * near * back * (0.3 + 0.5 * ndv + ahead * ahead);
    // Light gathers at the rims: along the silhouette, where the sheet is seen edge on, and at its own edge.
    float edge = 1.0 - ndv;
    edge = edge * edge * edge;
    float lip = smoothstep(0.75, 1.0, rim);
    col += petalCol(hue + 0.3) * (0.9 * edge + 0.7 * lip) * (0.3 + 0.7 * near);
    // The veins show against the light; far ones widen and dim rather than flicker.
    float v = fract(m.z) - 0.5;
    float vw = 0.003 + 0.1 * eps;
    col += petalCol(hue + 0.2) * gPetalVeins * 0.003 / (v * v + vw) * (0.2 + 0.8 * back) * (0.15 + 0.85 * near);
    float sheen = pow(clamp(dot(reflect(rd, n), l), 0.0, 1.0), 16.0);
    col += mix(vec3(1.0), petalCol(hue + 0.2), 0.5) * 0.3 * sheen * front * near;
    return col;
}

/** The light along one ray: sphere traced to each sheet in turn and on through it, with the lamp's glow between. */
vec3 petalTrace(vec3 ro, vec3 rd) {
    layerSetup(gPetalPal0, gPetalPal1, gPetalPal2, gPetalPal3, gPetalPal4, gPetalTone, gPetalFlow, gPetalSwell);
    float n = PETAL_STEPS;
    // Where the ray met its first sheet: the haze lies in front of it.
    float first = gPetalFar;
    vec3 acc = vec3(0.0);
    float through = 1.0;
    float halo = 0.0;
    float glow = 0.0;
    float t = 0.05 + 0.1 * petalDither();
    // Where along the ray the lamp's glow has been added up to.
    float lit = t;
    int hits = 0;
    for (int i = 0; i < PETAL_MAX_STEPS; i++) {
        if (float(i) >= n || t > gPetalFar) break;
        vec3 p = ro + rd * t;
        vec3 m = petalField(p);
        float d = abs(m.x) - gPetalThick;
        float eps = 0.002 * (1.0 + t);
        float air = exp(-2.0 * t / gPetalFar);
        halo += through * air * 0.01 / (1.0 + 80.0 * d * d);
        if (d < eps) {
            if (hits == 0) first = t;
            // The lamp's light up to this sheet, at the share let through so far.
            glow += through * petalGlow(ro, rd, lit, t);
            float ndv;
            // Nothing lights right at the eye: flying through a sheet must not fill the frame.
            acc += petalSheet(p, rd, m, eps, ndv) * through * air * smoothstep(0.1, 0.7, t);
            // A sheet seen edge on is a longer way through, so it lets less by.
            through *= gPetalPass * (0.45 + 0.55 * ndv);
            hits++;
            // On through the sheet's thickness, on the slant it is crossed at.
            t += 2.0 * (gPetalThick + eps) / max(ndv, 0.3) + eps;
            lit = t;
            if (hits >= PETAL_HITS) break;
        } else {
            t += max(d * gPetalStride, 0.25 * eps);
        }
    }
    glow += through * petalGlow(ro, rd, lit, max(lit, gPetalFar));
    vec3 col = acc + petalCol(gPetalTone + 0.4) * halo + petalCol(gPetalTone + 0.05) * gPetalShine * glow;
    // The midground haze in front of the first sheet, the far layer through whatever light the sheets let by (lib_layers).
    return layerCompose(col, ro, rd, first, gPetalFar, through);
}
