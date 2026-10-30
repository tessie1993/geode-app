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
//#include lib_touch
//#include lib_lucid

// Lucid Weave: the family's plain form, and its free style. One open plane of
// value noise read through itself three levels deep, so the field folds into
// slow, marbled ribbons that slide over one another. The three channels of
// the nest (the two warp offsets and the surface read) become the three
// colour channels; their order turns slowly across the frame and over time,
// and a cubic contrast curve crushes the mids so only the crests glow.
//
// It is deliberately the cheapest Lucid: no mapping, three levels, three
// octaves (two and two on a low Detail budget). What it has to do is hold
// attention, and a three-level nest already does that.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (uBassSmooth, slew-limited) is the warp STRENGTH: how hard each level
// pushes the one above it. Quiet passages unknot into soft ribbons; a heavy
// low end ties them into tight marbled knots. Strength is a smooth geometric
// parameter, so the field kneads rather than jumps. Travel speed rides
// uFlowPhase (loudness sets the rate, never the direction). Overall level
// follows uSwell gently. A finger pushes the weave aside.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock and the flow. Complexity: uMorph >= 0.5 adds
// a nest level and an octave. Chaos: uTurbulence/uWarp raise warp strength
// (lucidChaos) and jitter the domain; uTwist twists it (lucidView); uKaleido/
// uSymmetry fold it. Color: pal() tints every channel. Energy: uIntensity
// scales the bass push. Space: uZoom/uRotation through lucidView.

void main() {
    vec2 uv = lucidView(true);
    float t = uTime * uSpeed;
    float flow = uFlowPhase * uSpeed;
    float bass = clamp(uBassSmooth, 0.0, 1.5) * uIntensity;
    float swell = clamp(uSwell, 0.0, 1.5);

    int depth = lucidDetail(3, 2);
    int oct = lucidDetail(3, 2);
    float strength = (1.0 + 1.25 * bass) * lucidChaos();

    vec2 p = uv * 1.5;
    p += (p - touchAnchor()) * 0.6 * touchFalloff(uv, 0.45);
    p += lucidJitter(p, t);

    vec4 n = lucidNest(p, depth, strength, t * 0.30 + flow * 0.22, LUCID_VALUE, oct);

    vec3 raw = 0.5 + 0.5 * vec3(n.z, n.x, n.y);
    vec3 tint = pal(raw.x * 0.65 + 0.2 * n.w);
    vec3 col = raw * (0.45 + 0.55 * tint);
    col = lucidChannelTurn(col, t * 0.02 + 0.9 * n.w + 0.25 * uv.x);
    col = lucidCubic(col, 0.8);
    col = lucidCube(col * 1.3);

    col *= 0.85 + 0.12 * swell;
    fragColor = vec4(grade(col), 1.0);
}
