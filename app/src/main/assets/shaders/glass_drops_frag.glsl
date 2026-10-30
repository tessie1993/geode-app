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
//#include lib_glass

// Glass Drops: a rainy window at night. Behind the misted pane the city is a
// blur of bokeh - street lamps, windows and signs as soft discs, warm sodium
// low down, the palette higher up - over a dark sky. On the pane, raindrops:
// each is a tiny ball lens, so it holds a sharp, inverted, wide-angle image of
// the city behind it, darkening toward its rim and catching a highlight at its
// crown. Where a drop has slid it has wiped the mist away, leaving a clear
// streak through which the lights are sharper. Two layers of drops at two sizes,
// plus a fine mist of droplets. Flat and analytic: every drop and every light is
// one cell lookup, nothing loops.
//
// Cells: each drop and each light sits inside its own grid cell with room to
// spare, so nothing it draws crosses a cell border; a trail fades to nothing
// before the top of its cell. The picture is continuous everywhere.
//
// ---- audio ------------------------------------------------------------------
//
// Beats slide the drops: every onset (uHitCount) moves each column of drops a
// notch down the glass, and the move eases in as the onset envelope (uHits.w)
// falls (glassGlide), so the drops run in little jerks with the rhythm, each
// column at its own pace, and creep slowly between beats. The key (uKeyHue,
// uKeyStrength) tints the city. Brightness follows uSwell.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the creep and the lights' flicker. Complexity (uMorph)
// fills the pane with more drops. Chaos (uWarp, uTurbulence) wobbles the drops'
// refraction. Energy (uIntensity) scales how far a beat slides them. Colour:
// city lights and sky from pal(). Space is the rig's flat projection.

/** One layer of bokeh discs, `cell` units apart, each sized to stay inside its cell even at full blur. */
vec3 bokeh(vec2 x, float cell, float blur, float hue, float seed, float t) {
    vec2 g = x / cell;
    vec2 id = floor(g);
    vec2 f = fract(g) - 0.5;
    vec2 h2 = glassHash2(id + seed);
    float h = glassHash(id + seed + 9.1);
    float rad = 0.12 + 0.12 * h2.x;
    vec2 off = (h2 - 0.5) * 2.0 * max(0.5 - rad - 0.15, 0.0);
    float d = length(f - off) - rad;
    float soft = 0.015 + blur;
    float disc = smoothstep(soft, -soft, d) * (0.75 + 0.25 * smoothstep(-rad, 0.0, d));
    vec3 c = mix(vec3(1.0, 0.62, 0.25), pal(hue + h2.y * 0.45), 0.55);
    float flick = 0.85 + 0.15 * sin(t * (0.6 + 2.0 * h) + h * 30.0);
    return c * disc * step(0.45, h) * flick * (0.45 + 0.55 * h);
}

/** The city behind the glass, softened by `blur` (in light-cell units, up to about 0.13). */
vec3 city(vec2 x, float blur, float hue, float t) {
    vec3 col = mix(pal(hue + 0.08) * 0.16 + vec3(0.09, 0.045, 0.02), pal(hue + 0.62) * 0.04 + vec3(0.01, 0.01, 0.03), smoothstep(-0.9, 0.7, x.y));
    col += bokeh(x, 0.26, blur, hue, 0.0, t) * smoothstep(0.9, -0.6, x.y);
    col += bokeh(x + vec2(0.13, 0.07), 0.15, blur, hue, 5.0, t) * 0.7 * smoothstep(0.6, -0.9, x.y);
    return col;
}

/**
 * One layer of drops. Returns (drop coverage, trail coverage, drop radius); `l`
 * is the pixel's offset from its drop's centre, world units.
 */
