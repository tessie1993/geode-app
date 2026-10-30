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

// Nebula Veil: a stretch of an old supernova remnant's rim, the part where
// the shock front lies edge-on to us and the gas is a lace of thin, twisted
// filaments. The remnant's centre sits off screen to the lower left, so the
// filaments sweep across the frame in a broad arc, two strands of them -
// one in a warm palette hue, one in a cool one, offset across the front the
// way the hot and cooler gas separate in the real Veil - over a dim diffuse
// glow and a scatter of stars.
//
// The filaments are ridged gradient noise (nebRidged2) read in arc
// coordinates: distance from the remnant's centre across the front, a plain
// linear coordinate along it. Stretching the noise along the front makes its
// crests run as long threads rather than as a net. No atan is taken, so no
// seam can appear however far the view drifts.
//
// ---- audio ------------------------------------------------------------------
//
// Highs (uTrebleSmooth with the presence band, uBandsHi.y) LIGHT THE
// FILAMENTS: they sharpen and brighten the threads only - the diffuse glow
// and the dark between them stay where they are - so cymbals and hats make
// the lace glitter while the bass leaves it alone. The hats also twinkle the
// stars. The key turns both strand hues. Overall level follows uSwell gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the slow drift of the front. Complexity: uMorph adds
// ridged octaves, finer threads (nebOctaves). Chaos: uWarp/uTurbulence curl
// the threads (nebWarp2); uTwist via view(). Colour: both strands and the
// glow are pal(). Energy: uIntensity scales how much the highs light the
// threads. Space: view().

void main() {
    vec2 uv = view();
    float t = nebTime();
    float flow = nebFlow();
    float e = nebEnergy();
    float hi = clamp(0.55 * uTrebleSmooth + 0.45 * uBandsHi.y, 0.0, 1.3) * e;
    float swell = clamp(uSwell, 0.0, 1.5);
    float hats = clamp(uHits.z, 0.0, 1.0);
    float oct = nebOctaves(4.0);
    float key = nebKeyShift(0.1);

    vec2 p = nebWarp2(uv, t);
    vec2 c = vec2(-1.6, -1.25);
    vec2 rel = p - c;
    float R = length(rel);
    float drift = flow * 0.004 + t * 0.003;
    // Across the front (radius) and along it (a fixed tangent direction).
    vec2 q = vec2(R * 2.6, dot(rel, vec2(-0.6, 0.8)) * 0.8 + drift);

    float band = exp(-pow((R - 2.05) / 0.62, 2.0));
    float fa = nebRidged2(vec2(q.x * 2.4, q.y * 0.7) + vec2(0.0, drift), oct);
    float fb = nebRidged2(vec2(q.x * 2.4 + 0.35, q.y * 0.7) + vec2(5.3, 2.9 - drift * 0.6), oct);

    float sharp = 7.0 - 2.5 * clamp(hi, 0.0, 1.0);
    float gain = 0.55 + 0.75 * hi;
    float threadA = pow(fa, sharp) * band;
    float threadB = pow(fb, sharp) * band;

    vec3 warm = nebEmission(0.02 + key, 0.15);
    vec3 cool = nebEmission(0.52 + key, 0.15);
    vec3 col = (warm * threadA + cool * threadB) * 1.5 * gain;
    // The diffuse glow the threads sit in, and a faint wash inside the shell.
    col += mix(warm, cool, 0.5) * 0.07 * band * (0.5 + fa);
    col += cool * 0.02 * smoothstep(2.6, 1.2, R);

    col += nebStars(uv, 8.0, 2, vec2(0.0), 1.0, 0.2, hats, t) * (1.0 - 0.4 * band);
    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
