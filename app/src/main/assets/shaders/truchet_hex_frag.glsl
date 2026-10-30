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
//#include lib_truchet

// Truchet Hex: the honeycomb version. Each hexagon carries three 120-degree
// arcs round alternate corners, each joining the midpoints of the two edges
// at its corner; flip a hexagon a sixth of a turn and it takes the other three
// corners. The arcs are drawn as thick, rounded ribbons with banded stripes
// running along them, over a dim two-tone ground (the hexes' caps against
// their middles), so the paths read as braided cord laid on tiles.
//
// ---- audio ------------------------------------------------------------------
//
// Onsets turn a scattered share of the hexes a sixth of a turn (truchetHex),
// eased over the onset envelope, so the cords re-route on the hit. The mids
// (uMidSmooth) thicken the ribbons; the stripes slide along the cords on
// uFlowPhase (loudness sets the rate, never the direction - and the flow
// direction follows the honeycomb's vertex classes, so it is continuous from
// hex to hex). The bass breathes the scale. Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the stripe flow and hue drift. Complexity: uMorph packs
// more hexes in and adds stripes. Chaos: uWarp/uTurbulence bend the plane
// before tiling; uTwist in view(). Energy: uIntensity scales the mid
// thickening and the bass breathing. Colour: ribbons, ground and stripes are
// pal(). Space: view().

void main() {
    vec2 uv = view();
    float t = truchetTime();
    float e = truchetEnergy();
    float swell = clamp(uSwell, 0.0, 1.5);
    float mids = clamp(uMidSmooth, 0.0, 1.2);
    float morph = truchetMorph();

    float scale = (2.4 + 1.4 * morph) * truchetBreath(uBassSmooth);
    vec2 p = truchetWarp(uv * scale, t);
    float px = truchetPx(p);

    TruchetArc a = truchetHex(p, floor(uHitCount), uHits.w, 3.0);

    float hue = 0.04 * sin(t * 0.025) + 0.1 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue);
    float field = truchetField(p, t);

    // Ground: the two-colouring, kept dim so the cords carry the picture.
    float tone = truchetTone(a, px);
    vec3 col = mix(pal(hue + 0.62) * 0.06, pal(hue + 0.75) * 0.14, tone);

    // Ribbon: rounded across (u), striped along (s; a whole number of
    // stripes per arc so the stripes run on unbroken into the next hex).
    float hw = 0.075 + 0.03 * mids * e;
    float u = clamp(a.d / hw, -1.0, 1.0);
    float body = truchetLine(a.d, hw, px);
    float crown = sqrt(1.0 - u * u);
    float stripes = 2.0 + floor(morph * 2.0 + 0.5);
    float band = 0.5 + 0.5 * cos(TRUCHET_TAU * (a.s * stripes - uFlowPhase * uSpeed * 0.35));
    vec3 cord = mix(pal(hue + 0.12 + 0.12 * field), pal(hue + 0.32 + 0.12 * field), smoothstep(0.35, 0.65, band));
    vec3 lit = cord * (0.25 + 0.75 * crown) + vec3(1.0) * pow(crown, 12.0) * 0.18;
    // Dark lip at the ribbon edge, so neighbouring cords separate.
    lit *= smoothstep(1.0, 0.75, abs(u));
    // Ground shadow hugging the ribbon.
    col *= 0.55 + 0.45 * smoothstep(hw, hw + 0.08, abs(a.d));
    col = mix(col, lit, body);

    col *= 0.85 + 0.12 * swell;
    col *= 0.78 + 0.22 * smoothstep(2.2, 0.3, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
