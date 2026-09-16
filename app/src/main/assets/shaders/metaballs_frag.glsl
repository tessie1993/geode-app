#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uAudioTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Converted from a verbatim duplicate of the uniform block/view()/grade() to
// the shared includes: this style now needs lib_scene_motion's continuous
// uniforms (uEnergyRel, uTrebRel), and duplicating those declarations a
// second time here is exactly what lib_scene_uniforms exists to avoid. The
// picture is unchanged; view()/grade() now resolve to the same shared
// functions the rest of the styles use.
//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_palette
//#include lib_scene_grade

// Metaballs sized by band energy, drifting to the music.
//
// motion: uTrebRel -> ball radius / spawn detail (fine, fast-moving balls
// pick out the high end), uEnergyRel -> glow gain and rim brightness.
void main() {
    vec2 p = view();
    float t = uTime * 0.6;
    float field = 0.0;
    float hueAcc = 0.0;
    float trebRel = clamp(uTrebRel, 0.0, 2.0);
    float energyRel = clamp(uEnergyRel, 0.0, 2.0);
    for (int i = 0; i < 7; i++) {
        float fi = float(i) / 7.0;
        float b = aband(fi);
        vec2 c = 0.75 * vec2(sin(t * (0.5 + fi) + fi * 6.28), cos(t * (0.7 + fi * 0.5) + fi * 4.0));
        // Relative treble nudges the radius: a bright passage inflates the
        // balls by up to 10%, never a snap.
        float radius = (0.05 + b * 0.22) * (0.95 + 0.05 * trebRel);
        float d = max(length(p - c), 0.001);
        float contrib = radius * radius / (d * d);
        field += contrib;
        hueAcc += fi * contrib;
    }
    float hue = hueAcc / max(field, 0.001);
    float body = smoothstep(0.9, 1.15, field);
    float rim = smoothstep(0.9, 1.0, field) - smoothstep(1.05, 1.3, field);
    vec3 col = pal(hue) * body * (0.5 + uEnergy) * (0.9 + 0.1 * energyRel)
        + vec3(1.0) * max(rim, 0.0) * (0.4 + 0.3 * energyRel);
    fragColor = vec4(grade(col), 1.0);
}
