#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_palette
//#include lib_scene_grade
//#include lib_nebula

// Nebula Dust: the family's plain form, and its free style. A flat field of
// glowing gas with dark dust lanes winding across it and stars behind both,
// drifting slowly sideways in three layers - stars slowest, gas faster, dust
// fastest - so one travel reads as depth. Two fbm reads and three star
// layers, nothing else: the cheapest Nebula.
//
// The gas glows where its fbm crests, in two palette hues that follow the
// field's height. The dust is a second fbm read as ridges (its 0.5 contour),
// so it lies in long winding lanes rather than blobs, plus a few dense dark
// globules where it peaks. The dust absorbs through nebAbsorption, so the
// lanes redden their edges before they go black, as real dust does.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (sndShaped(SND_LOW), steadied with the bass band region) DEEPENS THE COLOUR:
// the gas threshold drops so the glow spreads into more of the field, its hue
// saturates (nebRich), and the dust lanes thicken - a heavy low end pulls the
// whole cloud denser and richer, a quiet passage thins it to wisps. The hats
// set the stars twinkling. The key turns the gas hue a little. Overall level
// follows SND_INTENSITY gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the drift and the twinkle. Complexity: uMorph adds up
// to two fbm octaves (nebOctaves). Chaos: uWarp/uTurbulence swirl the gas
// and dust through a curl warp (nebWarp2); uTwist through view(). Colour:
// gas, haze and dust tint are pal(). Energy: uIntensity scales the bass
// push. Space: view() (zoom, rotation, drift).

void main() {
    vec2 uv = view();
    float t = nebTime();
    float flow = nebFlow();
    float e = nebEnergy();
    float bass = clamp(0.6 * sndShaped(SND_LOW) + 0.4 * sndSpectrum(0.23), 0.0, 1.3) * e;
    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float hats = clamp(SND_HAT_ENV, 0.0, 1.0);
    float oct = nebOctaves(4.0);
    float key = nebKeyShift(0.12);

    // One slow travel, three depths of parallax.
    vec2 travel = vec2(0.9, 0.3) * (flow * 0.012 + t * 0.006);
    vec2 p = nebWarp2(uv, t);

    float gas = nebFbm2(p * 1.15 + travel * 0.6, oct);
    float dust = nebFbm2(p * 1.9 + travel + vec2(7.3, -2.1), oct);

    // Emission: the crests of the gas field, spreading lower as bass rises.
    float glow = smoothstep(0.44 - 0.09 * bass, 0.82, gas);
    vec3 tint = nebEmission(0.12 + 0.55 * gas + key, 0.18 * glow);
    tint = nebRich(tint, 0.9 * bass);
    vec3 col = tint * glow * (0.85 + 0.25 * bass);
    col += pal(0.62 + key) * 0.035 * (0.4 + gas);

    // Stars behind the gas: the emission veils the fainter ones.
    vec3 stars = nebStars(uv, 7.0, 3, travel * 0.3, 1.0, 0.22, hats, t);
    col += stars * (1.0 - 0.6 * glow);

    // Dust in front of both: winding lanes along the field's mid contour,
    // dark globules where it peaks.
    float ridge = 1.0 - abs(2.0 * dust - 1.0);
    float tau = smoothstep(0.55, 0.93, ridge) * (1.0 + 0.9 * bass);
    tau += smoothstep(0.66, 0.86, dust) * 1.6;
    col *= exp(-tau * nebAbsorption(0.4 + key));

    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
