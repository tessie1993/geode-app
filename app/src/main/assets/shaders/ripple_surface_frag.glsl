#version 300 es
// The water surface, drawn from the drop list (core/viz/fluid/RippleMath): the sum of every ring and its
// echoes. Reads no earlier frame. Each term is ripple::height / ripple::heightRate, line for line.
precision highp float;
in vec2 vSim;
uniform vec4 uWavelets[60];        // xy = centre, z = leading crest radius, w = crest height
uniform float uInvWavelength[60];
uniform int uWaveletCount;
uniform float uWaveSpeed;
out vec4 fragColor;

const float TAU = 6.2831853;

void main() {
    float h = 0.0;
    float rate = 0.0;
    for (int i = 0; i < 60; i++) {
        if (i >= uWaveletCount) break;
        vec4 w = uWavelets[i];
        float inv = uInvWavelength[i];
        float core = 0.3 / inv;
        vec2 d = vSim - w.xy;
        float s = (sqrt(dot(d, d) + core * core) - core - w.z) * inv;
        if (s > 1.7 || s < -4.0) continue;
        float width = s > 0.0 ? 0.55 : 1.15;
        float win = exp(-(s * s) / (width * width));
        float c = cos(TAU * s);
        h += w.w * c * win;
        rate -= w.w * (-TAU * sin(TAU * s) - 2.0 * s / (width * width) * c) * win * uWaveSpeed * inv;
    }
    fragColor = vec4(h, rate, 0.0, 1.0);
}
