#version 300 es
// A Studio render's clip frame drawn unchanged (core/viz/frame/ExportPasses): the transition pass outside
// its window and the capture pass that keeps a clip's last frame for the next clip's transition. Verbatim
// from export/GlTransitionEffect.kt's pass-through; drawn over quad_vert.glsl.
precision mediump float;
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTex;
void main() { fragColor = texture(uTex, vUv); }
