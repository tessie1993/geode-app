#version 300 es
precision highp float;

// The analytic line integral of a Gaussian beam along one waveform segment -
// the reason a real oscilloscope looks the way it does, and the thing a
// distance-falloff scope cannot fake.
//
// A CRT beam deposits energy at a constant rate in TIME, so a segment the beam
// crosses slowly (a short one, where the signal is barely moving) piles that
// energy into a small area and glows; a fast sweep spreads the same energy over
// a long segment and dims. Dividing the integral by the segment length is what
// produces that, and it is why the bright parts of a scope trace are the turning
// points rather than wherever the geometry happens to be dense.
//
// Ported from woscope (MIT, Igor Null and Chad von Nau) - see
// THIRD_PARTY_NOTICES.

in vec2 vLocal;
in float vLen;
in float vAge;

out vec4 fragColor;

uniform float uSigma;
uniform vec3 uColor;
uniform float uIntensity;
/** Fade along the trace, so the newest part of the sweep is the brightest. */
uniform float uTail;

/** Abramowitz & Stegun 7.1.26; max error ~1.5e-7, far below 8-bit output. */
float erfApprox(float x) {
    float s = sign(x);
    float a = abs(x);
    float t = 1.0 / (1.0 + 0.3275911 * a);
    float y = 1.0 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t * exp(-a * a);
    return s * y;
}

void main() {
    float sigma = max(uSigma, 1e-4);
    // Integral of exp(-r^2 / 2 sigma^2) along the segment, evaluated at this
    // fragment: the across-track term is a plain Gaussian, the along-track term
    // is the difference of two error functions at the segment's ends.
    float across = exp(-(vLocal.y * vLocal.y) / (2.0 * sigma * sigma));
    float along = erfApprox(vLocal.x / (1.41421356 * sigma)) - erfApprox((vLocal.x - vLen) / (1.41421356 * sigma));
    // Dwell: energy per unit time spread over the length actually travelled.
    // The floor keeps a segment of zero length (a held signal) from dividing
    // its whole energy into one pixel and blowing out.
    float dwell = 1.0 / max(vLen, sigma);
    float beam = 0.5 * across * along * dwell * uIntensity;
    float fade = mix(1.0, vAge, clamp(uTail, 0.0, 1.0));
    fragColor = vec4(uColor * beam * fade, 1.0);
}
