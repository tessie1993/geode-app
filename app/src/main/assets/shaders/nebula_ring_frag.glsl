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
//#include lib_nebula

// Nebula Ring: a planetary nebula - the shell a dying star blew off - seen
// down its axis, so it reads as a ring. A hot white dwarf burns at the
// centre; a glowing annulus surrounds it, ragged at both edges; the cavity
// inside is filled with a faint blue-green haze; a dim red halo trails
// outside; and along the inner edge the shell breaks into small dark
// cometary knots.
//
// The ring is layered by ionisation, as the real ones are: the gas nearest
// the star glows in one palette hue, the outer rim in another, and the
// colour runs between them across the ring's width. The ragged edge is noise
// read on the unit circle of DIRECTIONS (p / |p|), not on an angle, so there
// is no atan anywhere and nothing to seam.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (uBassSmooth with the bass band region) BREATHES THE RING: its width
// swells on the low end and slims in quiet passages, the coloured layers
// stretching with it. The hats twinkle the field stars. The key turns both
// ring hues. Overall level follows uSwell gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the slow churn of the edge and the twinkle.
// Complexity: uMorph adds octaves to the shell's texture (nebOctaves). Chaos:
// uWarp/uTurbulence swirl the shell through nebWarp2; uTwist via view().
// Colour: both ionisation hues and the haze are pal(). Energy: uIntensity
// scales the bass breathing. Space: view().

void main() {
    vec2 uv = view();
    float t = nebTime();
    float e = nebEnergy();
    float bass = clamp(0.6 * uBassSmooth + 0.4 * uBandsLo.y, 0.0, 1.3) * e;
    float swell = clamp(uSwell, 0.0, 1.5);
    float hats = clamp(uHits.z, 0.0, 1.0);
    float oct = nebOctaves(4.0);
    float key = nebKeyShift(0.1);

    vec2 p = nebWarp2(uv, t);
    float r = length(p);
    vec2 dir = p / max(r, 1e-4);

    // The turbulent radius: coarse lobes from the direction, fine fray from
    // the position.
    float lobes = nebFbm2(dir * 2.2 + vec2(t * 0.012, -t * 0.009), 3.0);
    float fine = nebFbm2(p * 6.0 + vec2(0.0, t * 0.02), oct);
    float rr = 0.55 + (lobes - 0.5) * 0.18 + (fine - 0.5) * 0.06;
    float w = 0.11 * (1.0 + 0.55 * bass);
    float x = (r - rr) / w;

    float shell = exp(-x * x * 1.6) * (0.55 + 0.9 * fine);
    vec3 inner = nebEmission(0.5 + key, 0.2);
    vec3 outer = nebEmission(0.0 + key, 0.1);
    vec3 col = mix(inner, outer, smoothstep(-1.0, 1.2, x)) * shell * 0.95;

    // The cavity's haze and the outer halo.
    col += inner * 0.12 * (1.0 - smoothstep(0.0, rr, r)) * (0.6 + 0.4 * fine);
    col += outer * 0.08 * exp(-max(r - rr, 0.0) * 3.0) * smoothstep(rr - w, rr + w, r) * fine;

    // Cometary knots: small dark heads along the inner edge.
    float knot = smoothstep(0.72, 0.9, nebValue2(p * 18.0)) * smoothstep(-1.6, -0.5, x) * (1.0 - smoothstep(-0.4, 0.3, x));
    col *= 1.0 - 0.65 * knot;

    // The white dwarf.
    col += vec3(0.82, 0.9, 1.0) * (exp(-r * r * 900.0) * 1.3 + exp(-r * 16.0) * 0.16);

    col += nebStars(uv, 8.0, 2, vec2(0.0), 1.0, 0.2, hats, t) * (1.0 - 0.6 * min(shell, 1.0));
    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
