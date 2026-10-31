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

// Truchet Arcs: Smith's tiling exactly as he drew it, and the family's free
// style. Every square holds two quarter circles on one of its diagonals; the
// arcs join across every edge into endless meandering paths, and the plane is
// filled in the two colours those paths divide it into - a warm tone in one,
// a deep one in the other, with a thin dark seam drawn along every arc. One
// tile lookup per pixel, no loops: the cheapest Truchet there is.
//
// ---- audio ------------------------------------------------------------------
//
// Onsets REWIRE the maze: each hit re-rolls a scattered third of the tiles
// (truchetRoll), and each one turns a quarter turn to its new diagonal over
// the onset's envelope while its two colours trade places, so paths break and
// re-join on the beat without a single frame snapping. The bass (sndShaped(SND_LOW))
// breathes the tile scale; the key (uKeyHue, weighted by uKeyStrength) drifts
// the palette. Brightness follows (0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)) only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the slow scroll and the hue drift. Complexity: uMorph
// shrinks the tiles (up to 5/3 as many across). Chaos: uWarp/uTurbulence bend
// the plane before it is tiled (truchetWarp); uTwist twists it in view().
// Energy: uIntensity scales the bass breathing. Colour: both tones are pal().
// Space: uZoom/uRotation/drift through view().

void main() {
    vec2 uv = view();
    float t = truchetTime();
    float swell = clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    float hits = floor(SND_HIT_TIME);

    float scale = (3.0 + 2.0 * truchetMorph()) * truchetBreath(sndShaped(SND_LOW));
    vec2 p = truchetWarp(uv * scale + vec2(0.0, t * 0.06), t);
    float px = truchetPx(p);

    TruchetArc a = truchetSquare(p, hits, SND_ONSET_ENV, 3.0);
    float tone = truchetTone(a, px);

    float hue = 0.05 * sin(t * 0.03) + 0.12 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue);
    vec3 warm = pal(hue + 0.08) * 0.85;
    vec3 deep = pal(hue + 0.58) * 0.28;
    vec3 col = mix(deep, warm, tone);
    // A little relief toward each arc, then the seam on the arc itself.
    col *= 0.8 + 0.2 * smoothstep(0.0, 0.22, abs(a.d));
    col *= 1.0 - 0.7 * truchetLine(a.d, 0.03, px);

    col *= 0.85 + 0.12 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
