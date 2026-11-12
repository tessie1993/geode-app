// The Loom family: the part of lib_loom that calls the style's loomMap, which the style
// defines between the two includes. The field, a strand's normal and the trace through the skins to a strand.
//
// Why a second file: no shader may declare a function ahead of its body
// (style_reads_test: noStyleDeclaresAFunctionAhead), so what calls the
// style's map comes after it, as lib_flame_march, lib_opal_march and
// lib_hollow_march do.
//
// INCLUDE ORDER: after lib_loom and after the style's loomMap.

vec4 loomField(vec3 p) {
    float k = growOpen(p, 0.18);
    // A domain warp over the bend: always a little, more with Warp (and k2 FLOW) and Turbulence (lib_layers).
    float amt = 0.05 + 0.14 * gLoomFlow + 0.08 * gLoomChaos;
    vec3 q = growOpened(layerWarp(loomBend(p), amt), k);
    // Grow and layers (lib_grow), each off at its default. Spawn slides a second weave off the first along the
    // way, so the strands and skins double; Fractal spins thinner strands a third the size round every strand.
    // The layers: Front layer opens the weave round the eye, Back layer the weave down the way, and it
    // billows the skins, the loom's own backdrop.
    // One map call, in a loop the compiler cannot unroll (it starts at a uniform): the base world, then
    // Spawn's copy and Fractal's child while each is on. Three inlined calls put the families past the
    // GPU's compile budget (CI lost its WebGL context on them).
    float reach = growFractalReach(0.15);
    vec4 m = vec4(0.0);
    for (int i = min(int(uSteps), 0); i < 3; i++) {
        if ((i == 1 && !(uSpawn > 0.0)) || (i == 2 && !(reach > 0.0))) continue;
        vec4 c = loomMap(i == 0 ? q : (i == 1 ? q + growSpawnShift(vec3(0.0, 0.0, 1.0), 1.0) : growChildSpace(q, 3.0, 2.0)));
        if (i == 0) {
            m = c;
        } else if (i == 1) {
            c.x = growClearEye(c.x, p, 0.5);
            if (c.x < m.x) m = vec4(c.x, c.y, m.z, c.w);
            if (abs(c.z) < abs(m.z)) m.z = c.z;
        } else {
            c.x = growClearEye(growFractalChild(c.x / 3.0, m.x, reach), p, 0.5);
            if (c.x < m.x) m = vec4(c.x, c.y, m.z, c.w);
        }
    }
    // The warp's slack keeps a step safe in the stretched space (lib_layers); k the layers' opening.
    float slack = layerWarpSlack(amt);
    m.x *= k * slack;
    m.z *= slack;
    m.z -= growSwellOf(uBackLayer, p, 0.3);
    return m;
}

/** The strand's normal at p, unit length; straight back along the ray where the field is flat. */
vec3 loomNormal(vec3 p, vec3 rd, float eps) {
    vec2 e = vec2(1.0, -1.0) * eps;
    // The four taps in one loop the compiler cannot unroll (it starts at a uniform), so the field is
    // compiled once here, not four times: the inlined copies were past the GPU's compile budget.
    vec3 g = vec3(0.0);
    for (int i = min(int(uSteps), 0); i < 4; i++) {
        vec3 s = e.x * (2.0 * vec3(float(((i + 3) >> 1) & 1), float((i >> 1) & 1), float(i & 1)) - 1.0);
        g += s * loomField(p + s).x;
    }
    float l = length(g);
    return l > 1.0e-9 ? g / l : -rd;
}

