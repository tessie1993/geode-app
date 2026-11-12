// The Fluor family: the part of lib_fluor that calls the style's fluorMap, which the style
// defines between the two includes. The field, the relief's shape at a hit and the trace.
//
// Why a second file: no style in the app declares a function ahead of its body
// (style_reads_test: noStyleDeclaresAFunctionAhead), as lib_hollow_march says.
//
// Round the relief it draws lib_strata's two other layers: the warped haze before it and the far
// fractal at the open end of the way.
//
// INCLUDE ORDER: after lib_strata, lib_fluor and the style's fluorMap.

vec2 fluorField(vec3 p) {
    vec3 q = fluorBend(p);
    // A second, finer warp on the Warp's bend (lib_strata's gStrataWarp: Warp, k2 FLOW and the
    // mids): the carving sways in place. Along the way a whole number of cycles per 16.
    q += 0.03 * gStrataWarp * sin(q.yzx * vec3(1.7, 1.5708, 1.7) + vec3(0.0, 2.0, 4.0) + uTime * FLUOR_RATE(110));
    vec2 m = fluorMap(q);
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
    float a = fluorField(p + k.xyy).x;
    float b = fluorField(p + k.yyx).x;
    float c = fluorField(p + k.yxy).x;
    float d = fluorField(p + k.xxx).x;
    vec3 g = k.xyy * a + k.yyx * b + k.yxy * c + k.xxx * d;
    float l = length(g);
    nor = l > 1.0e-9 ? g / l : -rd;
    // The four taps sit at the corners of a tetrahedron, so their sum less four
    // times the middle is 2 e^2 times the Laplacian.
    return clamp((a + b + c + d - 4.0 * f0) / (2.0 * e), -2.0, 2.0);
}

/** The light along one ray: sphere traced to the relief, then the stone lit from the eye and its edges painted. */
vec3 fluorTrace(vec3 ro, vec3 rd) {
    float n = FLUOR_STEPS;
    float t = 0.02 + 0.05 * fluorDither();
    vec2 m = vec2(1.0, 0.0);
    bool hit = false;
    float near = 1.0e3;
    for (int i = 0; i < FLUOR_MAX_STEPS; i++) {
        if (float(i) >= n || t > gFluorFar) break;
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
        // The stone: dark, lit faintly from the eye, darker in its hollows and at the foot of every ridge.
        col = fluorCol(hue) * (0.02 + 0.35 * lamp) * (0.2 + 0.8 * occ) * (1.0 - 0.8 * crease);
        // The paint on every raised edge, glowing by itself, so not lit by the lamp.
        col += fluorCol(hue + 0.3 + m.y) * paint * gFluorGlow * 1.6 * (0.5 + 0.5 * occ);
        float rim = 1.0 - ndv;
        col += fluorCol(hue + 0.5 + m.y) * rim * rim * rim * 0.12 * occ;
        float gloss = clamp(dot(reflect(rd, nor), normalize(vec3(0.3, 0.7, 0.6))), 0.0, 1.0);
        col += fluorCol(hue + 0.6) * pow(gloss, 32.0) * 0.25;
    }
    float air = exp(-3.0 * depth);
    // Light skims the relief the ray passes close to, and the far end of the way is never quite dark.
    col = col * air + fluorCol(hue + 0.5) * (0.025 * (1.0 - air) + 0.02 * exp(-6.0 * max(near, 0.0)));
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
