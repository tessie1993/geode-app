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

// Truchet Tri: a triangle-grid Truchet set in stained glass. Every triangle's
// edges are crossed at the same two points, and each tile pairs its six points
// with arcs round its corners - three small ones, or one corner's nested pair
// with a half circle on the facing edge - so the lead lines run on unbroken
// into whatever the neighbour holds and close into rounded panes of every
// size. The panes take the curves' two-colouring (two palette tones half the
// wheel apart), drifting in hue across the window, lit from behind by a slow
// moving light, and each pane is brightest at its middle and dims into the
// lead like real glass.
//
// ---- audio ------------------------------------------------------------------
//
// Onsets re-roll a scattered third of the triangles, cross-fading the old lead
// into the new over the onset envelope, so panes split and merge on the hits.
// A section change (uEvents.z, uSectionCount) turns the whole window's
// colours to a new place on the palette, eased across the section envelope.
// The key (uKeyHue, weighted by uKeyStrength) leans the hue; the bass breathes
// the scale. The back light follows uSwell gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the moving back light and the hue drift. Complexity:
// uMorph packs in smaller triangles. Chaos: uWarp/uTurbulence bend the window
// before it is tiled; uTwist in view(). Energy: uIntensity scales the bass
// breathing. Colour: every pane is pal(). Space: view().

/** x = lead coverage, y = glass colour (the two-colouring), z = distance to the lead. */
vec3 glass(TruchetTri tri, vec2 p, float variant, float px) {
    vec3 r = truchetTriTile(tri, p, variant);
    return vec3(truchetLine(r.x, 0.045, px), r.y, r.x);
}

void main() {
    vec2 uv = view();
    float t = truchetTime();
    float swell = clamp(uSwell, 0.0, 1.5);
    float morph = truchetMorph();

    float scale = (2.2 + 1.3 * morph) * truchetBreath(uBassSmooth);
    vec2 p = truchetWarp(uv * scale, t);
    float px = truchetPx(p);

    TruchetTri tri = truchetTriCell(p);
    vec2 rid = vec2(tri.id.x * 2.0 + tri.id.z, tri.id.y);
    vec3 roll = truchetRoll(rid, floor(uHitCount), uHits.w, 3.0);
    float vOld = truchetTriVariant(roll.x);
    float vNew = truchetTriVariant(roll.y);
    vec3 g = glass(tri, p, vNew, px);
    if (vOld != vNew && roll.z < 1.0) {
        g = mix(glass(tri, p, vOld, px), g, roll.z);
    }
    // Distance to the lead is continuous across triangle edges (every tile
    // crosses an edge at the same two points), so the pane shading below
    // never draws the grid.
    float lead = g.z;

    // Section colour: a new place on the wheel each section, slid into.
    float se = clamp(uEvents.z, 0.0, 1.0);
    se = se * se * (3.0 - 2.0 * se);
    float sec = mix(truchetHash(vec2(uSectionCount, 7.0)), truchetHash(vec2(uSectionCount - 1.0, 7.0)), se);

    float field = truchetField(p * 1.3, t);
    float hue = 0.3 * sec + 0.12 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue) + 0.03 * sin(t * 0.02);
    vec3 paneA = pal(hue + 0.05 + 0.16 * field);
    vec3 paneB = pal(hue + 0.52 + 0.16 * field);
    vec3 pane = mix(paneA, paneB, g.y);

    // Glass: lit from behind by a slow-moving soft light, brightest mid-pane.
    vec2 lightPos = vec2(sin(t * 0.13) * 1.1, cos(t * 0.11) * 0.6);
    float back = 0.45 + 0.55 * exp(-dot(uv - lightPos, uv - lightPos) * 0.6);
    float body = 0.55 + 0.45 * smoothstep(0.03, 0.2, lead);
    vec3 col = pane * back * body * (0.8 + 0.15 * swell);
    // Lead came: dark grey with a faint highlight.
    vec3 leadC = vec3(0.05) + pal(hue + 0.3) * 0.03;
    col = mix(col, leadC, g.x);

    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
