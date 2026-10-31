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

// Truchet Weave: two Truchet tilings laid one over the other and woven. The
// second layer's grid is shifted half a cell sideways, which puts its arcs'
// crossings of the first layer's cell borders only at that layer's corners -
// far from where the first layer's ribbons cross - so every crossing of two
// ribbons happens INSIDE one cell of the first layer. That cell's checkerboard
// parity decides which ribbon goes over, so the depth order never changes in
// the middle of a crossing and a ribbon passes alternately over and under as
// it runs through the grid. The under ribbon is shaded where the over ribbon
// shadows it; both are rounded across and threaded along their length.
//
// ---- audio ------------------------------------------------------------------
//
// Onsets re-roll a scattered quarter of each layer's tiles, the two layers on
// different cells (each has its own ids), so the cloth re-weaves itself
// patch by patch. The kick (uHits.x) widens the ribbons for a moment - the
// weave tightens on the drum - and the bass breathes the scale. Brightness
// follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the drift of the cloth and its hue. Complexity: uMorph
// shrinks the cells and thins the threads. Chaos: uWarp/uTurbulence bend the
// cloth before tiling; uTwist in view(). Energy: uIntensity scales the kick
// tightening and the bass breathing. Colour: warp and weft are two pal()
// tones half the wheel apart. Space: view().

/** A ribbon's colour and coverage: rounded across, threaded along. */
vec4 ribbon(TruchetArc a, float hw, float px, vec3 tint, float threads) {
    float u = clamp(a.d / hw, -1.0, 1.0);
    float cover = truchetLine(a.d, hw, px);
    float crown = sqrt(1.0 - u * u);
    float thread = 0.82 + 0.18 * cos(u * threads * TRUCHET_PI);
    vec3 c = tint * (0.3 + 0.7 * crown) * thread + vec3(0.9) * pow(crown, 16.0) * 0.12;
    c *= (1.0 - smoothstep(0.8, 1.0, abs(u)));
    return vec4(c, cover);
}

void main() {
    vec2 uv = view();
    float t = truchetTime();
    float e = truchetEnergy();
    float swell = clamp(uSwell, 0.0, 1.5);
    float kick = clamp(uHits.x, 0.0, 1.0) * e;
    float morph = truchetMorph();
    float hits = floor(uHitCount);

    float scale = (2.6 + 1.4 * morph) * truchetBreath(uBassSmooth);
    vec2 p = truchetWarp(uv * scale + vec2(t * 0.04, 0.0), t);
    float px = truchetPx(p);

    // Layer A on the plain grid; layer B shifted half a cell in x, with its
    // ids moved far off so its tiles do not echo A's.
    vec2 idA = floor(p);
    TruchetArc a = truchetSquareAt(idA, fract(p) - 0.5, hits, uHits.w, 4.0);
    vec2 pb = p + vec2(0.5, 0.0);
    TruchetArc b = truchetSquareAt(floor(pb) + vec2(131.0, 518.0), fract(pb) - 0.5, hits, uHits.w, 4.0);
    float aOver = 1.0 - mod(idA.x + idA.y, 2.0);

    float hue = 0.04 * sin(t * 0.02) + 0.1 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue);
    float field = 0.08 * truchetField(p, t);
    float hw = 0.12 + 0.025 * kick;
    float threads = 7.0 + 4.0 * morph;
    vec4 ra = ribbon(a, hw, px, pal(hue + 0.08 + field), threads);
    vec4 rb = ribbon(b, hw, px, pal(hue + 0.58 + field), threads);

    vec4 over = aOver > 0.5 ? ra : rb;
    vec4 under = aOver > 0.5 ? rb : ra;
    float dOver = aOver > 0.5 ? a.d : b.d;

    // Cloth behind both: dark, faintly cross-hatched by the two layers' bands.
    vec3 col = pal(hue + 0.82) * (0.035 + 0.025 * truchetTone(a, px) + 0.02 * truchetTone(b, px));
    float shadowAll = smoothstep(hw, hw + 0.1, abs(a.d)) * smoothstep(hw, hw + 0.1, abs(b.d));
    col *= 0.5 + 0.5 * shadowAll;
    col = mix(col, under.rgb, under.a);
    // The over ribbon shades the under one where it crosses.
    float shade = smoothstep(hw, hw + 0.07, abs(dOver));
    col *= 1.0 - 0.55 * shade * under.a;
    col = mix(col, over.rgb, over.a);

    col *= 0.85 + 0.12 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
