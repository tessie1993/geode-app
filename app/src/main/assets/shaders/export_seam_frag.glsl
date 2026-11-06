#version 300 es
// A loop render's seam (core/viz/frame/ExportPasses, SeamPass): a stashed opening frame laid over a
// closing one at a constant alpha. Source-alpha blending of a constant alpha is exactly a cross-dissolve.
// Verbatim from LoopRender's SeamStash; drawn over quad_vert.glsl.
precision mediump float;
uniform sampler2D uTex;
uniform float uAlpha;
in vec2 vUv;
out vec4 fragColor;
void main() {
    fragColor = vec4(texture(uTex, vUv).rgb, uAlpha);
}