/** The light along one ray: sphere traced through the skins to a strand, then the strand lit. */
vec3 loomTrace(vec3 ro, vec3 rd) {
    layerSetup(gLoomPal0, gLoomPal1, gLoomPal2, gLoomPal3, gLoomPal4, gLoomTone, gLoomFlow, gLoomSwell);
    float n = LOOM_STEPS;
    float t = 0.02 + 0.05 * loomDither();
    vec4 m = vec4(1.0, 0.0, 1.0e3, 0.0);
    bool hit = false;
    // The closest the ray came to a strand, and where, for the glow it leaves.
    float pass = 1.0e3;
    float passT = 0.0;
    float passHue = 0.0;
    vec3 acc = vec3(0.0);
    float through = 1.0;
    float veil = 0.0;
    int skins = 0;
    bool inside = false;
    float skinPrev = 1.0e3;
    float stepPrev = 1.0;
    for (int i = 0; i < LOOM_MAX_STEPS; i++) {
        if (float(i) >= n || t > gLoomFar) break;
        m = loomField(ro + rd * t);
        float eps = 0.0015 * (1.0 + t);
        float g = m.x / (0.4 + 0.25 * t);
        if (g < pass) {
            pass = g;
            passT = t;
            passHue = m.w;
        }
        if (m.x < eps) {
            hit = true;
            break;
        }
        float stride = max(m.x * gLoomStride, 0.25 * eps);
        if (skins < LOOM_SKINS) {
            float s = abs(m.z);
            float air = exp(-3.0 * t / gLoomFar);
            veil += through * air * 0.012 / (1.0 + 40.0 * s * s);
            if (s < eps) {
                if (!inside) {
                    // How squarely the ray met the skin, from how fast its distance fell over the last step.
                    float rim = 1.0 - clamp((skinPrev - s) / stepPrev, 0.0, 1.0);
                    // Nothing lights right at the eye: flying through a skin must not fill the frame.
                    acc += loomCol(gLoomTone + 0.35 + 0.4 * t / gLoomFar + 0.5 * rim) * (0.1 + 0.9 * rim * rim) *
                        gLoomSkin * through * air * smoothstep(0.1, 0.8, t);
                    through *= 0.72;
                    skins++;
                    inside = true;
                }
                // Off the skin in short steps, never past a strand: a ray along a skin cannot stall the trace.
                stride = min(stride, 0.02 + 6.0 * eps);
            } else {
                inside = false;
                stride = max(min(m.x, s) * gLoomStride, 0.25 * eps);
            }
            skinPrev = s;
        }
        stepPrev = stride;
        t += stride;
    }
    t = min(t, gLoomFar);
    float depth = t / gLoomFar;
    float hue = gLoomTone + 0.4 * depth;
    vec3 col = vec3(0.0);
    if (hit) {
        vec3 p = ro + rd * t;
        vec3 nor = loomNormal(p, rd, 0.002 * (1.0 + t));
        float ndv = clamp(dot(nor, -rd), 0.0, 1.0);
        float rim = (1.0 - ndv) * (1.0 - ndv);
        float occ = clamp(loomField(p + nor * 0.2).x / 0.2, 0.0, 1.0);
        float lamp = ndv / (1.0 + 0.1 * t * t);
        vec3 own = loomCol(hue + m.w);
        col = own * (0.04 + 0.45 * lamp) * (0.3 + 0.7 * occ);
        // The thread is lit from within, most along its fibres; far off, the fibres blend to their mean.
        float ply = mix(0.4, loomPly(m.y), exp(-0.12 * t * t));
        col += own * (0.22 + 0.9 * gLoomFibre * ply) * (0.35 + 0.65 * occ);
        col += loomCol(hue + m.w + 0.45) * rim * (0.2 + 0.6 * occ);
        float gloss = clamp(dot(reflect(rd, nor), normalize(vec3(0.3, 0.7, 0.6))), 0.0, 1.0);
        col += loomCol(hue + m.w + 0.6) * pow(gloss, 24.0) * 0.4;
    }
    float air = exp(-3.0 * depth);
    float passAir = exp(-3.0 * passT / gLoomFar) * smoothstep(0.05, 0.6, passT);
    vec3 glow = loomCol(gLoomTone + 0.4 * passT / gLoomFar + passHue) * 0.22 * gLoomSkin * exp(-14.0 * max(pass, 0.0)) * passAir;
    // What lies behind the skins is dimmed by them; the far end of the way is never quite dark.
    vec3 lit = (col * air + glow) * through + acc + loomCol(gLoomTone + 0.6) * veil * gLoomSkin +
        loomCol(hue + 0.5) * 0.03 * (1.0 - air);
    // The midground haze in front of the strand, the far layer wherever nothing was hit (lib_layers).
    return layerCompose(lit, ro, rd, t, gLoomFar, hit ? 0.0 : through);
}
