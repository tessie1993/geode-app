#version 300 es
precision highp float;

// The UI blur's last draw (core/viz/passes/UiBlurPass): the blurred quarter-resolution copy laid back over
// the finished frame inside the glass sheet's rounded rect, mixed by strength. Scissored to the rect, so a
// pixel outside it costs nothing; the rounded corners fade over about one pixel so the edge stays clean
// against the sheet Compose draws on top. Blend: SRC_ALPHA over colour, the destination's alpha kept.

out vec4 fragColor;

uniform sampler2D uBlur;
// Window pixels, GL y-up: center (xy) and half-size (zw), with the corner radius already clamped on CPU.
uniform vec4 uRectTerms;
// Window pixels to the blurred texture's uv: 1 / (4 × its size in texels).
uniform vec2 uTexScale;
uniform float uCorner;
uniform float uStrength;

void main() {
    vec2 p = gl_FragCoord.xy;
    vec2 q = abs(p - uRectTerms.xy) - (uRectTerms.zw - uCorner);
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - uCorner;
    float mask = 1.0 - smoothstep(-0.75, 0.75, d);
    if (mask <= 0.0) discard;
    vec3 blur = texture(uBlur, p * uTexScale).rgb;
    fragColor = vec4(blur, mask * uStrength);
}
