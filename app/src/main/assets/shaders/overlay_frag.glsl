#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to lowp (see composite_frag.glsl's uTexA/uTexB
// comment): explicit highp here avoids quantizing cover-art/text/logo reads below the RGBA8
// source data's own precision on GPUs that honor sampler precision (e.g. Mali).
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Overlay (W00): a full-frame, premultiplied-alpha RGBA8 texture (cover art, text, lyrics,
// logos, ...) drawn as the very last thing in the composite stage, straight over the finished
// frame. Sampled with linear filtering to the output size; aspect is the caller's job. Blended
// by CompositePass::drawOverlay with GL_ONE/GL_ONE_MINUS_SRC_ALPHA (correct for premultiplied
// alpha), so this fragment shader itself is a plain texture fetch. See CompositePass.hpp for the
// full pass description.
uniform sampler2D uOverlay;

void main() {
    fragColor = texture(uOverlay, vUv);
}
