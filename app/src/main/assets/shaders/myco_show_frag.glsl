#version 300 es
precision highp float;

// MYCELIUM - the present pass. The trail field is the organism's memory;
// six looks make six materials of it:
//
//   uLook 0  veins     luminous organic filigree, the classic slime look
//   uLook 1  filament  thin bright threads on near-black, moonlit web
//   uLook 2  nebula    wide soft glow, the network as interstellar dust
//   uLook 3  circuit   posterized traces with pad glints, a living PCB
//   uLook 4  ember     the network as heat, black-body ramped
//   uLook 5  relief    gradient-lit skin, roots under a raking light
//
// Species A and B each take their own point on the user's palette, so a
// two-population style is legible as two organisms sharing one world.

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uTrail;
uniform vec2 uRes;
uniform vec2 uTrailRes;
uniform int uLook;
uniform float uBaseHue;
uniform float uHueSpan;
uniform float uExposure;
uniform float uEnergy;
uniform float uBeat;

vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

float lumAt(vec2 uv) {
    vec2 t = texture(uTrail, uv).rg;
    return t.r + t.g;
}

void main() {
    vec2 uv = vUv;
    vec2 trail = texture(uTrail, uv).rg;
    float total = trail.r + trail.g;
    float tone = 1.0 - exp(-total * uExposure);

    vec3 cA = hsv2rgb(vec3(fract(uBaseHue), 0.8, 1.0));
    vec3 cB = hsv2rgb(vec3(fract(uBaseHue + 0.35 * uHueSpan), 0.75, 1.0));
    vec3 mixed = (cA * trail.r + cB * trail.g) / max(total, 1e-4);

    vec3 color;
    if (uLook == 0) {
        // Sharpened body: the diffusion halo around every trunk must fall
        // away on display or thin veins read as fat tubes.
        float body = pow(tone, 1.8);
        color = mixed * body * (0.85 + 0.35 * uEnergy);
        color += mixed * pow(tone, 5.0) * 0.7; // hot cores on the trunks
    } else if (uLook == 1) {
        float thread = pow(tone, 2.2);
        color = mix(vec3(0.004, 0.006, 0.01), mixed, thread) * (0.4 + 1.4 * thread);
    } else if (uLook == 2) {
        float soft = 1.0 - exp(-total * uExposure * 0.45);
        color = mixed * soft * soft * 1.4;
        color += cB * 0.02;
    } else if (uLook == 3) {
        float traces = smoothstep(0.18, 0.32, tone);
        float pads = smoothstep(0.75, 0.95, tone);
        color = vec3(0.008, 0.02, 0.012) + mixed * traces * 0.75;
        color += cA * pads * (0.8 + 0.6 * uBeat);
    } else if (uLook == 4) {
        float heat = pow(tone, 1.4) * (0.8 + 0.4 * uEnergy);
        color = vec3(heat * 1.7, heat * heat * 1.15, heat * heat * heat);
    } else {
        vec2 texel = 1.0 / uTrailRes;
        float gx = lumAt(uv + vec2(texel.x, 0.0)) - lumAt(uv - vec2(texel.x, 0.0));
        float gy = lumAt(uv + vec2(0.0, texel.y)) - lumAt(uv - vec2(0.0, texel.y));
        vec3 nrm = normalize(vec3(-gx * 6.0, -gy * 6.0, 1.0));
        float diff = max(dot(nrm, normalize(vec3(0.55, 0.5, 0.62))), 0.0);
        color = mixed * tone * (0.2 + 1.0 * diff);
        color += vec3(pow(max(dot(nrm, vec3(0.0, 0.0, 1.0)), 0.0), 30.0)) * tone * 0.3;
    }

    float aspect = uRes.x / uRes.y;
    vec2 q = (uv - 0.5) * vec2(aspect, 1.0);
    color *= 1.0 - 0.38 * dot(q, q);

    fragColor = vec4(max(color, vec3(0.0)), 1.0);
}
