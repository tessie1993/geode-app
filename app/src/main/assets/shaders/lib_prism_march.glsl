// The Prism family: the part of lib_prism that calls the style's prismMap, which the style
// defines between the two includes. The field, the mirror's shading and the trace with its echo.
//
// Why a second file: a library that calls the style's map before the style
// defines it needs a prototype, and no style in the app declares a function
// ahead: every function is defined before any call (style_reads_test:
// noStyleDeclaresAFunctionAhead).
//
// Round the chrome it draws lib_strata's two other layers: the warped haze before it and the far
// fractal where a ray meets no chrome, which the mirror also sees once along its reflection.
//
// THE SHADING PASS (lib_shade). Where a ray meets the chrome, a shadow ray toward the key light
// (prismShadow) darkens the mirror's own light, its reflection, rim and highlight, by Soft shadows (uShadow);
// the ribbons are light of their own and stay lit. The steps a ray spent before it met the chrome, or gave
// up, glow as a halo in the palette by Bloom (shadeStepGlow). The far fractal's escape colour is lib_strata's.
//
// INCLUDE ORDER: after lib_strata, lib_prism and the style's prismMap.

/** The key light the chrome's highlight comes from, and its shadows. */
#define PRISM_KEY normalize(vec3(0.4, 0.8, -0.4))
/** How far a shadow ray looks for chrome, world units. */
#define PRISM_SHADOW_REACH 3.0
/** The deepest the shadow goes on a mirror at Soft shadows 1: the rest is the world it still reflects. */
#define PRISM_SHADOW_DEEP 0.7

vec2 prismField(vec3 p) {
    float k = growOpen(p, 0.18);
    vec3 q = prismBend(p);
    // A second, finer warp on the Warp's bend (lib_strata's gStrataWarp: Warp, k2 FLOW and the
    // mids): the chrome flows in place. Along the way a whole number of cycles per 16.
    q += 0.04 * gStrataWarp * sin(q.yzx * vec3(1.9, 1.5708, 1.9) + vec3(0.0, 2.0, 4.0) + uTime * PRISM_RATE(110));
    q = growOpened(q, k);
    vec2 m = prismMap(q);
    // Grow and layers (lib_grow), each off at its default: Spawn slides a second chrome world off the first
    // along the way (half a rosette cell at full), Fractal grows chrome a third the size out of every surface,
    // and the layers open the chrome round the eye (front) and down the way (back), k at 1 while they rest.
    if (uSpawn > 0.0) {
        vec2 c = prismMap(q + growSpawnShift(vec3(0.0, 0.0, 1.0), 2.0));
        c.x = growClearEye(c.x, p, 0.6);
        if (c.x < m.x) m = c;
    }
    float reach = growFractalReach(0.2);
    if (reach > 0.0) {
        vec2 c = prismMap(growChildSpace(q, 3.0, 2.0)) / 3.0;
        c.x = growClearEye(growFractalChild(c.x, m.x, reach), p, 0.6);
        // A child's ribbons light only right at the child: its mirror lines run on through the air.
        c.y = max(c.y, 4.0 * c.x);
        if (c.x < m.x) m = c;
    }
    m.x *= k;
    vec3 s = sin(q * 4.0 + uTime * PRISM_RATE(1600) + vec3(0.0, 2.1, 4.2));
    m.x += gPrismChaos * 0.04 * (s.x * s.y + s.z);
    return m;
}

/** The solid's normal at p, unit length; straight back along the ray where the field is flat. */
vec3 prismNormal(vec3 p, vec3 rd, float eps) {
    vec2 e = vec2(1.0, -1.0) * eps;
    vec3 g = e.xyy * prismField(p + e.xyy).x + e.yyx * prismField(p + e.yyx).x +
        e.yxy * prismField(p + e.yxy).x + e.xxx * prismField(p + e.xxx).x;
    float l = length(g);
    return l > 1.0e-9 ? g / l : -rd;
}

/**
 * The key's light at p on chrome of normal nor (lib_shade): a shadow ray from just off the surface toward
 * PRISM_KEY, kept as its closest pass over the distance gone. 1 without a ray at Soft shadows 0.
 */
float prismShadow(vec3 p, vec3 nor) {
    if (!shadeShadowOn()) return 1.0;
    // A loop bound from a uniform (lib_shade): the driver keeps one loop and one field call, never unrolled.
    int count = shadeShadowCount();
    vec3 o = p + nor * 0.01;
    float res = 1.0;
    float t = 0.03;
    for (int i = 0; i < count; i++) {
        if (t > PRISM_SHADOW_REACH) break;
        float h = prismField(o + PRISM_KEY * t).x;
        res = shadePenumbra(res, h, t);
        if (res < 0.01) break;
        t += shadeShadowStep(h * gPrismStride, PRISM_SHADOW_REACH);
    }
    return shadeLight(res, PRISM_SHADOW_DEEP);
}

