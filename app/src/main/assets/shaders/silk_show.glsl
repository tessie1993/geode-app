// SILK - the present pass.
//
// A SimPass DISPLAY BODY. Unlike the step body next door this one does bring
// its own `in`, `out` and `main()` - it is an ordinary fragment shader over the
// state, run with quad_vert - but it does NOT bring a #version or a precision
// line, and it does not declare the state sampler. SimPass.displayShader()
// prepends all of that, so the present pass reads the dye through the same
// simSample() the step writes it with and is equally blind to the encoding: a
// filterable half-float field and the pre-scaled RGBA8 fallback look identical
// from in here, and the scale that separates them is a compile-time constant
// folded into the fetch rather than a uniform this file could forget.
//
// The sim texture holds three band lanes of dye (r bass, g mid, b treble).
// Each lane gets its own point on the user's palette - base hue for the bass
// body, offsets for mid and treble filaments, nudged by the melody's pitch
// class - then the sum is tone-mapped.
//
// The dye is also read as a HEIGHT (lib_relief): dense silk stands up as a
// lit ridge, empty field is the floor, and the eye leans and the light orbits
// on the sound clocks, so the strands read as a 3D surface with parallax
// rather than a flat picture. Each style has a base and an alternate material,
// swapped on every hard cut together with its alternate field (SilkScene).
//
// Styles that declare mirror folds recompose the field kaleidoscopically
// here, in the PRESENT pass only, so the simulation itself stays unfolded
// and un-mirrored (folding the sim would double-advect the seam). The fold is
// applied once, before the relief march, so the march's taps cost one fetch
// each and no trigonometry.

in vec2 vUv;
out vec4 fragColor;

uniform vec2 uRes;         // OUTPUT size, for aspect - not the sim grid, which is uSimSize
uniform float uBaseHue;    // palette identity, turns
uniform float uHueSpan;    // palette span, turns
uniform float uExposure;   // the style's exposure, lifted gently by the energy trend
uniform int uFold;         // mirror folds; 0 = none (steps on every 16th kick)
uniform float uFoldPhase;  // fold rotation on the mid and hit clocks, radians
uniform float uEnergy;     // overall level, lifts the floor glow
uniform float uMelodyHue;  // the melody's pitch class, slewed around the circle, turns
uniform float uMelodyAmt;  // how sure the pitch is, slewed: 0 leaves the palette alone
uniform int uLookBase;     // RELIEF_* material, and the one a hard cut swaps to
uniform int uLookAlt;
uniform float uDepth;      // relief thickness, uv

//#include lib_sound

const float TAU = 6.2831853;

#define RELIEF_EPS 0.004
float reliefHeight(vec2 uv) {
    vec3 lanes = simSample(uv).rgb;
    return 1.0 - exp(-dot(max(lanes, vec3(0.0)), vec3(0.55, 0.45, 0.35)));
}
//#include lib_relief

vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

vec2 foldUv(vec2 uv) {
    if (uFold <= 0) return uv;
    float aspect = uRes.x / uRes.y;
    vec2 q = (uv - 0.5) * vec2(aspect, 1.0);
    float a = atan(q.y, q.x) + uFoldPhase;
    float r = length(q);
    float seg = TAU / float(uFold);
    a = abs(mod(a, seg) - seg * 0.5);
    q = r * vec2(cos(a), sin(a));
    return q / vec2(aspect, 1.0) + 0.5;
}

void main() {
    Relief r = reliefMarch(foldUv(vUv), reliefTilt(), uDepth);
    vec3 lanes = clamp(simSample(r.uv).rgb, vec3(0.0), vec3(8.0));

    // The melody turns the mid and treble hues a little around their palette points: sine and
    // cosine of the pitch class, so every note of the octave gets its own pair and B to C is smooth.
    float span = uHueSpan * 0.18;
    vec2 tune = 0.16 * uMelodyAmt * vec2(sin(TAU * uMelodyHue), cos(TAU * uMelodyHue));
    vec3 cBass = hsv2rgb(vec3(fract(uBaseHue), 0.85, 1.0));
    vec3 cMid = hsv2rgb(vec3(fract(uBaseHue + span + tune.x), 0.7, 1.0));
    vec3 cTreb = hsv2rgb(vec3(fract(uBaseHue + 2.0 * span + tune.y), 0.45, 1.0));

    vec3 hdr = cBass * lanes.r + cMid * lanes.g + cTreb * lanes.b;

    // Tone map: additive lanes are HDR by construction; exp rolloff keeps
    // crossings luminous without clipping to one white blob.
    vec3 glow = 1.0 - exp(-hdr * uExposure);

    // The material: lit only where there is silk (the floor stays a dark void), with the glow kept
    // underneath as the strands' own light, Arbor's emissive-on-dark. Hats and note starts glitter.
    float sparkle = sndShaped(SND_HAT_ENV) * 0.8 + sndShaped(SND_ONSET_ENV) * 0.3;
    vec3 shade = reliefShade(reliefLook(uLookBase, uLookAlt), glow, r, reliefLight(), sparkle);
    float body = smoothstep(0.02, 0.22, r.h);
    vec3 color = shade * body + glow * 0.3;

    // A snare lights the ridges' slopes in the complementary hue.
    vec3 comp = hsv2rgb(vec3(fract(uBaseHue + 0.45), 0.7, 1.0));
    float ridge = smoothstep(0.5, 0.9, r.h) * clamp(1.0 - r.n.z, 0.0, 1.0);
    color += comp * ridge * sndShaped(SND_SNARE_ENV) * 0.6;

    // A whisper of ambient depth so silence is a deep field, not a void.
    float aspect = uRes.x / uRes.y;
    vec2 q = (vUv - 0.5) * vec2(aspect, 1.0);
    float vig = 1.0 - 0.45 * dot(q, q);
    color += cBass * 0.015 * (0.4 + uEnergy) * vig;
    color *= vig;

    fragColor = vec4(clamp(color, vec3(0.0), vec3(1.0)), 1.0);
}
