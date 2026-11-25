#version 300 es
// Fronts (core/viz/passes/FrontPass, the state and packing in core/viz/fronts/FrontField.hpp): up to eight
// lines, rings and wedges moving across any style's finished picture. Inside its band a front brightens,
// shifts toward a palette colour, bends, inverts or reveals. A front carries no colour of its own: uTint is
// the current palette at the front's position, resolved by the renderer.
precision highp float;

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uScene;
uniform sampler2D uOther;  // what a Reveal shows behind the front
uniform float uHasOther;
uniform float uAspect;     // width / height
uniform int uCount;
// Per front, three vec4:
//   [0] originX, originY (uv), dirX, dirY (unit, in frame heights)
//   [1] kind (0 line, 1 ring, 2 wedge), effect (0 brighten, 1 colour, 2 bend, 3 invert, 4 reveal),
//       distance travelled, half width
//   [2] amount, wedge half angle, palette position, unused
uniform vec4 uFront[24];
uniform vec3 uTint[8];

// The band: 1 on the front's line, 0 at one half width either side. d is the signed distance past the front.
float band(float d, float w) { return 1.0 - smoothstep(0.0, w, abs(d)); }

// How far this pixel is past the front (negative: the front has not reached it yet, positive: it has passed
// it), the unit direction of travel there, and the wedge's sector mask.
void measure(vec2 uv, int i, out float d, out vec2 n, out float sector) {
    vec4 a = uFront[3 * i];
    vec4 b = uFront[3 * i + 1];
    vec4 c = uFront[3 * i + 2];
    vec2 p = (uv - a.xy) * vec2(uAspect, 1.0);
    sector = 1.0;
    if (b.x < 0.5) {
        d = dot(p, a.zw) - b.z;
        n = a.zw;
    } else {
        float r = length(p);
        n = r > 1e-5 ? p / r : a.zw;
        d = r - b.z;
        if (b.x > 1.5) {
            float ang = acos(clamp(dot(n, a.zw), -1.0, 1.0));
            sector = 1.0 - smoothstep(0.8 * c.y, c.y, ang);
        }
    }
}

void main() {
    // Bends first: every front moves where the picture is read from, then the colour effects apply to that.
    vec2 uv = vUv;
    for (int i = 0; i < 8; ++i) {
        if (i >= uCount) break;
        vec4 b = uFront[3 * i + 1];
        if (abs(b.y - 2.0) > 0.5) continue;
        float d, s;
        vec2 n;
        measure(vUv, i, d, n, s);
        float w = b.w;
        float x = d / w;
        // A lens across the band: the picture is read from behind the front on one side and ahead of it on the other.
        float push = x * (1.0 - smoothstep(0.0, 1.0, abs(x)));
        uv -= n * push * w * uFront[3 * i + 2].x * s * 0.5 / vec2(uAspect, 1.0);
    }
    vec3 col = texture(uScene, uv).rgb;
    for (int i = 0; i < 8; ++i) {
        if (i >= uCount) break;
        vec4 b = uFront[3 * i + 1];
        float amount = uFront[3 * i + 2].x;
        float d, s;
        vec2 n;
        measure(vUv, i, d, n, s);
        float k = band(d, b.w) * s * amount;
        if (b.y < 0.5) {
            col *= 1.0 + 1.5 * k;
        } else if (b.y < 1.5) {
            float luma = dot(col, vec3(0.299, 0.587, 0.114));
            col = mix(col, uTint[i] * max(luma * 1.5, 0.15), clamp(k, 0.0, 1.0));
        } else if (b.y < 3.5 && b.y > 2.5) {
            col = mix(col, vec3(1.0) - col, clamp(k, 0.0, 1.0));
        } else if (b.y > 3.5) {
            // Where the front has passed, the other layer shows; with none, the picture shows from dark.
            float passed = smoothstep(-b.w, b.w, d) * s;
            vec3 other = uHasOther > 0.5 ? texture(uOther, vUv).rgb : col;
            vec3 ahead = uHasOther > 0.5 ? col : vec3(0.0);
            vec3 shown = mix(ahead, other, passed);
            col = mix(col, shown, clamp(amount, 0.0, 1.0));
        }
    }
    fragColor = vec4(col, 1.0);
}
