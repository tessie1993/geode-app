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
//#include lib_touch
//#include lib_lucid

// Lucid Vortex: a whirlpool of embers. The plane is swirled around a centre
// that wanders - the turn grows as 1/r, so the far field barely bends and the
// core is wound into a tight spiral - and the swirled plane is then advected
// through a CURL field several times over (fluidWarp from lib_scene_motion,
// one call per level). Curl noise is divergence-free, so unlike every other
// Lucid nest this one folds like a real fluid: it shears and stretches
// material into filaments but never piles it into knots. The surface read is
// value fbm, taken as heat.
//
// Coloured as molten metal: the heat runs through a blackbody ramp (black,
// deep red, orange, a white-hot core of the filaments) mixed with the
// palette, so the palette sets the metal and the heat sets how hot it glows.
//
// ---- audio ------------------------------------------------------------------
//
// Relative ENERGY (uEnergyRel: loudness against this track's 20 s average,
// attack 0.25 s / release 1 s) sets the SWIRL TIGHTNESS: a passage louder
// than the track's norm winds the whirlpool tighter; a quiet one lets it
// relax into broad arcs. Because it is relative, a quiet track swirls as
// much as a loud one. The centre rides uOrbit, the slow wander point that
// re-targets on novelty and section boundaries. A finger drags the eye of the
// vortex. Brightness follows uSwell gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock (the whirl's rotation and drift).
// Complexity: uMorph past 0.5 adds a curl level and an octave. Chaos:
// uTurbulence/uWarp raise the curl advection and jitter the core; uTwist
// twists the plane. Color: the metal is pal(), the glow the blackbody ramp.
// Energy: uIntensity scales the energy-driven tightening and the orbit's
// wander. Space: uZoom/uRotation through lucidView. The user's kaleidoscope
// folds the plane (through lucidView).

vec3 blackbody(float h) {
    h = clamp(h, 0.0, 1.0);
    return clamp(vec3(h * 2.6, h * 2.6 - 0.9, h * 3.2 - 2.2), 0.0, 1.0);
}

void main() {
    vec2 uv = lucidView(true);
    float t = uTime * uSpeed;
    float swell = clamp(uSwell, 0.0, 1.5);
    float rel = clamp(uEnergyRel - 0.6, 0.0, 1.4) * uIntensity;
    float chaos = lucidChaos();

    int depth = lucidDetail(3, 2);
    int oct = lucidDetail(3, 2);

    vec2 centre = uOrbit * 0.35 * uIntensity;
    centre = mix(centre, touchAnchor(), touchStrength());
    vec2 d = uv - centre;
    float r = length(d);

    // The core's turn is tight / 0.3 at most, so even a fast energy change
    // re-winds the eye by a fraction of a turn per second, not a spin.
    float tight = 0.45 + 0.6 * rel;
    float turn = tight / (r + 0.3) + t * 0.12;
    vec2 p = lucidRot(turn) * d * 1.8;
    p += lucidJitter(p, t);

    // The curl nest: one fluid advection per level, coarse to fine.
    for (int i = 0; i < LUCID_MAX_DEPTH; i++) {
        if (i >= depth) break;
        float lv = float(i);
        p = fluidWarp(p + vec2(0.0, t * 0.03), 0.7 + 0.45 * lv, 0.32 * chaos / (1.0 + lv));
    }
    float heat = 0.5 + 0.5 * lucidFbm(p * 1.4, LUCID_VALUE, oct);
    float fil = 1.0 - abs(lucidFbm(p * 2.3 + 4.1, LUCID_VALUE, oct));
    fil = pow(fil, 6.0);

    float h = clamp(heat * 0.7 + fil * 0.55 - 0.25, 0.0, 1.0);
    vec3 metal = pal(0.05 + 0.2 * heat);
    vec3 col = mix(blackbody(h), metal * h * 1.4, 0.35);
    col = lucidCubic(col, 0.5);
    // The eye is a dark drain.
    col *= smoothstep(0.0, 0.2, r);

    col *= 0.85 + 0.12 * swell;
    fragColor = vec4(grade(col), 1.0);
}