vec3 drops(vec2 x, float cell, float slide, float seed, float density, out vec2 l) {
    vec2 g = x / cell;
    float column = glassHash(vec2(floor(g.x), seed));
    g.y += slide * (0.35 + 0.65 * column) + column * 7.0;
    vec2 id = floor(g);
    vec2 f = fract(g) - 0.5;
    float h = glassHash(id + seed * 1.7);
    vec2 h2 = glassHash2(id + seed + 3.3);
    float present = step(1.0 - density, h2.y * 0.5 + h * 0.5);
    float rad = 0.12 + 0.11 * h2.x;
    vec2 c = vec2((h2.y - 0.5) * 0.3, (h - 0.5) * 0.24 - 0.06);
    vec2 lc = f - c;
    vec2 ls = vec2(lc.x * (1.0 + 0.35 * clamp(lc.y / rad, 0.0, 1.0)), lc.y * 0.9);
    float r = length(ls) / rad;
    float mask = smoothstep(1.0, 0.9, r) * present;
    float trail = smoothstep(rad * 0.55, rad * 0.15, abs(lc.x)) * step(0.0, lc.y) * smoothstep(0.5 - c.y, 0.0, lc.y) * present;
    l = ls * cell;
    return vec3(mask, trail * 0.85, rad * cell);
}

/** A drop: a ball lens holding an inverted wide view of the city, dark at the rim, lit at the crown. */
vec3 dropShade(vec2 x, vec2 l, float rad, float hue, float t, float chaos) {
    vec2 q = l / max(rad, 1e-4);
    float rr = length(q);
    vec2 centre = x - l;
    vec2 wob = chaos * 0.2 * vec2(sin(q.y * 6.0 + t), cos(q.x * 5.0 - t * 0.8));
    vec3 c = city(centre - (l + wob * rad) * 6.0, 0.02, hue, t) * 1.2;
    c *= mix(1.0, 0.3, smoothstep(0.5, 1.0, rr));
    c += pal(hue + 0.1) * 0.22 * smoothstep(0.7, 0.95, rr) * smoothstep(0.1, -0.5, q.y);
    c += vec3(1.0, 0.97, 0.92) * exp(-dot(q - vec2(-0.3, 0.38), q - vec2(-0.3, 0.38)) * 40.0) * 0.6;
    return c;
}

void main() {
    vec2 uv = view();
    float t = glassTime();
    float swell = clamp(uSwell, 0.0, 1.5);
    float hue = 0.04 * sin(t * 0.019) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue);
    float chaos = glassChaos();
    float density = 0.55 + 0.25 * glassComplexity();

    float slide = glassGlide(uHitCount, uHits.w) * 0.45 * glassEnergy() + t * 0.025;
    vec2 l1;
    vec2 l2;
    vec3 big = drops(uv, 0.34, slide, 1.0, density, l1);
    vec3 small = drops(uv + vec2(0.07, 0.0), 0.16, slide * 1.6, 7.0, density * 0.9, l2);

    // The mist: the city scattered and greyed, wiped clear where drops ran.
    vec3 fogged = city(uv, 0.12, hue, t);
    fogged = mix(fogged, vec3(dot(fogged, vec3(0.333))), 0.25) * 0.8 + pal(hue + 0.6) * 0.012 + 0.03;
    float clear = clamp(max(big.y, small.y), 0.0, 1.0);
    vec3 col = mix(fogged, city(uv, 0.03, hue, t), clear);

    // Fine droplets in the mist.
    vec2 mg = uv / 0.045;
    vec2 mf = fract(mg) - 0.5;
    vec2 mh = glassHash2(floor(mg) + 21.0);
    float mist = smoothstep(0.2, 0.12, length(mf - (mh - 0.5) * 0.4)) * step(0.55, mh.x);
    col += vec3(0.6, 0.65, 0.7) * mist * 0.05 * (1.0 - clear);

    if (small.x > 0.0) col = mix(col, dropShade(uv, l2, small.z, hue, t, chaos), small.x);
    if (big.x > 0.0) col = mix(col, dropShade(uv, l1, big.z, hue, t, chaos), big.x);

    col *= 0.85 + 0.15 * swell;
    col *= 0.8 + 0.2 * smoothstep(2.2, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
