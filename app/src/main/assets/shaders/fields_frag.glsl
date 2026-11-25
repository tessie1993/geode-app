#version 300 es
// Fields (core/viz/passes/FieldPass, the state and packing in core/viz/fields/FieldSet.hpp): up to eight force
// points that bend any style's finished picture. Attract draws the picture in toward the point, Repel pushes it
// out, Swirl turns it about the point. The pass reads the picture from where each Field moves it from; it adds no
// colour of its own.
precision highp float;

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uScene;
uniform float uAspect;  // width / height
uniform int uCount;
// Per Field, two vec4:
//   [0] x, y (uv), radius (frame heights), kind (0 attract, 1 repel, 2 swirl)
//   [1] amount (signed strength x envelope), unused x3
uniform vec4 uField[16];

void main() {
    vec2 frameScale = vec2(uAspect, 1.0);
    vec2 uv = vUv;
    for (int i = 0; i < 8; ++i) {
        if (i >= uCount) break;
        vec4 a = uField[2 * i];
        float amount = uField[2 * i + 1].x;
        vec2 d = (a.xy - vUv) * frameScale;  // toward the Field, in frame heights
        float r = length(d);
        float t = r / a.z;
        if (t >= 1.0) continue;
        float w = (1.0 - t) * (1.0 - t);
        vec2 off;
        if (a.w < 0.5) {
            // Content moves toward the point, so a pixel shows what was farther out.
            off = -d / sqrt(r * r + 0.0025) * amount * w * 0.04;
        } else if (a.w < 1.5) {
            off = d / sqrt(r * r + 0.0025) * amount * w * 0.04;
        } else {
            // Content turns counter-clockwise for a positive amount, so a pixel shows what was clockwise of it.
            float th = amount * w * 0.6;
            vec2 p = -d;
            vec2 q = vec2(cos(th) * p.x + sin(th) * p.y, -sin(th) * p.x + cos(th) * p.y);
            off = q - p;
        }
        uv += off / frameScale;
    }
    fragColor = vec4(texture(uScene, uv).rgb, 1.0);
}
