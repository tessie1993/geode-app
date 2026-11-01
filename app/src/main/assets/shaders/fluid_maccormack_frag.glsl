#version 300 es
// MacCormack advection, the corrector pass (Selle, Fedkiw, Kim, Liu, Rossignac
// 2008, "An Unconditionally Stable MacCormack Method"). The predictor is the
// plain semi-Lagrangian pass, fluid_advect_frag with no decay, written to
// uForward. Here, per texel x:
//
//   forward  = the source at x - dt u(x)        (recomputed: same four taps)
//   backward = uForward at x + dt u(x)          (trace the prediction back)
//   result   = forward + (source(x) - backward) / 2
//
// i.e. the round trip's error is measured and half of it removed, which
// cancels the first-order smearing that made the old solver's vortices go soft
// within a second. The result is clamped to the min and max of the four source
// taps the forward trace interpolated between - the "unconditionally stable"
// part: the correction can sharpen an edge, never ring past it into negative
// or over-bright dye. FluidMath::macCormack is the same line, host-tested.
//
// The decay is applied here, once, after the clamp, so the predictor and the
// corrector see the same undecayed field.
precision highp float;
precision highp sampler2D;
in vec2 vUv;
uniform sampler2D uVelocity;
uniform sampler2D uSource;    // the field at time n
uniform sampler2D uForward;   // the predictor's result
uniform vec2 uSrcInvRes;
uniform vec2 uVelInvRes;
uniform float uDt;
uniform float uRdx;
uniform vec3 uDecay;
out vec4 fragColor;

vec4 bilerp(sampler2D t, vec2 uv, vec2 inv) {
    vec2 st = uv / inv - 0.5;
    vec2 i = floor(st);
    vec2 f = fract(st);
    vec4 a = texture(t, (i + vec2(0.5, 0.5)) * inv);
    vec4 b = texture(t, (i + vec2(1.5, 0.5)) * inv);
    vec4 c = texture(t, (i + vec2(0.5, 1.5)) * inv);
    vec4 d = texture(t, (i + vec2(1.5, 1.5)) * inv);
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    vec2 vel = texture(uVelocity, vUv).xy;
    if (isnan(vel.x) || isnan(vel.y) || isinf(vel.x) || isinf(vel.y)) vel = vec2(0.0);
    vec2 step = uDt * uRdx * vel * uVelInvRes;

    vec2 st = (vUv - step) / uSrcInvRes - 0.5;
    vec2 i = floor(st);
    vec2 f = fract(st);
    vec4 a = texture(uSource, (i + vec2(0.5, 0.5)) * uSrcInvRes);
    vec4 b = texture(uSource, (i + vec2(1.5, 0.5)) * uSrcInvRes);
    vec4 c = texture(uSource, (i + vec2(0.5, 1.5)) * uSrcInvRes);
    vec4 d = texture(uSource, (i + vec2(1.5, 1.5)) * uSrcInvRes);
    vec4 forward = mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
    vec4 lo = min(min(a, b), min(c, d));
    vec4 hi = max(max(a, b), max(c, d));

    vec4 backward = bilerp(uForward, vUv + step, uSrcInvRes);
    vec4 here = texture(uSource, vUv);
    vec4 s = clamp(forward + 0.5 * (here - backward), lo, hi);
    s.rgb /= uDecay;
    if (any(isnan(s)) || any(isinf(s))) s = vec4(0.0);
    fragColor = s;
}
