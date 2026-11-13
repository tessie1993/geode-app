// The Shard family: the part of lib_shard that calls the style's shardMap, which the style
// defines between the two includes. The field, a face's shading and the trace through the faces.
//
// Why a second file: no shader in the app declares a function ahead (a prototype) and
// calls it before its body (style_reads_test: noStyleDeclaresAFunctionAhead), so the
// march comes after the style's map, as lib_opal_march does.
//
// Around the pieces it draws lib_strata's two other layers: the warped haze and the far fractal.
//
// THE SHADING PASS (lib_shade). The first face a ray meets is shadowed softly from the key light: a shadow
// ray through the pieces (shardShadow), its darkness Soft shadows (uShadow), at most SHARD_SHADOW_DEEP since
// glass lets light through; the edge lines are light of their own and stay lit. The steps a ray spent before
// its first face glow as a halo in the palette by Bloom (shadeStepGlow). The far fractal's escape colour is
// lib_strata's.
//
// INCLUDE ORDER: after lib_strata, lib_shard and the style's shardMap.

/** The key light the faces mirror (shardFace's highlight) and are shadowed from. */
#define SHARD_KEY normalize(vec3(0.45, 0.8, -0.35))
/** How far a shadow ray looks for a piece, world units. */
#define SHARD_SHADOW_REACH 2.5
/** The deepest a glass shadow goes at Soft shadows 1: glass lets the rest through. */
#define SHARD_SHADOW_DEEP 0.6

vec3 shardField(vec3 p) {
    float k = growOpen(p, 0.18);
    vec3 b = shardBend(p);
    // A second, finer warp on the Warp's bend (lib_strata's gStrataWarp: Warp, k2 FLOW and the
    // mids): the pieces drift and flex in place. Along the way a whole number of cycles per 16.
    b += 0.05 * gStrataWarp * sin(b.yzx * vec3(1.7, 1.5708, 1.7) + vec3(0.0, 2.0, 4.0) + uTime * SHARD_RATE(110));
    vec3 q = growOpened(b, k);
    // Grow and layers (lib_grow), each off at its default: Spawn slides a second hoard of glass off the first
    // along the way, Fractal splinters glass a third the size off every face, and the layers open the
    // glass round the eye (front) and down the way (back), k at 1 while they rest.
    // One map call, in a loop the compiler cannot unroll (it starts at a uniform): the base world, then
    // Spawn's copy and Fractal's child while each is on, then Life's three births (lib_grow) while each
    // lives. Three inlined calls put the families past the GPU's compile budget (CI lost its WebGL
    // context on them).
    float reach = growFractalReach(0.15);
    vec3 m = vec3(0.0);
    for (int i = min(int(uSteps), 0); i < 6; i++) {
        if ((i == 1 && !(uSpawn > 0.0)) || (i == 2 && !(reach > 0.0)) || (i > 2 && !growBorn(i - 3))) continue;
        vec3 c = shardMap(i == 0 ? q : (i == 1 ? q + growSpawnShift(vec3(0.0, 0.0, 1.0), 1.5) : (i == 2 ? growChildSpace(q, 3.0, 2.0) : growBirthAt(i - 3, p))));
        if (i == 0) {
            m = c;
            continue;
        }
        if (i > 2) c = vec3(growBirthDist(c.x), c.y, c.z * gGrowBirthS);
        else if (i == 1) c.x = growClearEye(c.x, p, 0.8);
        else c = vec3(growClearEye(growFractalChild(c.x / 3.0, m.x, reach), p, 0.8), c.y, c.z / 3.0);
        if (c.x < m.x) m = c;
    }
    m.xz *= k;
    return m;
}

