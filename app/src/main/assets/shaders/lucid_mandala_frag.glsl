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

// Lucid Mandala: a simplex nest four levels deep, seen through a kaleidoscope.
// The fold mirrors one wedge of the warped field around the centre, and the
// field inside the wedge streams outward along the radius, so the mandala
// keeps being born at its heart and dissolving at the rim. Coloured as cut
// jewels: the surface read picks a palette hue, rings of the radius pick its
// depth, and the channel order turns with the radius so every ring of the
// mandala is a different setting of the same stone.
//
// Simplex rather than value noise because value noise's square grid shows as
// a plaid once it is mirrored a dozen ways.
//
// ---- audio ------------------------------------------------------------------
//
// Hats (uHits.z) add FOLD SEGMENTS: the mandala has 8 arms at rest and grows
// toward 12 as the hats get busy. Segment counts are integers, so the fold is
// cross-faded between the two neighbouring even counts by the fractional
// part: arms split and merge instead of popping. The slow accumulated turn
// (uDrift) spins the whole figure; uMidSmooth sharpens the jewel contrast.
// Brightness follows uSwell gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock and the outward stream. Complexity: uMorph
// adds up to 4 arms (continuous) and, past 0.5, a nest level and an octave.
// Chaos: uTurbulence/uWarp raise warp strength and jitter the wedge; uTwist
// twists the plane. Kaleido: when uKaleido is on, uSymmetry replaces the 8-arm
// rest count (the user's fold is this style's fold, never applied twice).
// Color: pal() picks every stone. Energy: uIntensity scales the hat-driven
// arms and the uDrift spin. Space: uZoom/uRotation through lucidView.

vec2 wedge(vec2 uv, float segs) {
    float lo = 2.0 * floor(segs * 0.5);
    float f = (segs - lo) * 0.5;
    f = f * f * (3.0 - 2.0 * f);
    return mix(lucidFold(uv, lo), lucidFold(uv, lo + 2.0), f);
}

void main() {
    vec2 uv = lucidView(false);
    float t = uTime * uSpeed;
    float flow = uFlowPhase * uSpeed;
    float hat = clamp(uHits.z, 0.0, 1.0) * uIntensity;
    float mid = clamp(uMidSmooth, 0.0, 1.5);
    float swell = clamp(uSwell, 0.0, 1.5);

    int depth = lucidDetail(4, 3);
    int oct = lucidDetail(3, 2);

    uv = lucidRot(uDrift * 0.35 * uIntensity + t * 0.02) * uv;
    float rest = (uKaleido > 0.5 && uSymmetry >= 2.0) ? uSymmetry : 8.0;
    float segs = rest + 4.0 * hat + 4.0 * clamp(uMorph, 0.0, 1.0);
    vec2 p = wedge(uv, segs);
    float r = length(uv);

    // Stream outward: in the wedge frame +x is the radius.
    vec2 q = p * 2.2 - vec2(flow * 0.35 + t * 0.05, 0.0);
    q += lucidJitter(q, t);
    vec4 n = lucidNest(q, depth, 1.6 * lucidChaos(), t * 0.25, LUCID_SIMPLEX, oct);

    float hue = 0.5 + 0.5 * n.z;
    float ring = 0.5 + 0.5 * cos(r * 9.0 - flow * 0.8 + n.x * 2.0);
    vec3 stone = pal(hue * 0.8 + 0.15 * ring);
    vec3 col = stone * (0.25 + 0.95 * smoothstep(0.15, 0.95, hue));
    // Facet glints along the creases of the fold.
    col += pal(hue + 0.5) * smoothstep(0.55, 1.1, n.w) * 0.5;
    col = lucidChannelTurn(col, r * 1.4 - t * 0.03);
    col = lucidCubic(col, 0.55 + 0.3 * clamp(mid, 0.0, 1.0));
    // Dark heart so the centre singularity of the fold never reads as a seam.
    col *= smoothstep(0.0, 0.12, r);

    col *= 0.85 + 0.12 * swell;
    fragColor = vec4(grade(col), 1.0);
}
