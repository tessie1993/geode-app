// The Loom family: the part of lib_loom that calls the style's loomMap, which the style
// defines between the two includes. The field, a strand's normal and the trace through the skins to a strand.
//
// Why a second file: no shader may declare a function ahead of its body
// (style_reads_test: noStyleDeclaresAFunctionAhead), so what calls the
// style's map comes after it, as lib_flame_march, lib_opal_march and
// lib_hollow_march do.
//
// THE SHADING PASS (lib_shade). Where a ray meets a strand, a shadow ray toward the key light the gloss
// comes from (loomShadow) lays the weave's soft shadows over it by Soft shadows (uShadow): on the lamp's light
// on the thread, its rim and its gloss, never on the light inside its fibres. The steps a ray spent before it
// met a strand, or gave up, glow as a halo in the palette by Bloom (shadeStepGlow). The escape colour is
// lib_layers' (the haze's and the far layer's folds).
//
// INCLUDE ORDER: after lib_loom and after the style's loomMap.

/** The key light the thread's gloss comes from, and its shadows. */
#define LOOM_KEY normalize(vec3(0.3, 0.7, 0.6))
/** How far a shadow ray looks for a strand, world units. */
#define LOOM_SHADOW_REACH 2.0
/** The deepest a strand's shadow goes at Soft shadows 1: the eye's own lamp still reaches in. */
#define LOOM_SHADOW_DEEP 0.85

vec4 loomField(vec3 p) {
    float k = growOpen(p, 0.18);
    // A domain warp over the bend: always a little, more with Warp (and k2 FLOW) and Turbulence (lib_layers).
    float amt = 0.05 + 0.14 * gLoomFlow + 0.08 * gLoomChaos;
    vec3 q = growOpened(layerWarp(loomBend(p), amt), k);
    vec4 m = loomMap(q);
    // Grow and layers (lib_grow), each off at its default. Spawn slides a second weave off the first along the
    // way, so the strands and skins double; Fractal spins thinner strands a third the size round every strand.
    // The layers: Front layer opens the weave round the eye, Back layer the weave down the way, and it
    // billows the skins, the loom's own backdrop.
    if (uSpawn > 0.0) {
        vec4 c = loomMap(q + growSpawnShift(vec3(0.0, 0.0, 1.0), 1.0));
        c.x = growClearEye(c.x, p, 0.5);
        if (c.x < m.x) m = vec4(c.x, c.y, m.z, c.w);
        if (abs(c.z) < abs(m.z)) m.z = c.z;
    }
    float reach = growFractalReach(0.15);
    if (reach > 0.0) {
        vec4 c = loomMap(growChildSpace(q, 3.0, 2.0));
        c.x = growClearEye(growFractalChild(c.x / 3.0, m.x, reach), p, 0.5);
        if (c.x < m.x) m = vec4(c.x, c.y, m.z, c.w);
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
    vec3 g = e.xyy * loomField(p + e.xyy).x + e.yyx * loomField(p + e.yyx).x +
        e.yxy * loomField(p + e.yxy).x + e.xxx * loomField(p + e.xxx).x;
    float l = length(g);
    return l > 1.0e-9 ? g / l : -rd;
}

/**
 * The key's light at p on a strand of normal nor (lib_shade): a shadow ray from just off the thread toward
 * LOOM_KEY through the strands (the skins cast none), kept as its closest pass over the distance gone. 1
 * without a ray at Soft shadows 0.
 */
float loomShadow(vec3 p, vec3 nor) {
    if (!shadeShadowOn()) return 1.0;
    float steps = shadeShadowSteps();
    vec3 o = p + nor * 0.01;
    float res = 1.0;
    float t = 0.02;
    for (int i = 0; i < SHADE_SHADOW_MAX; i++) {
        if (float(i) >= steps || t > LOOM_SHADOW_REACH) break;
        float h = loomField(o + LOOM_KEY * t).x;
        res = shadePenumbra(res, h, t);
        if (res < 0.01) break;
        t += shadeShadowStep(h * gLoomStride, LOOM_SHADOW_REACH);
    }
    return shadeLight(res, LOOM_SHADOW_DEEP);
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
    // The steps spent before the strand (the step glow's measure).
    float used = 0.0;
    for (int i = 0; i < LOOM_MAX_STEPS; i++) {
        if (float(i) >= n || t > gLoomFar) break;
        used = float(i);
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
        float key = loomShadow(p, nor);
        vec3 own = loomCol(hue + m.w);
        col = own * (0.04 + 0.45 * lamp) * (0.3 + 0.7 * occ) * key;
        // The thread is lit from within, most along its fibres; far off, the fibres blend to their mean.
        float ply = mix(0.4, loomPly(m.y), exp(-0.12 * t * t));
        col += own * (0.22 + 0.9 * gLoomFibre * ply) * (0.35 + 0.65 * occ);
        col += loomCol(hue + m.w + 0.45) * rim * (0.2 + 0.6 * occ) * key;
        float gloss = clamp(dot(reflect(rd, nor), LOOM_KEY), 0.0, 1.0);
        col += loomCol(hue + m.w + 0.6) * pow(gloss, 24.0) * 0.4 * key;
    }
    float air = exp(-3.0 * depth);
    float passAir = exp(-3.0 * passT / gLoomFar) * smoothstep(0.05, 0.6, passT);
    vec3 glow = loomCol(gLoomTone + 0.4 * passT / gLoomFar + passHue) * 0.22 * gLoomSkin * exp(-14.0 * max(pass, 0.0)) * passAir;
    // What lies behind the skins is dimmed by them; the far end of the way is never quite dark.
    vec3 lit = (col * air + glow) * through + acc + loomCol(gLoomTone + 0.6) * veil * gLoomSkin +
        loomCol(hue + 0.5) * 0.03 * (1.0 - air);
    // The step glow (lib_shade): a halo where the ray crept past the strands, by Bloom.
    lit += loomCol(hue + 0.4) * 0.4 * shadeStepGlow(used, n);
    // The midground haze in front of the strand, the far layer wherever nothing was hit (lib_layers).
    return layerCompose(lit, ro, rd, t, gLoomFar, hit ? 0.0 : through);
}
