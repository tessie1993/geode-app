// The Petal family: the part of lib_petal that calls the style's petalMap, which the style
// defines between the two includes. The field, a sheet's shading and the trace through the sheets.
//
// Why a second file: no style in the app declares a function ahead of its
// body (style_reads_test: noStyleDeclaresAFunctionAhead), so the trace that
// calls petalMap comes after the style has defined it, as in Flame, Opal and
// Hollows.
//
// THE SHADING PASS (lib_shade). The first sheet a ray meets is shadowed softly from its lamp: a shadow ray
// from the sheet's lamp side toward the lamp (petalShadow), so the sheets between cast their shade on it,
// front lit or lit through, by Soft shadows (uShadow), at most PETAL_SHADOW_DEEP since a petal lets light
// through; the rims and veins keep their own glow. The steps a ray spent before its first sheet glow as a halo
// in the palette by Bloom (shadeStepGlow). The escape colour is lib_layers' (the haze's and far layer's folds).
//
// INCLUDE ORDER: after lib_petal and after the style's petalMap.

/** How far a shadow ray looks toward the lamp for a sheet, world units at most. */
#define PETAL_SHADOW_REACH 3.0
/** The deepest a sheet's shadow goes at Soft shadows 1: a petal lets the rest through. */
#define PETAL_SHADOW_DEEP 0.7

vec3 petalField(vec3 p) {
    float k = growOpen(p, 0.2);
    // A domain warp over the breeze: always a little, more with Warp (and k2 FLOW) and Turbulence (lib_layers).
    float amt = 0.04 + 0.12 * gPetalFlow + 0.06 * gPetalChaos;
    vec3 q = growOpened(layerWarp(petalBend(p), amt), k);
    vec3 m = petalMap(q);
    // Grow and layers (lib_grow), each off at its default: Spawn slides a second bloom off the first along the
    // way, so the petals double; Fractal grows petals a third the size out of every sheet; the layers open
    // the bloom round the eye (front) and down the way (back), k at 1 while they rest.
    if (uSpawn > 0.0) {
        vec3 c = petalMap(q + growSpawnShift(vec3(0.0, 0.0, 1.0), 1.0));
        c.x = growClearEye(abs(c.x), p, 0.6);
        if (c.x < abs(m.x)) m = c;
    }
    float reach = growFractalReach(0.2);
    if (reach > 0.0) {
        vec3 c = petalMap(growChildSpace(q, 3.0, 2.0));
        // Sheets are crossed by their unsigned distance: the child's, kept within the shell, if it is nearer.
        float d = growClearEye(growFractalChild(abs(c.x) / 3.0, abs(m.x), reach), p, 0.6);
        if (d < abs(m.x)) m = vec3(d, c.y, c.z);
    }
    // The warp's slack keeps a step safe in the stretched space (lib_layers); k the layers' opening.
    m.x *= k * layerWarpSlack(amt);
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

/**
 * The lamp's light at p on a sheet of normal n (lib_shade), l the unit way to the lamp and dl how far it is:
 * a shadow ray from just off the sheet's lamp side toward the lamp, kept as its closest pass over the distance
 * gone, never past the lamp. 1 without a ray at Soft shadows 0.
 */
float petalShadow(vec3 p, vec3 n, vec3 l, float dl) {
    if (!shadeShadowOn()) return 1.0;
    float steps = shadeShadowSteps();
    float reach = min(dl, PETAL_SHADOW_REACH);
    vec3 o = p + (dot(n, l) >= 0.0 ? n : -n) * (2.0 * gPetalThick + 0.01);
    float res = 1.0;
    float t = 0.02;
    for (int i = 0; i < SHADE_SHADOW_MAX; i++) {
        if (float(i) >= steps || t > reach) break;
        float h = abs(petalField(o + l * t).x) - gPetalThick;
        res = shadePenumbra(res, h, t);
        if (res < 0.01) break;
        t += shadeShadowStep(h * gPetalStride, PETAL_SHADOW_REACH);
    }
    return shadeLight(res, PETAL_SHADOW_DEEP);
}

/**
 * One sheet, seen along rd at p; m its (distance, rim, vein). ndv is how square on it is seen, 1 face on.
 * `first`: the ray's first sheet, which is shadowed from the lamp.
 */
vec3 petalSheet(vec3 p, vec3 rd, vec3 m, float eps, bool first, out float ndv) {
    vec3 n = petalNormal(p, rd, eps);
    // The side of the sheet that faces the eye.
    if (dot(n, rd) > 0.0) n = -n;
    ndv = clamp(-dot(n, rd), 0.0, 1.0);
    vec3 l = petalLampAt(p) - p;
    float dl = length(l);
    l = dl > 1.0e-4 ? l / dl : -rd;
    float key = first ? petalShadow(p, n, l, dl) : 1.0;
    float near = 1.0 / (1.0 + 0.2 * dl * dl);
    float rim = clamp(m.y, 0.0, 1.0);
    float hue = gPetalTone + 0.5 * rim + 0.1 * (1.0 - ndv);
    // The lamp on the eye's side lights the face; behind the sheet, its light comes through,
    // most where the sheet is seen square on and the lamp shines toward the eye.
    float front = clamp(dot(n, l), 0.0, 1.0);
    float back = clamp(-dot(n, l), 0.0, 1.0);
    float ahead = clamp(dot(rd, l), 0.0, 1.0);
    vec3 col = petalCol(hue) * (0.05 + 0.45 * front * near * key);
    col += petalCol(hue + 0.12) * 1.4 * near * back * (0.3 + 0.5 * ndv + ahead * ahead) * key;
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
    col += mix(vec3(1.0), petalCol(hue + 0.2), 0.5) * 0.3 * sheen * front * near * key;
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
    // The steps spent before the first sheet (the step glow's measure).
    float used = 0.0;
    for (int i = 0; i < PETAL_MAX_STEPS; i++) {
        if (float(i) >= n || t > gPetalFar) break;
        if (hits == 0) used = float(i);
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
            acc += petalSheet(p, rd, m, eps, hits == 0, ndv) * through * air * smoothstep(0.1, 0.7, t);
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
    // The step glow (lib_shade): a halo where the ray crept past the sheets before its first, by Bloom.
    col += petalCol(gPetalTone + 0.4) * 0.4 * shadeStepGlow(used, n);
    // The midground haze in front of the first sheet, the far layer through whatever light the sheets let by (lib_layers).
    return layerCompose(col, ro, rd, first, gPetalFar, through);
}
