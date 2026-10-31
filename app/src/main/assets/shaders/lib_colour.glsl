// The colour engine's frame (core/viz/colour, docs/COLOUR_ENGINE.md): the one way the music's colour
// reaches a shader. lib_palette includes this file, so a style that includes lib_palette must not
// include it again; the composite pass (which carries MilkDrop and the simulation scenes) includes it
// directly.
//
// Every frame the native colour engine hears the music and hands every scene the same few values; the
// functions below are the only place a style meets them, so all ~150 styles follow the music the same
// way and nothing per-style can drift. uColourOn = 0 (Follow at 0, or a program the engine never
// uploaded to) leaves pal() exactly as it was before the engine existed.
//
// - uColourHue / uColourClash: the style's OWN palette turned round the grey axis, luminance kept:
//   the key's place on the circle of fifths plus a major/minor tint, and that turned further toward
//   the complement, blended in on two soft bands by uColourClashAmt (harmonic tension).
// - uColourRamp: the music's palette (a curated multi-stop palette, crossfaded in OKLab on section
//   changes, already turned and tinted on the CPU), 16 display-sRGB entries.
// - uColourAway: how far pal() leans from the style's palette to the ramp. Home is the style.
// - uColourDrift: the hue driver (melody note, chord, timbre) moving the read position along the ramp.
uniform float uColourOn;
uniform vec3 uColourRamp[16];
uniform float uColourAway;
uniform float uColourDrift;
uniform mat3 uColourHue;
uniform mat3 uColourClash;
uniform float uColourClashAmt;
/** grade(): the highlight colour and how strongly the brightest pixels take it. */
uniform vec4 uColourHighlight;
/** grade(): x tension's shadow depth, y contrast offset, z saturation offset (energy). */
uniform vec3 uColourGrade;

/**
 * The music's palette at t. Read with a mirror (0 → 1 → 0 over each unit of t), because pal() has
 * always been periodic in t: styles wrap t with fract() freely, and a curated dark-to-light ramp read
 * with a plain fract() would show a hard seam where its lightest stop met its darkest.
 */
vec3 colourRamp(float t) {
    float u = 1.0 - abs(1.0 - 2.0 * fract(t));
    float x = u * 15.0;
    int i = int(min(floor(x), 14.0));
    return mix(uColourRamp[i], uColourRamp[i + 1], x - float(i));
}

/**
 * Any colour a style or family makes itself (its own cosine palette, a spectrum, a film) moved by the
 * music the way pal() moves the palette: turned with the key, clashed with tension, and leaned toward
 * the music's ramp at t. Family libraries call this on colours that do not come from pal().
 */
vec3 colourMusic(vec3 c, float t) {
    if (uColourOn < 0.5) return c;
    float band = 0.5 + 0.5 * sin(6.2831853 * 2.0 * t);
    vec3 turned = mix(uColourHue * c, uColourClash * c, uColourClashAmt * band);
    return mix(max(turned, 0.0), colourRamp(t + uColourDrift), uColourAway);
}

/**
 * The colour engine's last word on a finished colour, shared by grade() and the composite pass: energy
 * saturates and widens the contrast, tension deepens the shadows (the storm half of "both"; the
 * complementary clash is in colourMusic), and the highlight colour (timbre and melody, brighter on hits)
 * tints only the brightest pixels.
 */
vec3 colourFinish(vec3 col) {
    if (uColourOn < 0.5) return col;
    float g = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(vec3(g), col, 1.0 + uColourGrade.z);
    col = (col - 0.5) * (1.0 + uColourGrade.y) + 0.5;
    float l = dot(max(col, 0.0), vec3(0.299, 0.587, 0.114));
    col *= 1.0 - 0.6 * uColourGrade.x * (1.0 - smoothstep(0.08, 0.55, l));
    return mix(col, uColourHighlight.rgb * l * 1.15, uColourHighlight.a * smoothstep(0.55, 0.95, l));
}
