#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_palette
//#include lib_scene_grade
//#include lib_truchet

// Truchet Quad: a multiscale Truchet. Cells split into four by hash, up to two
// times, so tiles of three sizes sit side by side - and the pattern still
// joins seamlessly across every border, big tile to small.
//
// HOW THE SIZES JOIN. A plain quarter tile's edge reads one way from corner to
// corner, and two half-size tiles side by side can never read the same. So
// these tiles are drawn as filled bands instead of lines: each tile is a
// background with a band a third of its size wide round two opposite corners,
// and every level SWAPS the two colours (level 0 band on ground, level 1
// ground on band, ...). Then the only places the border patterns of a tile and
// its half-size neighbours disagree are near the points where the smaller
// tiles' corners land on the big tile's edge. Each of those points - every
// vertex of the tiling - gets a disc of radius one sixth of the size of the
// grid it first appears on, in that level's band colour, spilling over onto
// whatever tile is next to it. Worked through edge by edge, the discs fill
// exactly the stretches that disagreed, at every pairing of the three sizes.
// (The disc idea is Christopher Carlson's multiscale Truchet; this is its
// geometry worked out here, not anyone's code.)
//
// The cost is bounded: the leaf's own level (two hashes), its tile, its four
// corners (no hash - a vertex's grid level is read off its coordinates), and
// at most two neighbour lookups for the finer vertices on its nearest edge.
//
// ---- audio ------------------------------------------------------------------
//
// Onsets re-roll a scattered third of the tiles, at every size, cross-fading
// each band onto the other diagonal over the onset envelope. The bass breathes
// the scale; the key leans the hue. Brightness follows SND_ENERGY_TREND only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the slow drift and the hue field. Complexity: uMorph
// raises how often cells split, so the picture fills with the small sizes.
// Chaos: uWarp/uTurbulence bend the plane before tiling; uTwist in view().
// Energy: uIntensity scales the bass breathing. Colour: both inks are pal(),
// drifting with a smooth field. Space: view().

float gP1;
float gP2;

/** The leaf tile holding q: xy = its lower-left corner, z = its level (size 2^-level). */
vec3 leafAt(vec2 q) {
    vec2 c0 = floor(q);
    if (truchetHash3(vec3(c0, 3.0)) >= gP1) return vec3(c0, 0.0);
    vec2 c1 = floor(q * 2.0);
    if (truchetHash3(vec3(c1, 5.0)) >= gP2) return vec3(c1 * 0.5, 1.0);
    return vec3(floor(q * 4.0) * 0.25, 2.0);
}

/** The coarsest grid a vertex lies on (its coordinates are exact quarters). */
float nativeLevel(vec2 v) {
    if (all(equal(fract(v), vec2(0.0)))) return 0.0;
    if (all(equal(fract(v * 2.0), vec2(0.0)))) return 1.0;
    return 2.0;
}

/** Band coverage in a tile: f is 0..1 across it, s its size, the band a third wide round two opposite corners. */
float bandMask(vec2 f, float bit, float s, float px) {
    vec2 g = bit > 0.5 ? vec2(1.0 - f.x, f.y) : f;
    float e = 1.0 / 6.0 - min(abs(length(g) - 0.5), abs(length(g - 1.0) - 0.5));
    return smoothstep(-px, px, e * s);
}

/** Paint the disc of a tiling vertex of grid level k over the ink. */
float disc(float ink, vec2 p, vec2 v, float k, float px) {
    float r = exp2(-k) / 6.0;
    return mix(ink, 1.0 - mod(k, 2.0), 1.0 - smoothstep(-px, px, length(p - v) - r));
}

void main() {
    vec2 uv = view();
    float t = truchetTime();
    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float morph = truchetMorph();
    float hits = floor(SND_ONSET_COUNT);
    gP1 = 0.3 + 0.45 * morph;
    gP2 = 0.2 + 0.5 * morph;

    float scale = 1.6 * truchetBreath(SND_BASS_PRESENCE);
    vec2 p = truchetWarp(uv * scale + vec2(t * 0.03, t * 0.02), t);
    float px = truchetPx(p);

    vec3 leaf = leafAt(p);
    float lv = leaf.z;
    float s = exp2(-lv);
    vec2 f = (p - leaf.xy) / s;

    vec3 roll = truchetRoll(leaf.xy * 4.0 + vec2(lv * 4099.0, 0.0), hits, SND_ONSET_ENV, 3.0);
    float m = bandMask(f, step(0.5, roll.y), s, px);
    if (step(0.5, roll.x) != step(0.5, roll.y) && roll.z < 1.0) {
        m = mix(bandMask(f, step(0.5, roll.x), s, px), m, roll.z);
    }
    // Ink 0 / 1 are the two colours; even levels are ground 0 with band 1.
    float ground = mod(lv, 2.0);
    float ink = mix(ground, 1.0 - ground, m);

    // The leaf's own corners.
    for (int k = 0; k < 4; k++) {
        vec2 v = leaf.xy + s * vec2(float(k - 2 * (k / 2)), float(k / 2));
        ink = disc(ink, p, v, nativeLevel(v), px);
    }

    // Finer vertices on the nearest edge, where a subdivided neighbour's
    // corners land: at the edge midpoint, and (for the biggest tiles) the
    // nearer quarter point. Each exists only if the neighbour across is split
    // that far; the lookup lands just inside it.
    if (lv < 1.5) {
        vec2 de = min(f, 1.0 - f);
        bool vert = de.x < de.y;
        float along = vert ? f.y : f.x;
        vec2 axis = vert ? vec2(0.0, 1.0) : vec2(1.0, 0.0);
        vec2 nrm = vert ? vec2(f.x < 0.5 ? -1.0 : 1.0, 0.0) : vec2(0.0, f.y < 0.5 ? -1.0 : 1.0);
        vec2 e0 = leaf.xy + s * (vert ? vec2(step(0.5, f.x), 0.0) : vec2(0.0, step(0.5, f.y)));
        vec2 nudge = nrm * 0.01 + axis * 0.01;
        vec2 vm = e0 + axis * (0.5 * s);
        if (leafAt(vm + nudge).z > lv + 0.5) ink = disc(ink, p, vm, lv + 1.0, px);
        if (lv < 0.5) {
            vec2 vq = e0 + axis * ((along < 0.5 ? 0.25 : 0.75) * s);
            if (leafAt(vq + nudge).z > 1.5) ink = disc(ink, p, vq, 2.0, px);
        }
    }

    float hue = 0.04 * sin(t * 0.02) + 0.1 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue);
    float field = 0.14 * truchetField(p * 1.7, t);
    vec3 inkA = pal(hue + 0.62 + field) * 0.1;
    vec3 inkB = pal(hue + 0.12 + field) * 0.88;
    vec3 col = mix(inkA, inkB, ink);
    // A fine dark contour wherever the ink turns.
    col *= 1.0 - 0.45 * clamp(fwidth(ink) * 1.2, 0.0, 1.0);

    col *= 0.85 + 0.12 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
