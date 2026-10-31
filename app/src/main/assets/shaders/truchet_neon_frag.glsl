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

// Truchet Neon: thin neon tubing on black. Each quarter arc is drawn as three
// concentric lanes (radius 1/3, 1/2 and 2/3 about its corner), so each tile
// carries six strands and every edge is crossed three times. Across an edge
// the middle lane meets a middle lane, and the 1/3 lane meets the 2/3 lane
// of the neighbour, so the lanes fall into two families of paths that never
// touch: the middle lanes glow in one hue, the outer pair in another, and
// both hues drift slowly across the frame. Each line is a hot white-ish core
// with a two-scale bloom halo of its family's colour.
//
// ---- audio ------------------------------------------------------------------
//
// Onsets swing a scattered third of the tiles a quarter turn, so the tubing
// re-routes on the hit. The mids (sndShaped(SND_MIDDLE)) widen the bloom, the bass
// breathes the scale, the key leans both hues. Brightness follows SND_LEVEL
// only; nothing flashes on a hit.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the hue drift. Complexity: uMorph shrinks the tiles.
// Chaos: uWarp/uTurbulence bend the plane before tiling; uTwist in view().
// Energy: uIntensity scales the mid bloom and the bass breathing. Colour:
// both families are pal(), pushed to full saturation. Space: view().

/** Push a palette colour to full saturation: gas in glass, not pastel. */
vec3 neonize(vec3 c) {
    float lo = min(c.r, min(c.g, c.b));
    float hi = max(c.r, max(c.g, c.b));
    return clamp((c - lo) / max(hi - lo, 1e-3), 0.0, 1.0);
}

void main() {
    vec2 uv = view();
    float t = truchetTime();
    float e = truchetEnergy();
    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    float mids = clamp(sndShaped(SND_MIDDLE), 0.0, 1.2) * e;

    float scale = (2.2 + 1.4 * truchetMorph()) * truchetBreath(SND_BASS_PRESENCE);
    vec2 p = truchetWarp(uv * scale, t);
    float px = truchetPx(p);

    TruchetArc a = truchetSquare(p, floor(SND_ONSET_COUNT), SND_ONSET_ENV, 3.0);
    // Distance from the nearest corner; the lanes are rings about it.
    float l = a.d + 0.5;
    float dMid = abs(l - 0.5);
    float dOut = min(abs(l - 1.0 / 3.0), abs(l - 2.0 / 3.0));

    float hue = 0.05 * sin(t * 0.017) + 0.12 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue);
    float field = 0.12 * truchetField(p, t);
    vec3 cMid = neonize(pal(hue + 0.15 + field));
    vec3 cOut = neonize(pal(hue + 0.62 + field));

    float k1 = 70.0 - 25.0 * mids;
    float k2 = 16.0 - 6.0 * mids;
    vec3 col = vec3(0.0);
    col += cMid * (exp(-dMid * k1) * 0.55 + exp(-dMid * k2) * 0.16);
    col += cOut * (exp(-dOut * k1) * 0.55 + exp(-dOut * k2) * 0.16);
    float coreMid = truchetLine(dMid, 0.012, px);
    float coreOut = truchetLine(dOut, 0.012, px);
    col += mix(cMid, vec3(1.0), 0.65) * coreMid * 0.9;
    col += mix(cOut, vec3(1.0), 0.65) * coreOut * 0.9;

    col *= 0.85 + 0.12 * swell;
    col *= 0.75 + 0.25 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
