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

// Lucid Disc: a hyperbolic tiling in the Poincare disc, gilded. The disc is
// folded by the reflection group of the (5, 4, 2) triangle: the angle is
// mirrored into a wedge of pi/5, then any point inside the wedge's third side
// (a circle orthogonal to the rim, so a hyperbolic straight line) is inverted
// through it, and the two folds repeat until the point lands in the
// fundamental triangle. Tiles shrink without end toward the rim. Before the
// fold, a Mobius translation glides the whole tiling through the disc - the
// hyperbolic plane's own version of panning.
//
// Inside each tile, a cellular (Voronoi) nest three levels deep: crazed
// lacquer whose cracks are laid in gold leaf. Alternate tiles (odd numbers of
// inversions) take the lacquer's second tone, which is what makes the tiling
// legible.
//
// ---- audio ------------------------------------------------------------------
//
// A SECTION change (uSectionCount, eased by uEvents.z) re-seeds the piece:
// the hash of the section count picks the Mobius glide's heading, the
// lacquer's pair of hues and where in the cell field the tiles sample. The
// new seed is reached by sliding (lucidSeed), never cut to, so a section
// boundary reads as the tiling sailing off somewhere new over about a
// second. uTrebleSmooth sharpens the gold sheen. Brightness follows uSwell
// gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock (glide and cell drift). Complexity: uMorph
// past 0.5 adds a nest level, and uMorph also raises the cell density.
// Chaos: uTurbulence/uWarp raise warp strength and jitter the cells; uTwist
// twists the plane before the disc map. Color: both lacquer tones are pal(),
// and the leaf is gold toned by pal(). Energy: uIntensity scales how far a
// section seed throws the glide and the treble sheen. Space: uZoom/uRotation
// through lucidView (zoom scales the disc on screen). The user's kaleidoscope
// is skipped: the disc is already a reflection group.

#define DISC_P 5.0
#define DISC_Q 4.0

vec2 cmul(vec2 a, vec2 b) { return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
vec2 cdiv(vec2 a, vec2 b) { return cmul(a, vec2(b.x, -b.y)) / max(dot(b, b), 1e-6); }

void main() {
    vec2 uv = lucidView(false);
    float t = uTime * uSpeed;
    float flow = uFlowPhase * uSpeed;
    float swell = clamp(uSwell, 0.0, 1.5);
    float treb = clamp(uTrebleSmooth, 0.0, 1.5) * uIntensity;
    float seed = lucidSeed(uSectionCount, uEvents.z);

    float aspect = uResolution.x / max(uResolution.y, 1.0);
    vec2 z = uv / min(aspect, 1.0) * 1.02;
    float rz = length(z);

    // Mobius glide: z -> (z - b) / (1 - conj(b) z), b orbiting inside the disc.
    float heading = seed * LUCID_TAU * uIntensity + t * 0.03;
    vec2 b = 0.42 * vec2(cos(heading), sin(heading)) * (0.6 + 0.4 * sin(flow * 0.11));
    vec2 m = cdiv(z - b, vec2(1.0, 0.0) - cmul(vec2(b.x, -b.y), z));

    // The (p, q, 2) triangle's third side: centre (d, 0), radius rr.
    float pa = LUCID_PI / DISC_P;
    float qa = LUCID_PI / DISC_Q;
    float rr = sin(pa) / sqrt(cos(qa) * cos(qa) - sin(pa) * sin(pa));
    float d = sqrt(1.0 + rr * rr);

    int folds = lucidBudget(14, 9);
    float flips = 0.0;
    for (int i = 0; i < 14; i++) {
        if (i >= folds) break;
        float a = atan(m.y, m.x);
        a = mod(a, 2.0 * pa);
        if (a > pa) a = 2.0 * pa - a;
        m = vec2(cos(a), sin(a)) * length(m);
        vec2 v = m - vec2(d, 0.0);
        float l2 = dot(v, v);
        if (l2 >= rr * rr) break;
        m = vec2(d, 0.0) + v * (rr * rr / l2);
        flips += 1.0;
    }

    int depth = lucidDetail(3, 2);
    // One octave: a cellular tap is nine hashes, and the nest already layers it.
    int oct = 1;
    vec2 q = m * (7.0 + 3.0 * clamp(uMorph, 0.0, 1.0)) + vec2(seed * 31.0, seed * 17.0);
    q += lucidJitter(q, t);
    vec4 n = lucidNest(q, depth, 0.9 * lucidChaos(), t * 0.15, LUCID_CELLS, oct);
    vec3 cell = lucidCells(q + 0.9 * n.xy, 0.85);
    float crack = 1.0 - smoothstep(0.02, 0.09, cell.y - cell.x);

    float parity = mod(flips, 2.0);
    vec3 lacquerA = pal(seed) * 0.32;
    vec3 lacquerB = pal(seed + 0.5) * 0.18;
    vec3 lacquer = mix(lacquerA, lacquerB, parity) * (0.7 + 0.5 * cell.z);

    // Gold: a fake sheen from the nest's offset as if it were a surface normal.
    vec3 nrm = normalize(vec3(n.xy * 1.4, 1.0));
    float sheen = pow(max(dot(nrm, normalize(vec3(-0.4, 0.6, 0.7))), 0.0), 6.0 + 10.0 * clamp(treb, 0.0, 1.0));
    vec3 leaf = mix(vec3(0.86, 0.62, 0.24), pal(0.1) * vec3(1.0, 0.8, 0.45), 0.35);
    vec3 gold = leaf * (0.55 + 0.45 * n.z) + mix(vec3(1.0, 0.92, 0.7), pal(0.15), 0.25) * sheen * 0.6;
    vec3 col = mix(lacquer, gold, crack);
    col = lucidCubic(col, 0.35);

    // Outside the disc: black, with a thin gilt rim.
    float inside = smoothstep(1.0, 0.985, rz);
    float rim = exp(-abs(rz - 1.0) * 90.0);
    col = col * inside + leaf * rim * 0.5;
    col *= 0.85 + 0.12 * swell;
    fragColor = vec4(grade(col), 1.0);
}