/** The field's gradient at p, unit length; straight back along the ray where the field is flat. */
vec3 shardNormal(vec3 p, vec3 rd, float eps) {
    vec2 e = vec2(1.0, -1.0) * eps;
    // The four taps in one loop the compiler cannot unroll (it starts at a uniform), so the field is
    // compiled once here, not four times: the inlined copies were past the GPU's compile budget.
    vec3 g = vec3(0.0);
    for (int i = min(int(uSteps), 0); i < 4; i++) {
        vec3 s = e.x * (2.0 * vec3(float(((i + 3) >> 1) & 1), float((i >> 1) & 1), float(i & 1)) - 1.0);
        g += s * shardField(p + s).x;
    }
    float l = length(g);
    return l > 1.0e-9 ? g / l : -rd;
}

/**
 * The key's light at p on a face of normal n (lib_shade): a shadow ray from just off the face toward
 * SHARD_KEY through the pieces, kept as its closest pass over the distance gone. 1 where nothing stands in the
 * way, and without a ray at Soft shadows 0.
 */
float shardShadow(vec3 p, vec3 n) {
    if (!shadeShadowOn()) return 1.0;
    // A loop bound from a uniform (lib_shade): the driver keeps one loop and one field call, never unrolled.
    int count = shadeShadowCount();
    vec3 o = p + n * 0.01;
    float res = 1.0;
    float t = 0.03;
    for (int i = 0; i < count; i++) {
        if (t > SHARD_SHADOW_REACH) break;
        float h = abs(shardField(o + SHARD_KEY * t).x);
        res = shadePenumbra(res, h, t);
        if (res < 0.01) break;
        t += shadeShadowStep(h * gShardStride, SHARD_SHADOW_REACH);
    }
    return shadeLight(res, SHARD_SHADOW_DEEP);
}

/** One face, seen along rd at p, t from the eye; m is shardMap there. `first`: the ray's first face, which is shadowed. */
vec3 shardFace(vec3 p, vec3 rd, vec3 m, float t, float eps, bool first) {
    vec3 n = shardNormal(p, rd, eps);
    // The side of the face that looks at the eye.
    if (dot(n, rd) > 0.0) n = -n;
    float key = first ? shardShadow(p, n) : 1.0;
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
    float spec = pow(clamp(dot(r, SHARD_KEY), 0.0, 1.0), 40.0);
    col += mix(vec3(1.0), shardCol(film + 0.2), 0.4) * 1.4 * spec;
    // The face's own light, under the key's soft shadow; the split edges below are not shadowed.
    col *= key;
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
    // The steps spent before the first face (the step glow's measure).
    float used = 0.0;
    for (int i = 0; i < SHARD_MAX_STEPS; i++) {
        if (float(i) >= n || t > gShardFar) break;
        if (hits == 0) used = float(i);
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
            acc += shardFace(p, rd, m, t, eps, hits == 0) * through * air * near;
            through *= 0.55;
            hits++;
            if (hits >= SHARD_HITS) break;
            t += max(6.0 * eps, gShardSkip);
        } else {
            t += max(d * gShardStride, 0.25 * eps);
        }
    }
    vec3 col = acc + shardCol(gShardTone + 0.5 + 0.7 * glowId / max(glow, 1.0e-6)) * glow;
    // The step glow (lib_shade): a halo where the ray crept past the glass before its first face, by Bloom.
    col += shardCol(gShardTone + 0.4) * 0.4 * shadeStepGlow(used, n);
    // The middle layer (lib_strata): the warped haze between the eye and the reach, through the glass.
    float dz = shardDither();
    vec2 haze = strataHaze(ro, rd, 0.3, gShardFar, dz);
    col += shardCol(gShardTone + 0.3 + haze.y) * haze.x * gStrataHazeGlow;
    // The far layer, beyond the pieces, dimmed by every face the ray passed through.
    if (through > 0.05) {
        vec3 far = strataFar(ro, rd, 0.6 * gShardFar, 1.8 * gShardFar, dz);
        col += shardCol(gShardTone + 0.65 + far.y) * far.x * gStrataFarGlow * through;
    }
    return col;
}
