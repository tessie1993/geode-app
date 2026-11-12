// The Prism family: the part of lib_prism that calls the style's prismMap, which the style
// defines between the two includes. The field, the mirror's shading and the trace with its echo.
//
// Why a second file: a library that calls the style's map before the style
// defines it needs a prototype, and no style in the app declares a function
// ahead: every function is defined before any call (style_reads_test:
// noStyleDeclaresAFunctionAhead).
//
// INCLUDE ORDER: after lib_prism and after the style's prismMap.

vec2 prismField(vec3 p) {
    vec3 q = prismBend(p);
    vec2 m = prismMap(q);
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
 * The mirror at one hit p: `seen` is what lies along its reflection, `away` the ribbon distance
 * there and `w` the ribbon's half-width. A pulse runs along the ribbons, whole cycles per period.
 */
vec3 prismSkin(vec3 p, vec3 rd, vec3 nor, vec3 seen, float away, float hue, float w) {
    float ndv = clamp(dot(nor, -rd), 0.0, 1.0);
    float rim = 1.0 - ndv;
    float film = hue + gPrismSheen * rim * rim;
    vec3 col = seen * mix(vec3(1.0), prismCol(film), 0.6) * (0.45 + 0.55 * rim);
    col += prismCol(film + 0.45) * 0.5 * rim * rim * rim;
    float spec = pow(clamp(dot(reflect(rd, nor), normalize(vec3(0.4, 0.8, -0.4))), 0.0, 1.0), 80.0);
    col += mix(vec3(1.0), prismCol(film + 0.2), 0.3) * spec;
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
    for (int i = 0; i < PRISM_MAX_STEPS; i++) {
        if (float(i) >= n || t > gPrismFar) break;
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
            seen = prismSkin(p2, r, nor2, sky2, k.y, hue + 0.2, 0.03) * exp(-0.4 * u);
        }
        // Nothing lights right at the eye: flying into the chrome must not fill the frame.
        col = prismSkin(p, rd, nor, seen, m.y, hue, 0.02 + 0.004 * t) * exp(-2.6 * depth) * smoothstep(0.05, 0.5, t);
    }
    return col + prismCol(hue + 0.6) * glow;
}
