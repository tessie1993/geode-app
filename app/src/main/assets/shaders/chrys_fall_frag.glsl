#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_chrys

// Chrysanthemum Fall: loose petals drifting down through dusk air past a
// great out-of-focus bloom. Four depth layers of falling petals, each a grid
// of cells holding at most one petal: far layers small, slow, hazy and soft,
// near layers large and quick. Every petal tumbles in 3D - it turns in the
// plane and rolls about its own length, so it narrows to a sliver and opens
// again, showing its paler underside on the way round. The bloom behind is
// drawn with a deliberately huge edge softness, as a lens would leave it.
//
// ---- audio ------------------------------------------------------------------
//
// The hi-hat (uHits.z) FLIPS the petals: each one rolls over about its own
// length by up to a half turn on a hat, the direction its own (hashed), so a
// run of hats sets the whole air flickering between top and underside, and
// the petals roll back as the level falls. The air band (uBandsHi.z)
// shimmers a thin light along the petal edges (an edge highlight moving
// along the rim, never a full-frame lift). The fall rides uFlowPhase, so
// loudness sets the speed and never the direction. Brightness follows uSwell
// only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the tumbling, the sway and the shimmer's travel (the
// fall itself stays on uFlowPhase). Complexity: uMorph fills more cells, up
// to roughly twice the petals. Chaos: uTurbulence + uWarp blow a gustier
// wind (wider, less regular sway) and tear the petal edges; uTwist curls
// every petal. Energy: uIntensity scales the hat flip and the air shimmer.
// Colour: pal() for petals, bloom and sky. Space: uZoom via view().

#define FA_LAYERS 4

void main() {
    vec2 uv = view();
    float t = uTime * uSpeed;
    float swell = clamp(uSwell, 0.0, 1.5);
    float hat = clamp(uHits.z * uIntensity, 0.0, 1.5);
    float air = clamp(uBandsHi.z * uIntensity, 0.0, 1.5);
    float chaos = clamp(uTurbulence + uWarp, 0.0, 2.0);
    float fill = mix(0.5, 0.05, clamp(uMorph, 0.0, 1.0));
    float hue = 0.9 + 0.02 * sin(t * 0.02);

    // Dusk sky.
    vec3 col = mix(pal(hue + 0.55) * 0.05, pal(hue + 0.05) * 0.16, smoothstep(-1.0, 1.0, uv.y));
    // The bloom behind, far out of focus.
    vec2 bp = chrysTurn(t * 0.01) * (uv - vec2(0.35, 0.25));
    vec4 bpet = chrysPetal(bp, 14.0, 0.12, 0.7, 0.55, 0.0, 0.3, 0.4);
    float bm = smoothstep(0.09, -0.09, bpet.x);
    col = mix(col, pal(hue) * 0.22 * mix(0.6, 1.0, clamp(bpet.y, 0.0, 1.0)), bm * 0.85);
    col += pal(hue + 0.1) * 0.1 * chrysCore(length(bp), 0.18);

    for (int i = 0; i < FA_LAYERS; i++) {
        float k = float(i);
        float f = k / float(FA_LAYERS - 1);                 // 0 far .. 1 near
        float scale = mix(9.0, 2.6, f);
        float fall = mix(0.12, 0.42, f);
        float sway = (0.25 + 0.35 * chaos) * sin(t * (0.3 + 0.1 * k) + k * 1.7);
        vec2 q = uv * scale + vec2(sway + k * 17.3, uFlowPhase * fall * scale * 0.35 + k * 9.1);
        vec2 cell = floor(q);
        float h = chrysHash(cell + k * 31.0);
        if (h < fill) continue;
        float h2 = chrysHash(cell + k * 31.0 + 7.7);
        vec2 jitter = (vec2(h, h2) - 0.5) * 0.3;
        vec2 p = fract(q) - 0.5 - jitter;
        // Wind gusts nudge each petal on its own.
        p.x += chaos * 0.06 * sin(t * 1.3 + h * 40.0);

        float spin = h2 * CHRYS_TAU + t * (h - 0.5) * 1.2;
        float flipDir = h2 > 0.5 ? 1.0 : -1.0;
        float roll = t * (0.4 + 0.8 * h) + h2 * 6.0 + flipDir * 1.6 * hat;
        float width = cos(roll);
        vec2 lq = chrysTurn(spin) * p;
        float len = 0.5;
        lq.y += 0.5 * len;
        lq.x -= uTwist * 4.0 * (lq.y - 0.25) * (lq.y - 0.25);
        lq.x /= max(abs(width), 0.07);
        vec4 pet = chrysLeaf(lq, len, 0.5, 0.25, 0.4);
        pet.x *= max(abs(width), 0.07);
        pet.x += chrysFray(p + cell, chaos);

        // Depth of field: far petals soft, the nearest a little soft too.
        float blur = mix(0.08, 0.012, smoothstep(0.0, 0.66, f)) + 0.03 * smoothstep(0.66, 1.0, f);
        float m = smoothstep(blur, -blur, pet.x);
        float u = clamp(pet.y, 0.0, 1.0);
        bool under = width < 0.0;
        vec3 top = pal(hue + 0.1 * h) * mix(0.55, 1.0, u);
        vec3 bottom = mix(pal(hue + 0.1 * h), vec3(1.0, 0.95, 0.95), 0.45) * 0.7;
        vec3 pc = (under ? bottom : top) * (0.55 + 0.45 * abs(width));
        // Air shimmer: a light running along the rim.
        float rim = smoothstep(0.03, 0.0, abs(pet.x)) * step(pet.x, 0.0);
        float run = pow(0.5 + 0.5 * sin(u * 18.0 - t * 6.0 + h * 30.0), 6.0);
        pc += mix(pc, vec3(1.0), 0.5) * rim * run * 0.5 * air;
        // Haze: far layers sink into the sky colour.
        pc = mix(pal(hue + 0.4) * 0.08, pc, mix(0.35, 1.0, f));
        col = mix(col, pc, m);
    }

    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
