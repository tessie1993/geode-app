#version 300 es
// The watermark tile, repeated (GL_REPEAT on a power-of-two texture) and laid over the frame with
// premultiplied blending by WatermarkPass. The maths is the Kotlin overlay's, verbatim.
precision mediump float;
in vec2 vTexCoord;
uniform sampler2D uTexture;
out vec4 fragColor;

void main() { fragColor = texture(uTexture, vTexCoord); }
