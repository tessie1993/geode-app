#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_arbor

// Arbor Rosette: a forest seen from straight above and folded through a
// seven-way kaleidoscope into a stained-glass rose window. One ring of trees
// grows outward from a small centre, a second ring grows inward from the rim
// between them, and where their twigs meet the wedge mirrors they lace into
// tracery. Flat 2D, radial: no march.
//
// Both rings are arborTree2 in the frame of one mirror wedge (dihedral, so a
// tree crossing the wedge edge meets its own reflection). Each fold level
// takes its own jewel colour, a step along the palette, which is what turns
// a silhouette into glass: the depth record gArbLevel IS the colour index.
//
// ---- audio ------------------------------------------------------------------
//
// Pitch changes WALK THE HUES. The pitch-change envelope (uEvents.w) widens the
// hue step between fold levels: on a melodic move the window's colours fan out
// across the palette from trunk to twig and then gather back toward one hue as
// the envelope falls, so a melody visibly ripples outward through the glass.
// The key (uKeyHue, gated by uKeyStrength) sets the window's base hue. Hat
// (uHits.z) swells the bright jewels on the twig tips. Swell (uSwell) only
// lifts the backlight a little.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the window's turn. Complexity: uMorph adds a fork level
// to both rings (arbLevels2). Chaos: uTurbulence and uWarp add per-branch
// jitter, the tracery growing irregular and organic. Colour: every jewel and
// the lead from pal(). Energy: uIntensity scales the pitch walk, the key's
// pull and the hat jewels. Space: the view zoom and pan.

#define RS_FOLDS 7.0
#define RS_INNER 0.11
#define RS_OUTER 0.98
/** The window's radius on screen: it spans a portrait phone's width. */
#define RS_SCALE 0.5

vec2 wedge(vec2 p) {
    float w = ARB_TAU / RS_FOLDS;
    float a = atan(p.y, p.x);
    a = abs(mod(a + 0.5 * w, w) - 0.5 * w);
    return length(p) * vec2(cos(a), sin(a));
}

void main() {
    vec2 uv = arbView();
    float px = max(length(fwidth(uv)), 1e-4);
    float t = uTime * uSpeed;
    float energy = uIntensity;
    float chaos = arbChaos();
    float swell = clamp(uSwell, 0.0, 1.5);
    float pitch = clamp(uEvents.w * energy, 0.0, 1.0);
    float hat = clamp(uHits.z * energy, 0.0, 1.0);
    float key = uKeyHue * clamp(uKeyStrength * energy, 0.0, 1.0);

    float hue = key + 0.01 * t;
    float stepHue = 0.05 + 0.16 * pitch;

    vec2 p = rot2(t * 0.04) * uv / RS_SCALE;
    px /= RS_SCALE;
    float r = length(p);
    float jitter = 0.08 + 0.45 * chaos;

    // Outward ring.
    vec2 q = wedge(p);
    ArborForm2 fo = ArborForm2(vec2(0.44, 0.44), vec2(0.70, 0.70), 0.0, jitter, 0.19, 0.10, 0.72);
    float dOut = arborTree2(vec2(q.y, q.x - RS_INNER), fo, arbLevels2(8.0, 1.0), 1.0);
    float lOut = gArbLevel;
    float tOut = gArbTip;

    // Inward ring, half a wedge round.
    vec2 q2 = wedge(rot2(ARB_TAU / RS_FOLDS * 0.5) * p);
    ArborForm2 fi = ArborForm2(vec2(0.58, 0.58), vec2(0.66, 0.66), 0.0, jitter, 0.17, 0.09, 0.70);
    float dIn = arborTree2(vec2(q2.y, RS_OUTER - q2.x), fi, arbLevels2(7.0, 1.0), 2.0);
    float lIn = gArbLevel;
    float tIn = gArbTip;

    // Backlight: a dark window with a glow at its heart.
    vec3 col = pal(hue + 0.5) * (0.03 + 0.05 * smoothstep(1.2, 0.0, r)) * (0.9 + 0.2 * swell);
    col += pal(hue + 0.55) * 0.05 * (1.0 - smoothstep(0.0, 0.004, abs(r - RS_OUTER - 0.03)));

    // Glass: each level its own colour, lit brighter at the limb's centre.
    vec3 gOut = pal(hue + lOut * stepHue) * (0.55 + 0.45 * smoothstep(0.0, -0.012, dOut));
    vec3 gIn = pal(hue + 0.33 + lIn * stepHue) * (0.55 + 0.45 * smoothstep(0.0, -0.012, dIn));
    col = mix(col, gIn, arbInk(dIn, px));
    col = mix(col, gOut, arbInk(dOut, px));
    // Lead came: a dark line round every piece.
    float lead = 1.0 - smoothstep(0.0, 1.6 * px, abs(min(dOut, dIn)));
    col = mix(col, vec3(0.01), lead * 0.85);
    // Halo of light through the glass.
    col += pal(hue + 0.1) * exp(-max(min(dOut, dIn), 0.0) * 40.0) * 0.12;

    // Tip jewels, swollen by the hat.
    float jr = 0.005 + 0.008 * hat;
    float jewel = arbInk(min(tOut, tIn) - jr, px);
    col = mix(col, pal(hue + 0.5 + stepHue * 4.0) * 1.2, jewel);

    // The hub.
    float hub = r - RS_INNER * 0.9;
    col = mix(col, pal(hue) * (0.7 + 0.3 * smoothstep(0.0, -0.08, hub)), arbInk(hub, px));
    col = mix(col, vec3(0.01), (1.0 - smoothstep(0.0, 1.6 * px, abs(hub))) * 0.85);

    col *= 0.8 + 0.2 * smoothstep(2.0, 0.3, r);
    fragColor = vec4(grade(col), 1.0);
}
