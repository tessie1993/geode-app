#version 300 es
// The watermark tile, laid over the frame with premultiplied blending by WatermarkPass: repeated (GL_REPEAT)
// for the tiled pattern, or shown once (uClip) for a corner mark or the end card, which also dims the picture
// (uDim) and fades in (uAlpha). Tiled (uClip and uDim 0, uAlpha 1) it is the Kotlin overlay's maths, verbatim.
precision mediump float;
in vec2 vTexCoord;
uniform sampler2D uTexture;
uniform float uClip;
uniform float uDim;
uniform float uAlpha;
out vec4 fragColor;

void main() {
    vec4 c = texture(uTexture, vTexCoord);
    bool outside = any(lessThan(vTexCoord, vec2(0.0))) || any(greaterThan(vTexCoord, vec2(1.0)));
    if (uClip > 0.5 && outside) c = vec4(0.0);
    c += (1.0 - c.a) * vec4(0.0, 0.0, 0.0, uDim);
    fragColor = c * uAlpha;
}
