#version 300 es
precision highp float;

// LIFE - the present pass. The state is a scalar field of living matter plus
// an age trace; six looks turn one field into six materials:
//
//   uLook 0  glow        soft emissive plasma, age warms the hue
//   uLook 1  ink         dark organism on bright paper - a drawing, not a lamp
//   uLook 2  relief      gradient-lit skin, matter as sculpture
//   uLook 3  iridescent  hue from the surface normal's angle - petrol sheen
//   uLook 4  crystal     posterized shelves with lit edges
//   uLook 5  ember       black body: the field IS temperature
//
// The pass reads the sim at its own resolution with linear filtering, so the
// organism keeps its softness at any screen size.

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uState;
uniform vec2 uRes;        // output size
uniform vec2 uSimRes;     // state size, for gradient texels
uniform int uLook;
uniform float uShowV;     // 0 = show A/u, 1 = show v (the GS pattern channel)
uniform float uBaseHue;
uniform float uHueSpan;
uniform float uEnergy;
uniform float uBeat;

vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

float fieldAt(vec2 uv) {
    vec4 s = texture(uState, uv);
    return mix(s.r, s.g, uShowV);
}

void main() {
    vec2 uv = vUv;
    vec4 state = texture(uState, uv);
    float v = mix(state.r, state.g, uShowV);
    float age = state.b;

    vec2 texel = 1.0 / uSimRes;
    float gx = fieldAt(uv + vec2(texel.x, 0.0)) - fieldAt(uv - vec2(texel.x, 0.0));
    float gy = fieldAt(uv + vec2(0.0, texel.y)) - fieldAt(uv - vec2(0.0, texel.y));
    vec2 grad = vec2(gx, gy);
    float slope = length(grad);

    vec3 color;
    if (uLook == 0) {
        // Glow shaped by the field's own value, saturated in the body and
        // brightening only toward genuine peaks - a flat organism must not
        // wash to white, or every species reads as the same bright blob.
        float glow = smoothstep(0.05, 0.85, v);
        float hue = uBaseHue + uHueSpan * (0.12 * age + 0.18 * v);
        float sat = 0.85 - 0.25 * smoothstep(0.9, 1.0, v);
        color = hsv2rgb(vec3(fract(hue), sat, 0.25 + 0.75 * glow)) * glow * (0.75 + 0.4 * uEnergy);
        // Edge light, BOUNDED: in a dense colony nearly every texel is an
        // edge, and an unbounded gradient term paints the whole organism
        // white. Saturating it keeps the rim a rim.
        float rim = smoothstep(0.08, 0.5, slope) * 0.3;
        color += hsv2rgb(vec3(fract(hue + 0.5), 0.65, 1.0)) * rim;
    } else if (uLook == 1) {
        vec3 paper = hsv2rgb(vec3(fract(uBaseHue), 0.12, 0.92));
        vec3 pigment = hsv2rgb(vec3(fract(uBaseHue + 0.45 * uHueSpan), 0.65, 0.28));
        float mass = smoothstep(0.12, 0.5, v);
        color = mix(paper, pigment, mass);
        color -= vec3(slope * 1.4); // wet edge darkening
    } else if (uLook == 2) {
        vec3 nrm = normalize(vec3(-grad * 8.0, 1.0));
        vec3 light = normalize(vec3(0.5, 0.6, 0.65));
        float diff = max(dot(nrm, light), 0.0);
        float spec = pow(max(dot(reflect(-light, nrm), vec3(0.0, 0.0, 1.0)), 0.0), 24.0);
        float mass = smoothstep(0.06, 0.55, v);
        vec3 skin = hsv2rgb(vec3(fract(uBaseHue + uHueSpan * 0.15 * age), 0.55, 0.9));
        color = skin * mass * (0.25 + 0.85 * diff) + vec3(spec * mass * 0.7);
    } else if (uLook == 3) {
        float angle = atan(grad.y, grad.x) / 6.2831853;
        float mass = smoothstep(0.05, 0.5, v);
        color = hsv2rgb(vec3(fract(uBaseHue + uHueSpan * angle + 0.2 * age), 0.8, 1.0)) * mass;
        color += vec3(0.9, 0.95, 1.0) * pow(slope * 3.0, 2.0);
    } else if (uLook == 4) {
        float shelves = floor(v * 6.0) / 6.0;
        // Distance to the nearest shelf boundary; edges light up when close.
        float boundaryDist = 0.5 - abs(fract(v * 6.0) - 0.5);
        float edge = (1.0 - smoothstep(0.0, 0.1, boundaryDist)) * step(0.08, v);
        vec3 body = hsv2rgb(vec3(fract(uBaseHue + uHueSpan * shelves * 0.4), 0.5, 0.35 + 0.6 * shelves));
        color = body + hsv2rgb(vec3(fract(uBaseHue + 0.5), 0.3, 1.0)) * edge * (0.5 + 0.5 * uBeat);
    } else {
        float heat = smoothstep(0.03, 0.85, v) * (0.75 + 0.45 * uEnergy);
        color = vec3(heat * 1.6, heat * heat * 1.1, heat * heat * heat * 0.9);
        color += vec3(1.0, 0.6, 0.25) * slope * 1.5;
    }

    float aspect = uRes.x / uRes.y;
    vec2 q = (uv - 0.5) * vec2(aspect, 1.0);
    color *= 1.0 - 0.35 * dot(q, q);

    fragColor = vec4(max(color, vec3(0.0)), 1.0);
}
