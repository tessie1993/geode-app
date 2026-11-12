// The Opal family: the part of lib_opal that calls the style's opalMap, which the style
// defines between the two includes. The field, a skin's shading and the trace through the skins.
//
// Why a second file: lib_opal used to declare opalMap ahead (a prototype) and
// call it before the style defined it. No other style in the app declares a
// function ahead, and only these three families froze and crashed on the
// owner's phone, so every style now defines a function before any call
// (style_reads_test: noStyleDeclaresAFunctionAhead).
//
// INCLUDE ORDER: after lib_opal and after the style's opalMap.

vec2 opalField(vec3 p) {
    vec3 q = opalBend(p);
    vec2 m = opalMap(q);
    vec3 s = sin(q * 5.0 + uTime * OPAL_RATE(1700) + vec3(0.0, 2.1, 4.2));
    m.x += gOpalChaos * 0.035 * (s.x * s.y + s.z);
    return m;
}

/** One skin, seen along rd at p. */
vec3 opalSkin(vec3 p, vec3 rd, float skin, float eps) {
    vec2 e = vec2(1.0, -1.0) * eps;
    vec3 n = normalize(
        e.xyy * opalField(p + e.xyy).x + e.yyx * opalField(p + e.yyx).x +
        e.yxy * opalField(p + e.yxy).x + e.xxx * opalField(p + e.xxx).x
    );
    float facing = dot(n, rd);
    n *= -sign(facing);
    float ndv = abs(facing);
    float rim = 1.0 - ndv;
    vec3 r = reflect(rd, n);
    float film = gOpalTone + gOpalBands * rim * rim + 0.3 * r.y + 0.15 * skin;
    vec3 col = opalCol(film) * (0.1 + 1.3 * rim * rim);
    col += opalCol(film + 0.45) * 1.6 * pow(rim, 6.0);
    float spec = pow(max(dot(r, normalize(vec3(0.45, 0.8, -0.35))), 0.0), 60.0);
    col += mix(vec3(1.0), opalCol(film + 0.2), 0.35) * spec;
    float c = fract(skin) - 0.5;
    col += opalCol(film + 0.3) * gOpalLines * 0.0025 / (c * c + 0.0025) * (0.25 + 0.75 * rim);
    return col;
}

/** The gathered light along one ray. */
vec3 opalTrace(vec3 ro, vec3 rd) {
    float n = min(uSteps, float(OPAL_MAX_STEPS));
    vec3 acc = vec3(0.0);
    float through = 1.0;
    float halo = 0.0;
    float t = 0.05 + 0.1 * opalDither();
    int hits = 0;
    for (int i = 0; i < OPAL_MAX_STEPS; i++) {
        if (float(i) >= n || t > gOpalFar) break;
        vec3 p = ro + rd * t;
        vec2 m = opalField(p);
        float d = abs(m.x);
        float eps = 0.0025 * (1.0 + t);
        float air = exp(-2.2 * t / gOpalFar);
        halo += air * 0.012 / (1.0 + 60.0 * d * d);
        if (d < eps) {
            // Nothing lights right at the eye: flying through a skin must not fill the frame.
            acc += opalSkin(p, rd, m.y, eps) * through * air * smoothstep(0.1, 0.8, t);
            through *= 0.6;
            hits++;
            if (hits >= OPAL_HITS) break;
            t += 6.0 * eps;
        } else {
            t += d * gOpalStride;
        }
    }
    return acc + opalCol(gOpalTone + 0.5) * halo;
}
