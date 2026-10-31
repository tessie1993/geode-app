#version 300 es
precision highp float;

in vec3 vColor;
in float vAlpha;
out vec4 fragColor;

// A soft round sprite, premultiplied (blended ONE, ONE_MINUS_SRC_ALPHA).
void main() {
    vec2 c = gl_PointCoord - 0.5;
    float d = dot(c, c) * 4.0;
    if (d > 1.0) discard;
    float a = vAlpha * (1.0 - d) * (1.0 - d);
    fragColor = vec4(clamp(vColor, 0.0, 1.0) * a * 1.4, a);
}
