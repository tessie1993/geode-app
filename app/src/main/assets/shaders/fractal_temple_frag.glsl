#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_palette
//#include lib_scene_grade
//#include lib_touch

// Fractal Temple: a triangulated dome closing overhead, a ring of neon lamps
// hanging in the middle of it, a stepped causeway running out to the horizon,
// and a city of filigree spires standing on either side of the way.
//
// uv -> mirror -> horizon split -> dome above, causeway and spires below,
// portal ring on top.
//
// Everything is a 2D perspective construction: the dome is a hemisphere
// projection, the causeway a 1/y divide, the spires a scaled repeat. No
// raymarch, so this stays off MARCHED_SCENES.
//
// Audio: uSwell opens the dome and lengthens the causeway, uBassSmooth lights
// the portal, uTrebleSmooth picks out the filigree. A spike re-seeds the
// skyline (uSpawnSeed), steps the dome's tessellation (uFormPhase) and leans
// the walk (uMoveDir). The portal's flare rides uSpike, which has a rise on
// it, so the ring swells rather than strobing.

#define FT_TAU 6.2831853

vec2 rot(vec2 p, float a) { return mat2(cos(a), -sin(a), sin(a), cos(a)) * p; }

float hash11(float n) { return fract(sin(n * 127.1) * 43758.5453); }

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float wire(float d, float w) { return smoothstep(w, w * 0.25, abs(d)); }

// A triangular tiling: three line families at 60 degrees. The dome and the
// causeway are both drawn with it, which is what makes them read as one build.
float tri(vec2 p, float w) {
    return max(max(wire(fract(p.x) - 0.5, w),
                   wire(fract(rot(p, 1.0472).x) - 0.5, w)),
               wire(fract(rot(p, -1.0472).x) - 0.5, w));
}

// One filigree spire: a tapered cone of stacked, shrinking arcs, which is how
// the reference's lace towers are built.
float spire(vec2 p, float height, float width, float tiers) {
    // Outside the taper, cheap reject.
    float taper = width * (1.0 - clamp(p.y / max(height, 1e-4), 0.0, 1.0));
    if (p.y < 0.0 || p.y > height || abs(p.x) > taper + 0.02) return 0.0;
    // Horizontal courses up the cone.
    float course = wire(fract(p.y * tiers / height) - 0.5, 0.16);
    // Vertical ribs, converging as the cone narrows.
    float rib = wire(fract(p.x / max(taper, 1e-3) * 4.0) - 0.5, 0.16);
    // The skin under the lace, so the tower is not see-through.
    float skin = smoothstep(taper, taper * 0.85, abs(p.x));
    return skin * (0.22 + 0.78 * max(course, rib));
}

