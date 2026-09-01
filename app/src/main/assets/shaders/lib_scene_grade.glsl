// grade() - the shared colour grade every fragment style ends on.
//
// Verbatim from kaleido_frag.glsl, for the same reason lib_scene_uniforms is:
// this block is the meaning of eleven user controls (Bloom, Posterize,
// Duotone, Saturation, Contrast, Gamma, Temperature, Solarize, Flash,
// Brightness/Intensity, Invert), and one slider has to mean one thing on every
// style that offers it.
//
// It reads pal(), so lib_palette must be included ABOVE this file. Include
// order for a style is: lib_scene_uniforms, lib_palette, lib_scene_grade.

vec3 grade(vec3 col) {
    if (uBloom > 0.001) col += uBloom * col * col;
    if (uPosterize > 0.001) {
        float levels = mix(24.0, 3.0, uPosterize);
        col = floor(col * levels + 0.5) / levels;
    }
    float g = dot(col, vec3(0.299, 0.587, 0.114));
    if (uDuotone > 0.5) col = pal(g);
    col = mix(vec3(g), col, uSat);
    col = (col - 0.5) * uContrast + 0.5;
    col = pow(max(col, 0.0), vec3(1.0 / max(uGamma, 0.05)));
    col.r += uTemperature * 0.12;
    col.b -= uTemperature * 0.12;
    if (uSolarize > 0.5) col = abs(1.0 - 2.0 * col);
    col += uFlash * uBeat * 0.6;
    col = col * uBright * uIntensity;
    return mix(col, max(vec3(1.0) - col, 0.0), uInvert);
}
