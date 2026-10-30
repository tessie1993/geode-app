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
//#include lib_lucid

// Lucid Hive: a honeycomb of separate little worlds. The plane is cut into
// hexagonal cells and every cell runs its own simplex nest, seeded by the
// cell's id, so each one is a different marbled dream behind the same glass.
// The channel swap is per cell: each cell's hash picks its own channel order,
// so the comb is a quilt of one field shown in three colour permutations,
// separated by dark wax walls with a soft inner glow.
//
// The comb itself drifts slowly along the flow, and each cell's world turns
// at its own rate.
//
// ---- audio ------------------------------------------------------------------
//
// The SNARE (uHits.y) turns every cell inside out: the cell's local frame is
// blended toward its circle inversion (r -> R^2 / r), so the rim of each
// world swaps with its heart. The blend rides lucidBump of the snare - it
// starts at zero on the hit, swells as the hit decays and relaxes back - so
// the inversion breathes rather than snapping. uHarmony slows the cells'
// turning on tonal passages. Brightness follows uSwell gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock, the comb's drift and the cells' turning.
// Complexity: uMorph shrinks the cells (more of them) and, past 0.5, adds a
// nest level and an octave. Chaos: uTurbulence/uWarp raise warp strength and
// jitter each cell's world; uTwist twists the comb. Color: every cell's tint
// is pal() at its own hash. Energy: uIntensity scales the snare inversion.
// Space: uZoom/uRotation through lucidView. The user's kaleidoscope folds the
// comb (through lucidView).

void main() {
    vec2 uv = lucidView(true);
    float t = uTime * uSpeed;
    float flow = uFlowPhase * uSpeed;
    float snare = clamp(uHits.y, 0.0, 1.0);
    float swell = clamp(uSwell, 0.0, 1.5);
    float calm = clamp(uHarmony, 0.0, 1.0);

    int depth = lucidDetail(3, 2);
    int oct = lucidDetail(3, 2);

    vec2 p = uv * (2.4 + 1.2 * clamp(uMorph, 0.0, 1.0)) + vec2(flow * 0.12, flow * 0.05);
    vec4 hx = lucidHex(p);
    vec2 l = hx.xy;
    vec2 id = hx.zw;
    float h = lucidHash(id * 3.7 + 1.3);

    // Inside-out: blend toward the inversion in the cell's inscribed circle.
    float inv = clamp(lucidBump(snare) * 0.85 * uIntensity, 0.0, 0.95);
    float R2 = 0.2;
    vec2 li = l * (R2 / max(dot(l, l), 0.035));
    vec2 lc = mix(l, li, inv);

    float spin = t * (0.15 + 0.35 * h) * (1.0 - 0.6 * calm) + h * LUCID_TAU;
    vec2 q = lucidRot(spin) * lc * 2.6 + id * 2.31;
    q += lucidJitter(q, t);
    vec4 n = lucidNest(q, depth, 1.4 * lucidChaos(), t * 0.3, LUCID_SIMPLEX, oct);

    vec3 raw = 0.5 + 0.5 * vec3(n.z, n.x, n.y);
    vec3 col = raw * mix(vec3(1.0), pal(h + 0.3 * n.z), 0.55);
    col = lucidChannelTurn(col, floor(h * 3.0) + 0.3 * n.w);
    col = lucidCubic(col, 0.9);
    col = lucidCube(col * 1.35);

    // Wax walls, with a soft glow on the inside of each wall.
    float e = lucidHexEdge(l);
    float wallMask = smoothstep(0.47, 0.43, e);
    col *= wallMask;
    col += pal(h + 0.5) * smoothstep(0.3, 0.45, e) * wallMask * 0.18;

    col *= 0.85 + 0.12 * swell;
    fragColor = vec4(grade(col), 1.0);
}
