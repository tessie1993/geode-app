#version 300 es
// Residual and restriction in one pass, drawn at the COARSE level: each coarse
// cell is the mean of its 2x2 fine block's residual rhs - lap p. Fusing the two
// saves a full-size residual target and a fine-grid pass per level per cycle;
// the 12 fine taps it reads would have been read by a separate residual pass
// anyway. A block that runs off an odd edge repeats the edge cell, which is
// what the cell-centred (n + 1) / 2 coarse size implies.
precision highp float;
precision highp sampler2D;
uniform sampler2D uP;      // the fine level's pressure or correction
uniform sampler2D uRhs;    // the fine level's right-hand side
uniform float uH2;         // the FINE level's cell size squared
out vec4 fragColor;

float p(ivec2 c, ivec2 hi) { return texelFetch(uP, clamp(c, ivec2(0), hi), 0).x; }

float residual(ivec2 c, ivec2 hi) {
    c = min(c, hi);
    float lap = p(c - ivec2(1, 0), hi) + p(c + ivec2(1, 0), hi) + p(c - ivec2(0, 1), hi) + p(c + ivec2(0, 1), hi) - 4.0 * p(c, hi);
    return texelFetch(uRhs, c, 0).x - lap / uH2;
}

void main() {
    ivec2 f = ivec2(gl_FragCoord.xy) * 2;
    ivec2 hi = textureSize(uP, 0) - 1;
    float r = residual(f, hi) + residual(f + ivec2(1, 0), hi) + residual(f + ivec2(0, 1), hi) + residual(f + ivec2(1, 1), hi);
    r *= 0.25;
    // Half-float storage tops out at 65504, as the divergence pass clamps.
    fragColor = vec4(clamp(r, -60000.0, 60000.0), 0.0, 0.0, 1.0);
}
