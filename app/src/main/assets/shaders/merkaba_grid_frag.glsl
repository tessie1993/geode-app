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

// Merkaba Grid: a cathedral of sacred geometry. A triangulated web fills the
// depth, wire-frame geodesic spheres sit along the floor, star tetrahedra hang
// in the field with a glint at every vertex, and a column of light runs up the
// middle and blooms at the top. Everything is drawn in a receding
// perspective, so the whole thing reads as a hall rather than as a pattern.
//
// uv -> mirror -> perspective bands -> web + spheres + merkabas + column.
//
// Nothing here marches: the depth is a 1/y perspective divide, so this stays
// off MARCHED_SCENES and never spends the Detail budget.
//
// The audio contract is the one in lib_scene_motion. Bass breathes the web
// spacing through uBassSmooth, treble picks out the glints, and the harmony
// and bar-phase oscillators steer the lattice density and the constellation
// of lit nodes/hanging merkabas continuously - nothing here is re-rolled on
// a hit any more.
// No term in this file can reach its peak inside one frame.
//
// motion: uHarmony -> lattice density plateau and how fully the hanging
// merkabas resolve (tonal passages hold a settled lattice); uTrebRel -> node
// glints, tetrahedra points and the burst's spoke brightness.

#define MG_TAU 6.2831853
#define MG_ROWS 7.0

float hash11(float n) { return fract(sin(n * 127.1) * 43758.5453); }

float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

vec2 rot(vec2 p, float a) { return mat2(cos(a), -sin(a), sin(a), cos(a)) * p; }

// One line of a wire frame: bright on the line, dark off it, with the width in
// the same units as the coordinate so a receding copy thins out by itself.
float wire(float d, float width) {
    return smoothstep(width, width * 0.25, abs(d));
}

// The triangulated web that fills the background: three line families at 60
// degrees, which is the tiling every geodesic in the reference is built from.
float web(vec2 p, float width) {
    float a = wire(fract(p.x) - 0.5, width);
    float b = wire(fract(rot(p, 1.0472).x) - 0.5, width);
    float c = wire(fract(rot(p, -1.0472).x) - 0.5, width);
    return max(max(a, b), c);
}

// A wire-frame geodesic sphere: the web wrapped over a hemisphere, so the
// spacing crowds toward the limb the way a real projection does.
float geodesic(vec2 p, float radius, float density, float width) {
    float rr = length(p) / max(radius, 1e-4);
    if (rr > 1.0) return 0.0;
    // z of the unit hemisphere, which is what compresses the wires at the edge.
    float z = sqrt(max(0.0, 1.0 - rr * rr));
    vec2 s = vec2(atan(p.y, p.x) / MG_TAU, z);
    float lines = web(s * density, width * 1.6);
    // Latitude rings, denser near the limb.
    lines = max(lines, wire(fract(z * density * 0.55) - 0.5, width * 1.4));
    // A soft skin under the wires so the sphere reads as a solid volume.
    return lines * (0.35 + 0.65 * z) + 0.06 * z;
}

// A star tetrahedron seen flat: two opposed triangles. Returns the edge glow;
// `glint` is the extra brightness at the six points.
float merkaba(vec2 p, float size, float width, out float glint) {
    float best = 1e9;
    float tip = 0.0;
    for (int i = 0; i < 6; i++) {
        // Six vertices, the two triangles interleaved.
        float a = float(i) * (MG_TAU / 6.0);
        vec2 v = vec2(cos(a), sin(a)) * size;
        // Each edge joins a vertex to the one two steps round, which is what
        // draws two triangles rather than a hexagon.
        float b = a + 2.0 * (MG_TAU / 6.0);
        vec2 w = vec2(cos(b), sin(b)) * size;
        vec2 e = w - v;
        float t = clamp(dot(p - v, e) / max(dot(e, e), 1e-6), 0.0, 1.0);
        best = min(best, length(p - v - e * t));
        tip = max(tip, smoothstep(size * 0.22, 0.0, length(p - v)));
    }
    glint = tip;
    return wire(best, width);
}

