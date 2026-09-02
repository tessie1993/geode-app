#version 300 es
// WATER scene display pass: shades the ripple heightfield as a lit pool.
// Height + 4 neighbors -> surface normal; the normal's gradient refracts a
// procedural depth-graded background (palette-tinted, uDepth scales the
// gradient and absorption); Blinn specular + fresnel rim scaled by
// uSpecular; treble-driven glints on high-curvature crests; the whole
// lookup drifts slowly by uFlowDrift so the pool reads as moving water.
// The refraction offset is kept in lockstep with RippleMath.refractionOffset
// (headless-gate-verified: soft cap 0.08 UV, composite uFlow idiom).
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). Half-float height values exceed that; on GPUs honoring
// sampler precision (Mali) every read clamped and quantized.
precision highp sampler2D;
in vec2 vUv;
uniform sampler2D uHeight;   // R = height (G = velocity, unused here)
uniform highp vec2 uInvRes;  // ripple grid texel size
uniform float uAspect;
uniform float uTime;
uniform float uBaseHue;
uniform float uHueSpan;
uniform float uDepth;        // 0..1 gradient + absorption
uniform float uSpecular;     // 0..1 specular + fresnel gain
uniform float uFlowDrift;    // 0..1 slow uv drift
uniform float uRefract;      // refraction gradient scale
uniform float uTreble;       // treble band for crest glints
uniform float uBrightness;
uniform sampler2D uInk;      // liquid colour film (rgb = colour, a = coverage)
uniform float uInkAmount;    // 0 = plain pool .. 1 = the film IS the surface
out vec4 fragColor;

vec3 hsv(float h, float s, float v) {
    vec3 k = mod(vec3(5.0, 3.0, 1.0) + h * 6.0, 6.0);
    return v - v * s * clamp(min(k, 4.0 - k), 0.0, 1.0);
}

float H(vec2 c) {
    vec2 cc = clamp(c, uInvRes * 0.5, 1.0 - uInvRes * 0.5);
    return texture(uHeight, cc).x;
}

void main() {
    // Slow drift: the surface lookup wanders so ripples + wakes travel.
    // Bounded (sines only, no linear term): H() clamps its lookup into the
    // grid, so a linear uTime term pinned the whole lookup to the border
    // texel within ~2 minutes and the control went dead - and it also kept
    // uTime from wrapping cleanly. All rates are TWO-DECIMAL constants, so
    // every term is exactly periodic at WaterScene.TIME_WRAP_SECONDS (200pi).
    vec2 drift = uFlowDrift * 0.012 *
        vec2(2.5 * sin(uTime * 0.11) + 0.35 * sin(uTime * 0.23), 0.4 * sin(uTime * 0.31));
    vec2 uv = vUv + drift;
    float hC = H(uv);
    float hL = H(uv - vec2(uInvRes.x, 0.0));
    float hR = H(uv + vec2(uInvRes.x, 0.0));
    float hT = H(uv + vec2(0.0, uInvRes.y));
    float hB = H(uv - vec2(0.0, uInvRes.y));

    // Surface normal from the height gradient (z scaled for visible relief).
    vec2 grad = vec2(hR - hL, hT - hB);
    vec3 n = normalize(vec3(-grad * 24.0, 1.0));

    // Refraction offset, lockstep with RippleMath.refractionOffset:
    // gradient * strength, soft-capped at 0.08 UV (composite uFlow idiom).
    vec2 off = grad * uRefract;
    off *= 0.08 / (0.08 + length(off));
    vec2 buv = uv + off;

    // Procedural depth-graded pool: palette-tinted shallow -> deep vertical
    // gradient with a radial deepening, plus faint refracted light bands so
    // the refraction is visible even on a calm surface. uDepth scales both
    // the gradient contrast and the absorption of the water column.
    vec3 shallow = hsv(fract(uBaseHue + 0.04 * uHueSpan), 0.45, 0.95);
    vec3 deep = hsv(fract(uBaseHue + 0.30 * uHueSpan), 0.75, 0.22);
    float depthT = clamp(0.25 + 0.75 * (1.0 - buv.y) * uDepth +
        0.35 * uDepth * length((buv - 0.5) * vec2(uAspect, 1.0)), 0.0, 1.0);
    vec3 bg = mix(shallow, deep, depthT);
    float bands = sin((buv.x * uAspect * 9.0 + buv.y * 4.0) + uTime * 0.35) *
        sin(buv.y * 11.0 - uTime * 0.27);
    bg += (0.06 + 0.05 * uDepth) * max(bands, 0.0) * shallow;
    // Beer-Lambert-ish absorption: deeper water swallows more light.
    bg *= exp(-uDepth * (0.35 + 0.9 * depthT));

    // Liquid film. The ink layer is sampled through the SAME refracted lookup
    // as the pool, so the colour the emitters poured in is bent by the very
    // ripples that are carrying it - the difference between a picture of
    // water and the visual itself having gone liquid. Coverage decides where
    // the film has a body; below it the depth-graded pool still shows through,
    // so a quiet passage drains back to open water instead of a flat wash.
    vec4 film = texture(uInk, buv);
    float cover = clamp(film.a, 0.0, 1.0) * clamp(uInkAmount, 0.0, 1.0);
    // Tone-map the HDR ink before it meets the pool: the splat pass lets rgb
    // run past 1 so bright strikes bloom, and mixing that in raw would clip to
    // white the moment two drops overlapped.
    vec3 liquid = film.rgb / (1.0 + film.rgb);
    bg = mix(bg, liquid, cover);

    // Blinn-Phong specular + fresnel rim, scaled by uSpecular.
    vec3 lightDir = normalize(vec3(-0.4, 0.6, 0.8));
    vec3 halfVec = normalize(lightDir + vec3(0.0, 0.0, 1.0));
    float spec = pow(max(dot(n, halfVec), 0.0), 64.0) * uSpecular;
    float fresnel = pow(1.0 - clamp(n.z, 0.0, 1.0), 3.0) * uSpecular;

    // Treble glint: sparkle on high-curvature crests when the highs bite.
    float curv = abs(hL + hR + hT + hB - 4.0 * hC);
    float glint = uTreble * smoothstep(0.015, 0.12, curv) * 0.9;

    vec3 skyTint = hsv(fract(uBaseHue + 0.55 * uHueSpan), 0.25, 1.0);
    vec3 c = bg + (spec + glint) * vec3(1.0, 0.98, 0.92) + fresnel * skyTint * 0.6;
    fragColor = vec4(c * uBrightness, 1.0);
}
