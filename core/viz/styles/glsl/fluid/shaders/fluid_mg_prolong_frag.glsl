#version 300 es
// Prolongation: the fine level's pressure plus the coarse correction, read
// bilinearly at this fine cell's centre. A fine centre (x + 0.5) sits at
// (x + 0.5) / 2 - 0.5 in coarse cells, which is computed from gl_FragCoord
// rather than from uv because an odd fine side makes the two grids' uv spaces
// disagree by half a coarse cell.
precision highp float;
precision highp sampler2D;
uniform sampler2D uP;      // fine
uniform sampler2D uE;      // coarse correction
out vec4 fragColor;

float e(ivec2 c, ivec2 hi) { return texelFetch(uE, clamp(c, ivec2(0), hi), 0).x; }

void main() {
    ivec2 c = ivec2(gl_FragCoord.xy);
    vec2 st = gl_FragCoord.xy * 0.5 - 0.5;
    vec2 i = floor(st);
    vec2 w = st - i;
    ivec2 b = ivec2(i);
    ivec2 hi = textureSize(uE, 0) - 1;
    float lower = mix(e(b, hi), e(b + ivec2(1, 0), hi), w.x);
    float upper = mix(e(b + ivec2(0, 1), hi), e(b + ivec2(1, 1), hi), w.x);
    float v = texelFetch(uP, c, 0).x + mix(lower, upper, w.y);
    if (isnan(v) || isinf(v)) v = 0.0;
    fragColor = vec4(v, 0.0, 0.0, 1.0);
}