void main() {
    vec2 uv = view();
    // Mirrored left to right: the reference is a symmetric hall.
    vec2 m = vec2(abs(uv.x), uv.y);

    float bass = clamp(uBassSmooth, 0.0, 1.5);
    float treb = clamp(uTrebleSmooth, 0.0, 1.5);
    float swell = clamp(uSwell, 0.0, 1.5);
    float trebRel = clamp(uTrebRel, 0.0, 2.0);
    // Harmony (smoothed over 1.0s) steps the web between 5, 6 and 7
    // subdivisions and holds it there while the passage stays tonal.
    float density = 5.0 + floor(clamp(uHarmony, 0.0, 1.0) * 2.999);

    // ---- the field ---------------------------------------------------------
    //
    // 1/y perspective: the plane recedes toward the top of the frame, so a
    // constant step in `field` is a shrinking step on screen.
    float persp = 1.0 / (1.28 - 0.42 * clamp(m.y, -1.0, 1.0));
    // Advected along the travel direction, through the integrated phase, so a
    // change of pace never slides the hall sideways in one frame.
    vec2 field = fluidWarp(m * persp * 2.2, 0.8, 0.06) + flowOffset(0.30);
    field *= 1.0 + 0.06 * bass;

    float width = 0.02 + 0.012 * persp;
    float lattice = web(field * density * 0.5, width);
    vec3 col = pal(0.58) * lattice * (0.18 + 0.30 * persp);
    // Lit nodes: a sparse subset of the lattice cells, re-chosen on every spawn
    // and grown in over a second so a new constellation never appears at once.
    vec2 node = floor(field * density * 0.5);
    // The constellation drifts with uOrbit rather than being re-rolled on a
    // hit: it holds while the wander target holds and eases to a new set
    // whenever novelty or a section boundary re-targets uOrbit.
    float lit = step(0.80, hash21(node + floor(uOrbit * 97.0)));
    float nodeGlow = lattice * lit * (0.4 + 0.8 * treb) * (0.85 + 0.15 * uBarOsc);
    col += mix(pal(0.44), pal(0.86), hash21(node)) * nodeGlow * 0.55;

    // ---- geodesic spheres along the floor ----------------------------------
    //
    // Three rows receding, each row offset so they read as scattered rather
    // than as a grid of the same ball.
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        float depth = 0.55 + fi * 0.30;
        float y = -0.95 + fi * 0.34;
        float x = mod(fi * 0.47 + uFlowPhase * 0.05, 1.0) * 1.3;
        vec2 c = (m - vec2(x, y)) / depth;
        float radius = 0.62 - 0.10 * fi;
        float shell = geodesic(c, radius, density * 2.2, width * 1.4 / depth);
        vec3 tint = mix(pal(0.55), pal(0.40), fi / 3.0);
        // Nearer spheres sit in front, so a later row cannot draw over an
        // earlier one: max() on the coverage is enough at this depth spread.
        col = mix(col, tint * (0.25 + 0.9 * shell), min(shell * 1.4, 1.0));
    }

    // ---- hanging star tetrahedra -------------------------------------------
    for (int i = 0; i < 5; i++) {
        float fi = float(i);
        float seed = hash11(fi * 7.31 + floor((uOrbit.x + uOrbit.y) * 31.0));
        float depth = 0.5 + seed * 0.7;
        // Each drifts on its own slow orbit, plus the shared travel offset.
        vec2 c = m - vec2(0.15 + seed * 1.25, 0.15 + fract(seed * 3.7) * 0.9);
        c -= flowOffset(0.12) * (0.4 + seed);
        c += 0.05 * vec2(sin(uTime * (0.11 + seed * 0.07)), cos(uTime * (0.09 + seed * 0.05)));
        c = rot(c / depth, uFlowPhase * (0.3 + seed * 0.4));
        float glint;
        float edge = merkaba(c, 0.30, 0.026, glint);
        vec3 tint = mix(pal(0.30), pal(0.90), seed);
        // Harmony resolves the merkabas: a tonal passage holds them fully
        // formed, a noisy one lets them fade toward half strength.
        float show = mix(0.55, 1.0, clamp(uHarmony, 0.0, 1.0));
        col += tint * edge * 0.55 * show;
        // The glint rides treble's relative level, so it sparkles with the
        // high end rather than swelling on a hit.
        col += mix(vec3(1.0), tint, 0.35) * glint * (0.25 + 0.8 * treb + 0.5 * clamp(trebRel - 1.0, 0.0, 1.0)) * show;
    }

    // ---- the column of light -----------------------------------------------
    //
    // A narrow vertical shaft that blooms where it leaves the top of the hall.
    float shaft = exp(-m.x * m.x * (150.0 - 60.0 * swell));
    float rise = smoothstep(-0.9, 0.95, uv.y);
    col += pal(0.52) * shaft * rise * (0.35 + 0.5 * bass);
    // The burst at the top: a soft disc plus radial spokes that turn with the
    // travel phase.
    vec2 burst = m - vec2(0.0, 0.86);
    float br = length(burst);
    float spokes = 0.55 + 0.45 * cos(atan(burst.y, burst.x) * 12.0 + uFlowPhase * 2.0);
    col += mix(vec3(1.0), pal(0.50), 0.4) * exp(-br * 7.0) * spokes * (0.5 + 0.4 * uBarOsc);
    col += vec3(1.0) * exp(-br * br * 220.0) * 1.6;

    // ---- the particle layer -------------------------------------------------
    //
    // Motes riding the same curl field the lattice is warped by: the fluid half
    // of the look, drifting through the hall rather than pinned to it.
    col += mix(pal(0.48), vec3(1.0), 0.3) * fluidMotes(uv * 0.85, 5.5, 0.15) * 0.28;

    if (!touchIdle()) {
        col += pal(0.5 + 0.15 * sin(uTime * 0.05)) * min(touchWake(uv), 3.0) * 0.05;
    }
    // Vignette, so the hall falls into the dark at the corners.
    col *= 0.62 + 0.38 * smoothstep(2.1, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
