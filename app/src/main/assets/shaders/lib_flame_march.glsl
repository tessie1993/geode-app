// The Fractal Flame family: the part of lib_flame that calls the style's flameField, which the style
// defines between the two includes. The march gathers the veils along one ray.
//
// Why a second file: lib_flame used to declare flameField ahead (a prototype) and
// call it before the style defined it. No other style in the app declares a
// function ahead, and only these three families froze and crashed on the
// owner's phone, so every style now defines a function before any call
// (style_reads_test: noStyleDeclaresAFunctionAhead).
//
// INCLUDE ORDER: after lib_flame and after the style's flameField.

/** The gathered light along one ray. */
vec3 flameMarch(vec3 ro, vec3 rd) {
    float n = min(uSteps, float(FLAME_MAX_STEPS));
    float w = gFlameWidth;
    vec3 acc = vec3(0.0);
    float tPrev = 0.0;
    vec2 fPrev = vec2(0.0);
    vec2 aPrev = vec2(0.0);
    float jitter = flameDither();
    for (int i = 0; i < FLAME_MAX_STEPS; i++) {
        if (float(i) >= n) break;
        float x = (float(i) + jitter) / n;
        float t = gFlameFar * x * sqrt(x);
        vec2 f;
        vec2 hue;
        vec2 wt;
        flameField(flameBend(ro + rd * t), f, hue, wt);
        vec2 a = flameAnti(f, w);
        if (i > 0) {
            // Between two samples the field is taken as a straight line, so the light of every veil
            // crossed on the way is exact: a coarse step loses no veil and none aliases.
            // An angle's wrap is a jump by whole veils, not veils crossed.
            vec2 jump = round(f - fPrev) * step(1.5, abs(f - fPrev));
            fPrev += jump;
            aPrev += jump;
            vec2 df = abs(f - fPrev);
            vec2 e = mix(abs(a - aPrev) / max(df, 1.0e-4), flameLine(f, w), step(df, vec2(2.0e-3)));
            // A veil seen along its surface is the bright one: the silhouettes are the filaments.
            vec2 rate = df / ((t - tPrev) * gFlameGraze);
            e *= mix(vec2(1.0), 2.5 / (1.0 + rate * rate), gFlameEdge);
            e = mix(e, e * flameNear(f.yx) * 4.0, gFlameThread) + gFlameHaze;
            // Nothing lights right at the eye: flying through a veil must not fill the frame.
            float fade = smoothstep(0.05, 1.0, t) * exp(-2.6 * t / gFlameFar) * (t - tPrev);
            vec2 id = fract((mod(floor(0.5 * (f + fPrev)), gFlamePeriod) + 1.0) * 0.381966);
            // Every veil has its own brightness, so the layers read apart.
            vec2 amp = smoothstep(gFlameGate, gFlameGate + 0.3, wt) * (0.12 + 0.88 * id * id) * e * fade;
            acc += flameCol(hue.x + id.x + gFlameTone) * amp.x + flameCol(hue.y + id.y + gFlameTone) * amp.y;
        }
        tPrev = t;
        fPrev = f;
        aPrev = a;
    }
    return acc;
}
