// The Fluor family: the part of lib_fluor that calls the style's fluorMap, which the style
// defines between the two includes. The field, the relief's shape at a hit and the trace.
//
// Why a second file: no style in the app declares a function ahead of its body
// (style_reads_test: noStyleDeclaresAFunctionAhead), as lib_hollow_march says.
//
// Round the relief it draws lib_strata's two other layers: the warped haze before it and the far
// fractal at the open end of the way.
//
// THE SHADING PASS (lib_shade). Where a ray meets the relief, a shadow ray toward the key light the gloss
// comes from (fluorShadow) lays the ridges' soft shadows over the stone by Soft shadows (uShadow); the paint
// on the edges glows by itself and stays lit. The steps a ray spent before it met the stone, or gave up, glow
// as a halo in the palette by Bloom (shadeStepGlow). The far fractal's escape colour is lib_strata's.
//
// INCLUDE ORDER: after lib_strata, lib_fluor and the style's fluorMap.

/** The key light the stone's gloss comes from, and its shadows. */
#define FLUOR_KEY normalize(vec3(0.3, 0.7, 0.6))
/** How far a shadow ray looks for the relief, world units. */
#define FLUOR_SHADOW_REACH 2.0
/** The deepest the stone's shadow goes at Soft shadows 1: the eye's own faint light still reaches in. */
#define FLUOR_SHADOW_DEEP 0.85

vec2 fluorField(vec3 p) {
    float k = growOpen(p, 0.15);
    vec3 q = fluorBend(p);
    // A second, finer warp on the Warp's bend (lib_strata's gStrataWarp: Warp, k2 FLOW and the
    // mids): the carving sways in place. Along the way a whole number of cycles per 16.
    q += 0.03 * gStrataWarp * sin(q.yzx * vec3(1.7, 1.5708, 1.7) + vec3(0.0, 2.0, 4.0) + uTime * FLUOR_RATE(110));
    q = growOpened(q, k);
    // Grow and layers (lib_grow), each off at its default: Spawn slides a second carving off the first
    // along the way (half a tile at full), so every boss, spiral and panel doubles; Fractal carves the relief
    // again a third the size on every raised face; the layers open the carving round the eye (front) and
    // down the way (back), k at 1 while they rest.
    // One map call, in a loop the compiler cannot unroll (it starts at a uniform): the base world, then
    // Spawn's copy and Fractal's child while each is on. Three inlined calls put the families past the
    // GPU's compile budget (CI lost its WebGL context on them).
    float reach = growFractalReach(0.2);
    vec2 m = vec2(0.0);
    for (int i = min(int(uSteps), 0); i < 3; i++) {
        if ((i == 1 && !(uSpawn > 0.0)) || (i == 2 && !(reach > 0.0))) continue;
        vec2 c = fluorMap(i == 0 ? q : (i == 1 ? q + growSpawnShift(vec3(0.0, 0.0, 1.0), 0.5) : growChildSpace(q, 3.0, 2.0)));
        if (i == 0) {
            m = c;
            continue;
        }
        c.x = i == 1 ? growClearEye(c.x, p, 0.4) : growClearEye(growFractalChild(c.x / 3.0, m.x, reach), p, 0.4);
        if (c.x < m.x) m = c;
    }
    m.x *= k;
    vec3 s = sin(q * 6.0 + uTime * FLUOR_RATE(1300) + vec3(0.0, 2.1, 4.2));
    m.x += gFluorChaos * 0.025 * (s.x * s.y + s.z);
    return m;
}

/**
 * The relief at a hit p, where the field is f0, from four taps e away: its
 * normal (unit length; straight back along the ray where the field is flat),
 * and how sharply it bends, e times the field's Laplacian: over 0 on a raised
 * edge, under 0 in a crease, 0 on a flat face, about 1 on an edge sharper than e.
 */
float fluorShape(vec3 p, vec3 rd, float f0, float e, out vec3 nor) {
    vec2 k = vec2(1.0, -1.0) * e;
    // The four taps in one loop the compiler cannot unroll (it starts at a uniform), so the field is
    // compiled once here, not four times: the inlined copies were past the GPU's compile budget.
    vec3 g = vec3(0.0);
    float sum = 0.0;
    for (int i = min(int(uSteps), 0); i < 4; i++) {
        vec3 s = k.x * (2.0 * vec3(float(((i + 3) >> 1) & 1), float((i >> 1) & 1), float(i & 1)) - 1.0);
        float f = fluorField(p + s).x;
        g += s * f;
        sum += f;
    }
    float l = length(g);
    nor = l > 1.0e-9 ? g / l : -rd;
    // The four taps sit at the corners of a tetrahedron, so their sum less four
    // times the middle is 2 e^2 times the Laplacian.
    return clamp((sum - 4.0 * f0) / (2.0 * e), -2.0, 2.0);
}