/**
 * The mirror at one hit p: `seen` is what lies along its reflection, `away` the ribbon distance
 * there and `w` the ribbon's half-width. A pulse runs along the ribbons, whole cycles per period.
 * `key` is the key's light there (prismShadow), on the mirror's own light and not on the ribbons.
 */
vec3 prismSkin(vec3 p, vec3 rd, vec3 nor, vec3 seen, float away, float hue, float w, float key) {
    float ndv = clamp(dot(nor, -rd), 0.0, 1.0);
    float rim = 1.0 - ndv;
    float film = hue + gPrismSheen * rim * rim;
    vec3 col = seen * mix(vec3(1.0), prismCol(film), 0.6) * (0.45 + 0.55 * rim);
    col += prismCol(film + 0.45) * 0.5 * rim * rim * rim;
    float spec = pow(clamp(dot(reflect(rd, nor), PRISM_KEY), 0.0, 1.0), 80.0);
    col += mix(vec3(1.0), prismCol(film + 0.2), 0.3) * spec;
    col *= key;
    float run = 0.55 + 0.45 * sin(6.0 * PRISM_TAU / PRISM_PERIOD * p.z + 2.0 * length(p.xy) - uTime * PRISM_RATE(900));
    col += prismCol(hue + 0.6) * gPrismLines * 1.4 * run * prismLine(away, w);
    return col;
}

/** The light along one ray: sphere traced to the chrome, then once more along its reflection. */
vec3 prismTrace(vec3 ro, vec3 rd) {
    float n = PRISM_STEPS;
    float t = 0.02 + 0.05 * prismDither();
    vec2 m = vec2(1.0, 1.0);
    bool hit = false;
    float glow = 0.0;
    // The steps spent before the chrome (the step glow's measure).
    float used = 0.0;
    for (int i = 0; i < PRISM_MAX_STEPS; i++) {
        if (float(i) >= n || t > gPrismFar) break;
        used = float(i);
        m = prismField(ro + rd * t);
        float eps = 0.0015 * (1.0 + t);
        // The ribbons light the air the ray passes close to them.
        glow += exp(-2.5 * t / gPrismFar) * prismLine(m.y, 0.08) * 0.02 / (1.0 + 40.0 * m.x * m.x);
        if (m.x < eps) {
            hit = true;
            break;
        }
        t += max(m.x * gPrismStride, 0.25 * eps);
    }
    t = min(t, gPrismFar);
    float depth = t / gPrismFar;
    float hue = gPrismTone + 0.35 * depth;
    vec3 col = vec3(0.0);
    if (hit) {
        vec3 p = ro + rd * t;
        vec3 nor = prismNormal(p, rd, 0.003 * (1.0 + t));
        vec3 r = reflect(rd, nor);
        vec3 from = p + nor * 0.02 * (1.0 + t);
        float en = PRISM_ECHO;
        float u = 0.05;
        vec2 k = vec2(1.0, 1.0);
        bool echo = false;
        for (int i = 0; i < PRISM_MAX_ECHO; i++) {
            if (float(i) >= en || u > 6.0) break;
            k = prismField(from + r * u);
            if (k.x < 0.004 * (1.0 + u)) {
                echo = true;
                break;
            }
            u += max(k.x * gPrismStride, 0.002);
        }
        vec3 seen = prismSky(r, hue);
        if (echo) {
            vec3 p2 = from + r * u;
            vec3 nor2 = prismNormal(p2, r, 0.006);
            vec3 sky2 = prismSky(reflect(r, nor2), hue + 0.2);
            seen = prismSkin(p2, r, nor2, sky2, k.y, hue + 0.2, 0.03, 1.0) * exp(-0.4 * u);
        } else {
            // The far layer (lib_strata) seen in the mirror, once along the reflection.
            vec2 glance = strataGlance(from, r, 0.7 * gPrismFar);
            seen += prismCol(hue + 0.6 + glance.y) * glance.x * gStrataFarGlow;
        }
        // Nothing lights right at the eye: flying into the chrome must not fill the frame.
        col = prismSkin(p, rd, nor, seen, m.y, hue, 0.02 + 0.004 * t, prismShadow(p, nor)) * exp(-2.6 * depth) *
            smoothstep(0.05, 0.5, t);
    }
    col += prismCol(hue + 0.6) * glow;
    // The step glow (lib_shade): a halo where the ray crept along the chrome, by Bloom.
    col += prismCol(hue + 0.4) * 0.4 * shadeStepGlow(used, n);
    // The middle layer (lib_strata): the warped haze between the eye and the chrome.
    float dz = prismDither();
    vec2 haze = strataHaze(ro, rd, 0.3, hit ? t : gPrismFar, dz);
    col += prismCol(gPrismTone + 0.3 + haze.y) * haze.x * gStrataHazeGlow;
    // The far layer, where the ray met no chrome.
    if (!hit) {
        vec3 far = strataFar(ro, rd, 0.6 * gPrismFar, 1.8 * gPrismFar, dz);
        col += prismCol(gPrismTone + 0.65 + far.y) * far.x * gStrataFarGlow;
    }
    return col;
}