void main() {
    vec2 uv = view();
    vec2 m = vec2(abs(uv.x), uv.y);

    float bass = clamp(uBassSmooth, 0.0, 1.5);
    float treb = clamp(uTrebleSmooth, 0.0, 1.5);
    float swell = clamp(uSwell, 0.0, 1.5);
    // A spike steps the dome between three tessellation densities and holds it.
    float tess = 9.0 + 4.0 * floor(uFormPhase * 3.0);
    // The horizon. The whole scene is split on it: dome above, ground below.
    float horizon = -0.12;

    vec3 col = vec3(0.0);

    // ---- the causeway --------------------------------------------------------
    //
    // 1/(horizon - y) is the perspective divide: the ground plane runs away
    // toward the horizon and the steps crowd up against it.
    if (uv.y < horizon) {
        float d = 1.0 / max(horizon - uv.y, 0.02);
        // Walked forward on the integrated phase, never on `uTime * rate`.
        vec2 ground = vec2(m.x * d, d * 0.6 + uFlowPhase * 1.4);
        // The stair treads: one bright nosing per unit of depth.
        float tread = wire(fract(ground.y) - 0.5, 0.10 + 0.05 / d);
        float deck = smoothstep(1.05, 0.35, m.x * d * 0.55);
        vec3 stone = mix(pal(0.60), pal(0.46), clamp(d * 0.12, 0.0, 1.0));
        col += stone * deck * (0.10 + 0.40 * tread);
        // The lit edge down each side of the way.
        col += pal(0.42) * wire(m.x * d * 0.55 - 1.0, 0.06) * (0.4 + 0.5 * treb);

        // ---- the spires ------------------------------------------------------
        //
        // Two rows standing off the edge of the causeway, receding. Which ones
        // are there is re-rolled on a spike and grown in over a second.
        for (int i = 0; i < 6; i++) {
            float fi = float(i);
            float seed = hash11(fi * 9.13 + floor(uSpawnSeed * 41.0));
            // Depth along the way, wrapped so the row is endless.
            float z = fract(seed + uFlowPhase * 0.20) * 4.0 + 0.35;
            float scale = 1.0 / z;
            // Standing just outside the deck, leaning with the walk.
            float x = (0.55 + seed * 0.75) * scale + uMoveDir.x * 0.04 * scale;
            vec2 c = vec2(m.x - x, uv.y - horizon) / scale;
            float lit = spire(c, 0.55 + seed * 0.5, 0.13 + seed * 0.06, 7.0 + floor(seed * 6.0));
            vec3 tint = mix(pal(0.94), pal(0.86), seed);
            float show = spawnGrow(0.9 + seed * 0.6) * smoothstep(4.4, 3.2, z);
            col = mix(col, tint * (0.35 + 0.9 * lit), min(lit * 1.5, 1.0) * show);
            // The star on top, swelling on a spike rather than flashing.
            vec2 tipv = c - vec2(0.0, 0.55 + seed * 0.5);
            col += mix(vec3(1.0), tint, 0.3) * exp(-dot(tipv, tipv) * 900.0) * (0.5 + 0.7 * uSpike) * show;
        }
    }

    // ---- the dome ------------------------------------------------------------
    //
    // A hemisphere seen from inside: the projection crowds the tessellation
    // toward the springing line, which is what makes it read as curved.
    if (uv.y > horizon - 0.05) {
        float h = clamp((uv.y - horizon) / (1.35 - 0.10 * swell), 0.0, 1.0);
        // z of the dome at this height; small near the springing, 1 at the crown.
        float z = sqrt(max(0.0, 1.0 - (1.0 - h) * (1.0 - h)));
        // Longitude compresses as the dome closes over.
        vec2 shell = vec2(m.x / max(z, 0.12), h * 1.6);
        shell = fluidWarp(shell, 0.9, 0.05) + flowOffset(0.12);
        float lace = tri(shell * tess, 0.05);
        vec3 domeCol = mix(pal(0.58), pal(0.50), h);
        float lam = smoothstep(0.0, 0.22, uv.y - horizon);
        col = mix(col, domeCol * (0.10 + 0.55 * lace) * (0.45 + 0.55 * z), lam);
        // Lit facets: a sparse subset, re-chosen on each spawn.
        vec2 cell = floor(shell * tess);
        float on = step(0.86, hash21(cell + floor(uSpawnSeed * 71.0)));
        col += mix(pal(0.44), pal(0.90), hash21(cell)) * lace * on * lam
             * (0.35 + 0.75 * treb) * spawnGrow(1.1);
    }

    // ---- the portal ring -----------------------------------------------------
    //
    // Concentric rings of lamps hanging over the head of the causeway.
    vec2 pc = uv - vec2(0.0, horizon + 0.42);
    float pr = length(pc);
    float pa = atan(pc.y, pc.x);
    for (int i = 0; i < 3; i++) {
        float radius = 0.20 + float(i) * 0.09;
        // Lamps spaced round the ring, turning with the travel phase.
        float lamps = 22.0 + float(i) * 10.0;
        float bead = 0.5 + 0.5 * cos(pa * lamps + uFlowPhase * (1.4 + float(i) * 0.4));
        float band = exp(-pow((pr - radius) * 34.0, 2.0));
        vec3 lampCol = mix(pal(0.92), pal(0.04), float(i) / 3.0);
        col += lampCol * band * (0.25 + 0.75 * bead) * (0.55 + 0.55 * bass);
    }
    // The glow the ring throws into the hall, and its flare on a transient.
    col += pal(0.92) * exp(-pr * 5.0) * (0.10 + 0.22 * uSpike);
    // The dark eye at the middle of the portal.
    col *= 1.0 - 0.55 * exp(-pr * pr * 260.0);

    // ---- the particle layer ---------------------------------------------------
    col += mix(pal(0.50), vec3(1.0), 0.35) * fluidMotes(uv * 0.9, 5.5, 0.15) * 0.26;

    if (!touchIdle()) {
        col += pal(0.5 + 0.15 * sin(uTime * 0.05)) * min(touchWake(uv), 3.0) * 0.05;
    }
    col *= 0.66 + 0.34 * smoothstep(2.2, 0.5, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
