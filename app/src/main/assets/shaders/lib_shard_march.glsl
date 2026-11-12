// The Shard family: the part of lib_shard that calls the style's shardMap, which the style
// defines between the two includes. The field, a face's shading and the trace through the faces.
//
// Why a second file: no shader in the app declares a function ahead (a prototype) and
// calls it before its body (style_reads_test: noStyleDeclaresAFunctionAhead), so the
// march comes after the style's map, as lib_opal_march does.
//
// INCLUDE ORDER: after lib_shard and after the style's shardMap.

vec3 shardField(vec3 p) {
    float k = growOpen(p, 0.18);
    vec3 q = growOpened(shardBend(p), k);
    vec3 m = shardMap(q);
    // Grow and layers (lib_grow), each off at its default: Spawn slides a second hoard of glass off the first
    // along the way, Fractal splinters glass a third the size off every face, and the layers open the
    // glass round the eye (front) and down the way (back), k at 1 while they rest.
    if (uSpawn > 0.0) {
        vec3 c = shardMap(q + growSpawnShift(vec3(0.0, 0.0, 1.0), 1.5));
        c.x = growClearEye(c.x, p, 0.8);
        if (c.x < m.x) m = c;
    }
    float reach = growFractalReach(0.15);
    if (reach > 0.0) {
        vec3 c = shardMap(growChildSpace(q, 3.0, 2.0));
        c = vec3(growClearEye(growFractalChild(c.x / 3.0, m.x, reach), p, 0.8), c.y, c.z / 3.0);
        if (c.x < m.x) m = c;
    }
    m.xz *= k;
    return m;
}

/** The field's gradient at p, unit length; straight back along the ray where the field is flat. */
vec3 shardNormal(vec3 p, vec3 rd, float eps) {
    vec2 e = vec2(1.0, -1.0) * eps;
    vec3 g = e.xyy * shardField(p + e.xyy).x + e.yyx * shardField(p + e.yyx).x +
        e.yxy * shardField(p + e.yxy).x + e.xxx * shardField(p + e.xxx).x;
    float l = length(g);
    return l > 1.0e-9 ? g / l : -rd;
}

/** One face, seen along rd at p, t from the eye; m is shardMap there. */
vec3 shardFace(vec3 p, vec3 rd, vec3 m, float t, float eps) {
    vec3 n = shardNormal(p, rd, eps);
    // The side of the face that looks at the eye.
    if (dot(n, rd) > 0.0) n = -n;
    // Turbulence shivers the face, not its outline: under 0.6 off a unit normal, so never zero.
    vec3 s = sin(p * 9.0 + uTime * SHARD_RATE(1600) + vec3(0.0, 2.1, 4.2));
    n = normalize(n + gShardChaos * 0.3 * s);
    float ndv = clamp(-dot(n, rd), 0.0, 1.0);
    float rim = 1.0 - ndv;
    vec3 r = reflect(rd, n);
    // The film: its depth wanders across the face, and the light crosses it more steeply
    // inside the glass (index 1.5), so the colour shifts as the piece turns.
    float inside = sqrt(1.0 - 0.444 * (1.0 - ndv * ndv));
    float depth = 0.5 + 0.5 * sin(dot(p, vec3(2.1, 1.3, 1.7)) + SHARD_TAU * m.y);
    float film = gShardTone + 0.7 * m.y + gShardBands * depth * inside;
    float rim2 = rim * rim;
    float fres = 0.04 + 0.96 * rim2 * rim2 * rim;
    float sky = 0.5 + 0.5 * r.y;
    vec3 col = shardCol(film) * (0.04 + (0.3 + 0.7 * sky) * (0.12 + 1.5 * fres));
    float spec = pow(clamp(dot(r, normalize(vec3(0.45, 0.8, -0.35))), 0.0, 1.0), 40.0);
    col += mix(vec3(1.0), shardCol(film + 0.2), 0.4) * 1.4 * spec;
    // The edge splits the light: three fine lines a little apart, each its own place in the palette.
    float w = gShardEdge * (1.0 + 0.4 * t);
    float w2 = w * w;
    float e0 = m.z;
    float e1 = m.z - 2.5 * w;
    float e2 = m.z - 5.0 * w;
    vec3 split = shardCol(film + 0.15) * w2 / (e0 * e0 + w2) + shardCol(film + 0.48) * w2 / (e1 * e1 + w2) +
        shardCol(film + 0.81) * w2 / (e2 * e2 + w2);
    col += split * (0.5 + rim);
    return col;
}

/** The light along one ray: sphere traced to each face in turn, and on through it. */
vec3 shardTrace(vec3 ro, vec3 rd) {
    float n = SHARD_STEPS;
    vec3 acc = vec3(0.0);
    float through = 1.0;
    float glow = 0.0;
    float glowId = 0.0;
    float t = 0.05 + 0.1 * shardDither();
    int hits = 0;
    for (int i = 0; i < SHARD_MAX_STEPS; i++) {
        if (float(i) >= n || t > gShardFar) break;
        vec3 p = ro + rd * t;
        vec3 m = shardField(p);
        float d = abs(m.x);
        float eps = 0.0025 * (1.0 + t);
        float air = exp(-2.2 * t / gShardFar);
        // Nothing lights right at the eye: flying through a piece must not fill the frame.
        float near = smoothstep(0.1, 0.8, t);
        // A ray that passes close to an edge catches a thread of its light.
        float g = air * near * through * 0.008 / (1.0 + 2500.0 * m.z * m.z);
        glow += g;
        glowId += g * m.y;
        if (d < eps) {
            acc += shardFace(p, rd, m, t, eps) * through * air * near;
            through *= 0.55;
            hits++;
            if (hits >= SHARD_HITS) break;
            t += max(6.0 * eps, gShardSkip);
        } else {
            t += max(d * gShardStride, 0.25 * eps);
        }
    }
    return acc + shardCol(gShardTone + 0.5 + 0.7 * glowId / max(glow, 1.0e-6)) * glow;
}