/**
 * The key's light at p on the relief of normal nor (lib_shade): a shadow ray from just off the stone toward
 * FLUOR_KEY, kept as its closest pass over the distance gone. 1 without a ray at Soft shadows 0.
 */
float fluorShadow(vec3 p, vec3 nor) {
    if (!shadeShadowOn()) return 1.0;
    // A loop bound from a uniform (lib_shade): the driver keeps one loop and one field call, never unrolled.
    int count = shadeShadowCount();
    vec3 o = p + nor * 0.01;
    float res = 1.0;
    float t = 0.02;
    for (int i = 0; i < count; i++) {
        if (t > FLUOR_SHADOW_REACH) break;
        float h = fluorField(o + FLUOR_KEY * t).x;
        res = shadePenumbra(res, h, t);
        if (res < 0.01) break;
        t += shadeShadowStep(h * gFluorStride, FLUOR_SHADOW_REACH);
    }
    return shadeLight(res, FLUOR_SHADOW_DEEP);
}

/** The light along one ray: sphere traced to the relief, then the stone lit from the eye and its edges painted. */
vec3 fluorTrace(vec3 ro, vec3 rd) {
    float n = FLUOR_STEPS;
    float t = 0.02 + 0.05 * fluorDither();
    vec2 m = vec2(1.0, 0.0);
    bool hit = false;
    float near = 1.0e3;
    // The steps spent before the stone (the step glow's measure).
    float used = 0.0;
    for (int i = 0; i < FLUOR_MAX_STEPS; i++) {
        if (float(i) >= n || t > gFluorFar) break;
        used = float(i);
        m = fluorField(ro + rd * t);
        float eps = 0.0015 * (1.0 + t);
        near = min(near, m.x / (0.4 + 0.25 * t));
        if (m.x < eps) {
            hit = true;
            break;
        }
        t += max(m.x * gFluorStride, 0.25 * eps);
    }
    t = min(t, gFluorFar);
    float depth = t / gFluorFar;
    float hue = gFluorTone + 0.35 * depth;
    vec3 col = vec3(0.0);
    if (hit) {
        vec3 p = ro + rd * t;
        vec3 nor;
        float bend = fluorShape(p, rd, m.x, gFluorLine * (1.0 + 0.5 * t), nor);
        float ndv = clamp(dot(nor, -rd), 0.0, 1.0);
        float occ = clamp(fluorField(p + nor * 0.15).x / 0.15, 0.0, 1.0);
        float lamp = ndv / (1.0 + 0.12 * t * t);
        float paint = smoothstep(0.12, 0.6, bend);
        float crease = smoothstep(0.1, 0.6, -bend);
        float key = fluorShadow(p, nor);
        // The stone: dark, lit faintly from the eye, darker in its hollows and at the foot of every ridge, and
        // under the key's soft shadows.
        col = fluorCol(hue) * (0.02 + 0.35 * lamp) * (0.2 + 0.8 * occ) * (1.0 - 0.8 * crease) * key;
        // The paint on every raised edge, glowing by itself, so not lit by the lamp.
        col += fluorCol(hue + 0.3 + m.y) * paint * gFluorGlow * 1.6 * (0.5 + 0.5 * occ);
        float rim = 1.0 - ndv;
        col += fluorCol(hue + 0.5 + m.y) * rim * rim * rim * 0.12 * occ * key;
        float gloss = clamp(dot(reflect(rd, nor), FLUOR_KEY), 0.0, 1.0);
        col += fluorCol(hue + 0.6) * pow(gloss, 32.0) * 0.25 * key;
    }
    float air = exp(-3.0 * depth);
    // Light skims the relief the ray passes close to, and the far end of the way is never quite dark.
    col = col * air + fluorCol(hue + 0.5) * (0.025 * (1.0 - air) + 0.02 * exp(-6.0 * max(near, 0.0)));
    // The step glow (lib_shade): a halo where the ray crept along the relief, by Bloom.
    col += fluorCol(hue + 0.4) * 0.4 * shadeStepGlow(used, n);
    // The middle layer (lib_strata): the warped haze between the eye and the relief.
    float dz = fluorDither();
    vec2 haze = strataHaze(ro, rd, 0.3, hit ? t : gFluorFar, dz);
    col += fluorCol(gFluorTone + 0.3 + haze.y) * haze.x * gStrataHazeGlow;
    // The far layer, where the ray met no relief: the open end of the way.
    if (!hit) {
        vec3 far = strataFar(ro, rd, 0.6 * gFluorFar, 1.8 * gFluorFar, dz);
        col += fluorCol(gFluorTone + 0.65 + far.y) * far.x * gStrataFarGlow;
    }
    return col;
}
