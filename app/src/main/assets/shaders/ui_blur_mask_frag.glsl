#version 300 es
precision highp float;

// The UI blur's last draw (core/viz/UiBlurPass): the blurred quarter-resolution copy laid back over the
// finished frame inside the glass sheet's rounded rect, mixed by strength. Scissored to the rect, so a
// pixel outside it costs nothing; the rounded corners fade over about one pixel so the edge stays clean
// against the sheet Compose draws on top. Blend: SRC_ALPHA over colour, the destination's alpha kept.

out vec4 fragColor;

uniform sampler2D uBlur;
// The sheet in window pixels, GL y-up: (x0, y0, x1, y1).
uniform vec4 uRect;
// Window pixels to the blurred texture's uv: 1 / (4 × its size in texels).
uniform vec2 uTexScale;
uniform float uCorner;
uniform float uStrength;

void main() {
    vec2 p = gl_FragCoord.xy;
    vec2 centre = 0.5 * (uRect.xy + uRect.zw);
    vec2 halfSize = 0.5 * (uRect.zw - uRect.xy);
    float r = clamp(uCorner, 0.0, min(halfSize.x, halfSize.y));
    vec2 q = abs(p - centre) - (halfSize - r);
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    float mask = 1.0 - smoothstep(-0.75, 0.75, d);
    if (mask <= 0.0) discard;
    vec3 blur = texture(uBlur, p * uTexScale).rgb;
    fragColor = vec4(blur, mask * uStrength);
}
