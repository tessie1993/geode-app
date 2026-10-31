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

// Truchet Tubes: the quarter-arc paths as glossy pipes lying on a floor, lit
// like objects - and not a single ray marched. Across a tube, the distance to
// its centreline IS the shape: u = d / radius runs -1..1, the tube's height
// over the floor is sqrt(1 - u^2), and the normal is the arc's own radial
// direction tipped up by that height. That is exact for a pipe of circular
// section bent along the arc, so diffuse, a tight specular and a rim all come
// out right for the cost of one tile lookup. A second lookup, offset away from
// the light, casts each pipe's shadow on the floor (which keeps the faint
// two-tone of the tiling).
//
// ---- audio ------------------------------------------------------------------
//
// Onsets swing a scattered third of the tiles a quarter turn, so pipes
// unplug and re-plug into new circuits on the hits. The bass (uBassSmooth)
// swells the pipes' radius and breathes the scale. The light circles slowly
// overhead on its own; the treble (uTrebleSmooth) tightens the highlight.
// Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the light's circling and the hue drift. Complexity:
// uMorph shrinks the tiles. Chaos: uWarp/uTurbulence bend the plane before
// tiling, so the pipes meander; uTwist in view(). Energy: uIntensity scales
// the bass swell. Colour: pipes and floor are pal(). Space: view().

void main() {
    vec2 uv = view();
    float t = truchetTime();
    float e = truchetEnergy();
    float swell = clamp(uSwell, 0.0, 1.5);
    float bass = clamp(uBassSmooth, 0.0, 1.5);
    float treble = clamp(uTrebleSmooth, 0.0, 1.2);
    float hits = floor(uHitCount);

    float scale = (2.8 + 1.8 * truchetMorph()) * truchetBreath(bass);
    vec2 p = truchetWarp(uv * scale, t);
    float px = truchetPx(p);

    vec3 light = normalize(vec3(0.55 * cos(t * 0.17), 0.55 * sin(t * 0.17), 0.85));
    TruchetArc a = truchetSquare(p, hits, uHits.w, 3.0);
    // The shadow: where the floor point under the pixel, seen from the light,
    // falls behind a pipe.
    TruchetArc sh = truchetSquare(p - light.xy * 0.16, hits, uHits.w, 3.0);

    float rad = 0.15 + 0.035 * bass * e;
    float hue = 0.04 * sin(t * 0.02) + 0.1 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue);
    float field = truchetField(p, t);

    // Floor.
    float tone = truchetTone(a, px);
    vec3 col = mix(pal(hue + 0.7) * 0.05, pal(hue + 0.78) * 0.1, tone);
    col *= 0.35 + 0.65 * smoothstep(rad * 0.7, rad * 1.25, abs(sh.d));
    col *= 0.6 + 0.4 * smoothstep(rad, rad + 0.12, abs(a.d));

    // Pipe.
    float cover = truchetLine(a.d, rad, px);
    float u = clamp(a.d / rad, -1.0, 1.0);
    float h = sqrt(max(1.0 - u * u, 0.0));
    vec3 n = normalize(vec3(a.n * u, h));
    float dif = max(dot(n, light), 0.0);
    float spec = pow(max(dot(reflect(-light, n), vec3(0.0, 0.0, 1.0)), 0.0), 28.0 + 40.0 * treble);
    float rim = pow(1.0 - h, 3.0);
    vec3 base = pal(hue + 0.1 + 0.2 * field);
    vec3 pipe = base * (0.12 + 0.88 * dif) + vec3(1.0, 0.97, 0.92) * spec * 0.55 + pal(hue + 0.5) * rim * 0.25;
    col = mix(col, pipe, cover);

    col *= 0.85 + 0.12 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
