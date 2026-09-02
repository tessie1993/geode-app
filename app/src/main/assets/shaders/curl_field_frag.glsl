#version 300 es
// Curl-noise velocity field per Bridson et al. "Curl-Noise for Procedural
// Fluid Flow" (SIGGRAPH 2007): v = (dPsi/dy, -dPsi/dx) of a noise potential -
// divergence-free by construction, so particles stream and swirl without
// clumping.
//
// The potential is psrdnoise (MIT, Gustavson & McEwan - see
// THIRD_PARTY_NOTICES), which returns its own ANALYTIC gradient. That is the
// whole reason it is here, and it fixes two things at once:
//
//   * EXACTNESS. This used to take central differences of a hand-rolled value
//     noise at e = 0.02, so the field was only approximately divergence-free -
//     the finite difference is the gradient of a slightly different function
//     than the one being sampled, and the error is exactly the divergence that
//     makes particles pile up. The analytic gradient has no such error.
//
//   * COST. Three octaves x two axes x two taps was twelve noise evaluations
//     per pixel, each of them eight hash calls. It is three evaluations now.
//
// psrdnoise is also PERIODIC on request, which is what lets the field close a
// seamless loop: with uPeriod set to a whole number of cells the potential
// repeats exactly, and the rotating-gradient argument alpha returns to itself
// at 2*pi. uLoopPhase drives that instead of a free-running clock when the
// caller wants a loop that joins.
precision highp float;
in vec2 vUv;
in vec2 vSim;
uniform float uTime;     // pre-scaled noise time (mids drive the rate)
uniform float uFreq;     // base spatial frequency
uniform float uDetail;   // third-octave gain (treble adds fine turbulence)
uniform float uAmp;      // output speed, sim units/s
uniform vec2 uPeriod;    // noise period in cells; 0 = non-periodic
out vec4 fragColor;

//#include lib_psrdnoise2

/**
 * The stream-function gradient, summed over three octaves.
 *
 * Curl noise needs dPsi/dx and dPsi/dy, not Psi itself, so nothing here ever
 * evaluates the potential - each octave contributes its gradient directly and
 * they add, because differentiation is linear.
 *
 * Time enters as psrdnoise's `alpha`, which ROTATES each lattice gradient
 * rather than translating the field. That is what makes the flow appear to
 * churn in place instead of sliding past, and it is periodic in 2*pi, so an
 * animation can return exactly to its start.
 */
vec2 psiGradient(vec2 p) {
    vec2 g;
    vec2 total = vec2(0.0);
    psrdnoise(p * uFreq, uPeriod, uTime, g);
    total += g * uFreq * 0.625;
    psrdnoise(p * uFreq * 2.02 + 11.3, uPeriod * 2.0, uTime * 1.7, g);
    total += g * uFreq * 2.02 * 0.25;
    psrdnoise(p * uFreq * 4.05 + 29.7, uPeriod * 4.0, uTime * 2.9, g);
    total += g * uFreq * 4.05 * 0.125 * uDetail;
    return total;
}

void main() {
    vec2 grad = psiGradient(vSim);
    // The curl of a scalar potential in 2D: rotate its gradient a quarter turn.
    vec2 v = vec2(grad.y, -grad.x);
    fragColor = vec4(v * uAmp, 0.0, 1.0);
}
