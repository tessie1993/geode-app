#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_terra

// Terrain Topo: the family's mountains as a map. Not marched: the same
// ridged field as Terrain Ridge, read once per pixel from straight above and
// drawn as a topographic sheet - hypsometric tints from valley to summit,
// hill-shading from the north-west, a contour line every tenth of the
// range with every fifth one an index contour drawn heavier, a faint
// graticule, and a lake wherever the land dips under its lowest level. The
// map scrolls with the rig's travel, so it is the flight seen on the chart.
//
// The contours are lines of a value, not of the plane: fwidth() of the level
// sets their width in pixels, so they stay one weight at any zoom and over any
// slope. The hill-shade normal is a forward difference: three reads of the
// field at three octaves (four with Complexity), twelve noise loop
// iterations at most, inside the flat styles' budget.
//
// ---- audio ------------------------------------------------------------------
//
// Beats (uEvents.x) THICKEN THE ISOLINES: every contour swells on the beat
// and thins back as the envelope falls, so the whole chart pulses its ink in
// time - line weight, not brightness. Bass (uBassSmooth) raises the land a
// little, so contours crowd up the slopes and the lakes shrink on low notes.
// The key (uKeyHue, gated by uKeyStrength) drifts the tints. Brightness
// follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the Chaos drift and the tint drift. Complexity
// (uMorph) fades in an octave of ground (terraOctaves). Chaos (uWarp,
// uTurbulence, uTwist) bends the ridges (terraChaos). Energy (uIntensity)
// scales the beat's ink and the bass lift. Colour is pal() in the tints and
// the lines. Space is the zoom and pan of view(), and the rig's travel.

#define TP_SCALE 5.0
#define TP_LEVELS 10.0
#define TP_LAKE 0.16

void main() {
    vec2 uv = view();
    float swell = clamp(uSwell, 0.0, 1.5);
    float energy = terraEnergy();
    float beat = clamp(uEvents.x * energy, 0.0, 1.0);
    float lift = 0.12 * clamp(uBassSmooth, 0.0, 1.2) * energy;
    float oct = terraOctaves(3.0);
    float hue = 0.03 * sin(terraTime() * 0.02) + 0.1 * uKeyStrength * sin(TERRA_TAU * uKeyHue);

    // Map north is up the screen and the flight runs north.
    vec2 xz = terraWrap(uv * TP_SCALE + vec2(0.4 * uCamPos.x, uCamTravel));
    float k = 1.0 + lift;
    float h = terraRidged(terraChaos(xz), oct) * k;

    // Hill-shade: forward differences, a tenth of a unit, exaggerated.
    float e = 0.1;
    float hx = terraRidged(terraChaos(xz + vec2(e, 0.0)), oct) * k;
    float hz = terraRidged(terraChaos(xz + vec2(0.0, e)), oct) * k;
    vec3 n = normalize(vec3((h - hx) * 2.5, e, (h - hz) * 2.5));
    float shade = clamp(dot(n, normalize(vec3(-0.6, 0.7, 0.4))), 0.0, 1.0);

    // Hypsometric tints: green lowland, tan uplands, brown heights, pale
    // summits, each pulled a little toward the palette.
    vec3 tint = mix(vec3(0.42, 0.58, 0.38), vec3(0.80, 0.72, 0.52), smoothstep(0.25, 0.55, h));
    tint = mix(tint, vec3(0.62, 0.46, 0.34), smoothstep(0.55, 0.78, h));
    tint = mix(tint, vec3(0.94, 0.93, 0.90), smoothstep(0.8, 0.95, h));
    tint = mix(tint, pal(hue + 0.6 * h), 0.22);
    vec3 col = tint * (0.5 + 0.65 * shade);

    // Lakes below the lowest contour: flat water, its own shoreline.
    float lake = smoothstep(TP_LAKE + 0.004, TP_LAKE - 0.004, h);
    col = mix(col, mix(vec3(0.42, 0.62, 0.78), pal(hue + 0.6) * 0.7, 0.2), lake);

    // Contours: distance to the nearest level, in pixels of that level.
    float v = h * TP_LEVELS;
    float fw = max(fwidth(v), 1e-4);
    float dl = abs(fract(v - 0.5) - 0.5) / fw;
    float di = abs(fract(v / 5.0 - 0.5) - 0.5) * 5.0 / fw;
    float wid = 0.55 + 1.4 * beat;
    float line = 1.0 - smoothstep(wid, wid + 1.0, dl);
    float index = 1.0 - smoothstep(wid * 1.9, wid * 1.9 + 1.0, di);
    vec3 ink = mix(vec3(0.36, 0.22, 0.12), pal(hue + 0.05) * 0.3, 0.25);
    col = mix(col, ink, max(line * 0.55, index * 0.85) * (1.0 - lake));
    float shore = 1.0 - smoothstep(0.8, 1.8, abs(h - TP_LAKE) / max(fwidth(h), 1e-4));
    col = mix(col, vec3(0.16, 0.36, 0.58), shore * 0.8);

    // The graticule: a grid every 8 world units (divides the tile).
    vec2 g = abs(fract(xz / 8.0 - 0.5) - 0.5) * 8.0 / max(fwidth(uv) * TP_SCALE, 1e-4);
    col = mix(col, vec3(0.2, 0.3, 0.45), (1.0 - smoothstep(0.4, 1.2, min(g.x, g.y))) * 0.3);

    col *= 0.85 + 0.17 * swell;
    col *= 0.8 + 0.2 * smoothstep(2.4, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
