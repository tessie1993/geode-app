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

// Truchet Knot: Celtic interlace grown from a Truchet tiling. The grid is set
// at 45 degrees, so its straight runs read as the diagonals knotwork is drawn
// on. Each cell holds either a CROSSING - two straight strands, one through
// each pair of opposite edge midpoints - or one of the two quarter-arc tiles.
// Every strand leaves every tile square to the edge at its midpoint, so the
// cords run on smoothly whatever the neighbours hold.
//
// WHY THE OVER-UNDER ALWAYS ALTERNATES. At a crossing, the cell's checkerboard
// parity picks which strand is on top: in even cells the one running along
// the first axis, in odd cells the other. Follow a strand from one crossing
// to the next with m arc tiles between: the parity flips m + 1 times (once
// per cell stepped into) and the strand's axis m times (once per turn), 2m + 1
// flips in all - always odd. So a strand that went over goes under at its
// next crossing, over at the one after, exactly as interlace must, with no
// planning across cells.
//
// Each strand is a doubled cord (two rounded cords with a dark groove between
// them), and the under strand is cut back on either side of the over strand,
// the gap that makes knotwork read.
//
// ---- audio ------------------------------------------------------------------
//
// Onsets re-roll a scattered third of the cells between crossing and turning,
// cross-fading the cords, so the knot re-ties itself on the hits. The snare
// (uHits.y) opens the gaps at the crossings for a moment. The bass breathes
// the scale; the key leans the hue. Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the hue drift. Complexity: uMorph shrinks the cells.
// Chaos: uWarp/uTurbulence bend the knot before tiling; uTwist in view().
// Energy: uIntensity scales the snare gaps and the bass breathing. Colour:
// cords and ground are pal(). Space: view().

/** A doubled cord about distance d: colour (premultiplied) and coverage. */
vec4 cord(float d, float hw, float px, vec3 tint) {
    float ad = abs(d);
    float cover = truchetLine(d, hw, px);
    float cu = clamp(ad / hw * 2.0 - 1.0, -1.0, 1.0);
    float crown = sqrt(1.0 - cu * cu);
    float groove = smoothstep(0.012, 0.03 + px, ad);
    vec3 c = tint * (0.22 + 0.78 * crown) * (0.3 + 0.7 * groove);
    c *= (1.0 - smoothstep(0.86, 1.0, ad / hw));
    return vec4(c * cover, cover);
}

/** One tile of the knot: colour (premultiplied) and coverage; near = distance to the nearest strand. */
vec4 knotTile(vec2 f, float parity, float kind, float hw, float gap, float px, vec3 tint, out float near) {
    if (kind < 0.5) {
        float dA = f.x;
        float dB = f.y;
        float dOver = parity < 0.5 ? dA : dB;
        float dUnder = parity < 0.5 ? dB : dA;
        vec4 over = cord(dOver, hw, px, tint);
        vec4 under = cord(dUnder, hw, px, tint * 0.85);
        under *= smoothstep(hw + gap - px, hw + gap + px, abs(dOver));
        near = min(abs(dA), abs(dB));
        return over + under * (1.0 - over.a);
    }
    TruchetArc a = truchetQuarter(f, parity, kind - 1.0);
    near = abs(a.d);
    return cord(a.d, hw, px, tint);
}

float knotKind(float h) {
    return h < 0.5 ? 0.0 : (h < 0.75 ? 1.0 : 2.0);
}

void main() {
    vec2 uv = view();
    float t = truchetTime();
    float e = truchetEnergy();
    float swell = clamp(uSwell, 0.0, 1.5);
    float snare = clamp(uHits.y, 0.0, 1.0) * e;

    float scale = (2.4 + 1.6 * truchetMorph()) * truchetBreath(uBassSmooth);
    vec2 p = truchetWarp(truchetRot(0.78539816) * uv * scale, t);
    float px = truchetPx(p);

    vec2 id = floor(p);
    vec2 f = fract(p) - 0.5;
    float parity = mod(id.x + id.y, 2.0);
    vec3 roll = truchetRoll(id, floor(uHitCount), uHits.w, 3.0);

    float hue = 0.04 * sin(t * 0.02) + 0.1 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue);
    vec3 tint = pal(hue + 0.1 + 0.1 * truchetField(p, t));
    float hw = 0.16;
    float gap = 0.045 + 0.03 * snare;

    float near;
    vec4 k = knotTile(f, parity, knotKind(roll.y), hw, gap, px, tint, near);
    float kOld = knotKind(roll.x);
    if (kOld != knotKind(roll.y) && roll.z < 1.0) {
        float nearOld;
        vec4 ko = knotTile(f, parity, kOld, hw, gap, px, tint, nearOld);
        k = mix(ko, k, roll.z);
        near = mix(nearOld, near, roll.z);
    }

    vec3 col = pal(hue + 0.62) * 0.07;
    col *= 0.45 + 0.55 * smoothstep(hw, hw + 0.1, near);
    col = col * (1.0 - k.a) + k.rgb;

    col *= 0.85 + 0.12 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
