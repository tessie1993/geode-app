// grade() - the shared colour grade every fragment style ends on.
//
// Shared by every fragment style, for the same reason lib_scene_uniforms is:
// this block is the meaning of eleven user controls (Bloom, Posterize,
// Duotone, Saturation, Contrast, Gamma, Temperature, Solarize, Flash,
// Brightness/Intensity, Invert), and one slider has to mean one thing on every
// style that offers it.
//
// It reads pal(), so lib_palette must be included ABOVE this file. Include
// order for a style is: lib_scene_uniforms, lib_palette, lib_scene_grade.

// Posterize levels, inverse gamma, temperature shift, brightness * intensity.
// Resolved once per frame in core/viz/styles/shader/SceneTerms.hpp.
uniform vec4 uSceneGrade;

vec3 grade(vec3 col) {
    if (uBloom > 0.001) col += uBloom * col * col;
    if (uPosterize > 0.001) {
        col = floor(col * uSceneGrade.x + 0.5) / uSceneGrade.x;
    }
    float g = dot(col, vec3(0.299, 0.587, 0.114));
    if (uDuotone > 0.5) col = pal(g);
    col = mix(vec3(g), col, uSat);
    col = (col - 0.5) * uContrast + 0.5;
    col = pow(max(col, 0.0), vec3(uSceneGrade.y));
    col.r += uSceneGrade.z;
    col.b -= uSceneGrade.z;
    if (uSolarize > 0.5) col = abs(1.0 - 2.0 * col);
    col *= uSceneGrade.w;
    return mix(col, max(vec3(1.0) - col, 0.0), uInvert);
}
