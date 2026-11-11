#version 300 es
// The feedback pass (core/viz/liquid/LiquidPass): Liquid mode's liquid, and with Liquid off the flat
// styles' dive and a shader style's Trails.
//
// One pass, two targets. location 0 is the liquid itself, carried frame to frame (half float where
// the GPU renders it, so a slow fade does not band); location 1 is what the composite shows, the
// liquid with its Surface lit on top, so the lighting never feeds back into the liquid.
//
// Per pixel:
//   1. back-trace: MilkDrop's per-frame UV transform (lib_feedback: zoom, stretch, warp, rotation,
//      translation; LiquidMath.hpp says why the dive is not a force in the solver), then the flow
//      field's velocity, folded back at the edges;
//   2. read the liquid there, sharpened against the bilinear blur that a magnifying feedback adds
//      every frame, clamped to its neighbours so the sharpening can never ring or run away;
//   3. age it (Colour age);
//   4. pour the style back in: Liquidity sets how much, and the style's own edges get more, so its
//      outlines stay readable while its fills melt;
//   5. the born disc at the vanishing point: a whole frame (the live style, a re-roll of it, the
//      next style) squeezed into a small disc, or ink blooms in the style's palette, which the dive
//      then swells outward. That is how new visuals form inside the liquid.
precision highp float;
precision highp sampler2D;

in vec2 vUv;
uniform sampler2D uPrev;    // the liquid, last frame
uniform sampler2D uStyle;   // this frame's style
uniform sampler2D uBorn;    // what forms inside (uStyle itself for Born = Live)
uniform sampler2D uVel;     // the flow field's velocity (FluidSim grid units)
uniform vec2 uVelScale;     // uv per (velocity * second): rdx / grid size, per axis
uniform vec2 uTexel;        // one liquid texel
uniform vec2 uStyleTexel;   // one style texel
uniform float uDt;
uniform float uAspect;
uniform vec2 uVp;           // vanishing point, centred units (x times the aspect, y -1..1): the born disc
uniform float uInject;     // share of the style poured back this frame
uniform float uEdge;        // extra pour on the style's edges, 0..1
uniform float uSharpen;     // 0 off
uniform float uBornR;       // the born disc's radius, centred units
uniform float uBornGain;    // 0..1, dips while the born look is swapped
uniform int uBornMode;      // 0 frame from uBorn, 3 ink forms
uniform int uAge;           // 0 pure, 1 fade deep, 2 ink bleed
uniform int uSurface;       // 0 ink, 1 wet gloss, 2 glass
uniform int uFeedback;      // 0: the warp fallback, nothing carried over
uniform float uShimmer;     // the highs, 0..1
uniform float uLight;      // the sound's energy, 0..1, lights the surface
uniform float uTime;

layout(location = 0) out vec4 outLiquid;
layout(location = 1) out vec4 outDisplay;

//#include lib_feedback

vec2 centred(vec2 uv) { return (uv * 2.0 - 1.0) * vec2(uAspect, 1.0); }
vec2 uncentred(vec2 c) { return (c / vec2(uAspect, 1.0)) * 0.5 + 0.5; }
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

vec3 hueRotate(vec3 c, float a) {
    // Rotation about the grey axis: keeps luminance roughly, turns the hue.
    const vec3 k = vec3(0.57735);
    float ca = cos(a);
    return c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - ca);
}

void main() {
    vec2 c = centred(vUv);
    vec2 src = uvFor(frameMotion(), vUv, uAspect, uTime);
    vec2 vel = texture(uVel, fold(src)).xy;
    if (any(isnan(vel)) || any(isinf(vel))) vel = vec2(0.0);
    src -= uDt * vel * uVelScale;
    if (uShimmer > 0.001) {
        vec2 q = src * vec2(uAspect, 1.0) * 38.0 + uTime * 0.9;
        src += (vec2(noise(q), noise(q + 17.3)) - 0.5) * uShimmer * 3.0 * uTexel;
    }
    // Zooming out, a twist or the flow reads past the frame's edge. Clamping there repeats the edge
    // texel as stripes; folding the frame back on itself continues the picture with no seam.
    src = fold(src);
    src = clamp(src, uTexel * 0.5, 1.0 - uTexel * 0.5);

    vec3 prev;
    vec3 n0;
    vec3 n1;
    vec3 n2;
    vec3 n3;
    if (uFeedback == 1) {
        prev = texture(uPrev, src).rgb;
        n0 = texture(uPrev, src + vec2(uTexel.x, 0.0)).rgb;
        n1 = texture(uPrev, src - vec2(uTexel.x, 0.0)).rgb;
        n2 = texture(uPrev, src + vec2(0.0, uTexel.y)).rgb;
        n3 = texture(uPrev, src - vec2(0.0, uTexel.y)).rgb;
        if (uSharpen > 0.0) {
            vec3 sharp = prev + uSharpen * (4.0 * prev - n0 - n1 - n2 - n3) * 0.25;
            vec3 lo = min(min(min(n0, n1), min(n2, n3)), prev);
            vec3 hi = max(max(max(n0, n1), max(n2, n3)), prev);
            prev = clamp(sharp, lo, hi);
        }
        if (uAge == 1) {
            float dist = length(c - uVp);
            prev *= max(0.0, 1.0 - uDt * mix(0.1, 1.4, smoothstep(0.25, 1.3, dist)));
        } else if (uAge == 2) {
            vec2 along = normalize(vel + vec2(1e-5)) * uTexel * 1.5;
            prev = vec3(texture(uPrev, src + along).r, prev.g, texture(uPrev, src - along).b);
            prev = max(hueRotate(prev, uDt * 0.5), 0.0);
        }
    } else {
        // The warp: the style itself, bent by the flow; nothing kept.
        prev = texture(uStyle, src).rgb;
        n0 = texture(uStyle, src + vec2(uStyleTexel.x, 0.0)).rgb;
        n1 = texture(uStyle, src - vec2(uStyleTexel.x, 0.0)).rgb;
        n2 = texture(uStyle, src + vec2(0.0, uStyleTexel.y)).rgb;
        n3 = texture(uStyle, src - vec2(0.0, uStyleTexel.y)).rgb;
    }

    vec3 liquid = prev;
    float r = uBornR;
    float bornMask = uBornGain * (1.0 - smoothstep(r * 0.5, r, length(c - uVp)));
    if (uFeedback == 1) {
        vec3 style = texture(uStyle, vUv).rgb;
        float gx = luma(texture(uStyle, vUv + vec2(uStyleTexel.x, 0.0)).rgb) - luma(texture(uStyle, vUv - vec2(uStyleTexel.x, 0.0)).rgb);
        float gy = luma(texture(uStyle, vUv + vec2(0.0, uStyleTexel.y)).rgb) - luma(texture(uStyle, vUv - vec2(0.0, uStyleTexel.y)).rgb);
        float edge = clamp(length(vec2(gx, gy)) * 3.0, 0.0, 1.0);
        // Where the style is brighter than the liquid it is re-poured too, so a thin lit shape (a branch,
        // an outline) is never averaged away into its dark surroundings, while bright liquid still
        // smears freely over the style's dark.
        float lift = clamp((luma(style) - luma(prev)) * 2.5, 0.0, 1.0);
        float w = uInject + (1.0 - uInject) * max(edge, lift) * uEdge;
        liquid = mix(prev, style, w * (1.0 - bornMask));
        if (bornMask > 0.001) {
            vec2 bc = (c - uVp) / (2.0 * r);
            vec3 born;
            if (uBornMode == 3) {
                // Ink forms: blooms in the style's own palette, read from points circling its frame.
                vec2 q = bc * 5.0;
                float f = noise(q + uTime * 0.35) * 0.6 + noise(q * 2.1 - uTime * 0.5) * 0.4;
                float a = f * 6.2831853 + uTime * 0.2;
                vec3 ink = texture(uStyle, 0.5 + 0.38 * vec2(cos(a), sin(a))).rgb;
                born = ink * smoothstep(0.35, 0.75, f) * 1.4;
            } else {
                // The disc shows the born frame's central circle, whatever the screen's shape.
                born = texture(uBorn, clamp(uncentred(bc * 2.0 * min(uAspect, 1.0)), 0.0, 1.0)).rgb;
            }
            liquid = mix(liquid, born, bornMask);
        }
    }
    liquid = clamp(liquid, 0.0, 64.0);
    outLiquid = vec4(liquid, 1.0);

    vec3 shown = liquid;
    if (uSurface != 0) {
        float hx = luma(n0) - luma(n1);
        float hy = luma(n2) - luma(n3);
        vec3 nrm = normalize(vec3(-hx * 2.5, -hy * 2.5, 1.0));
        vec3 l = normalize(vec3(cos(uTime * 0.37), sin(uTime * 0.29), 1.3));
        float spec = pow(max(dot(reflect(-l, nrm), vec3(0.0, 0.0, 1.0)), 0.0), 28.0);
        if (uSurface == 1) {
            shown = liquid * (0.88 + 0.12 * max(dot(nrm, l), 0.0)) + spec * (0.25 + 0.55 * uLight);
        } else {
            vec3 bent = uFeedback == 1 ? texture(uPrev, vUv + nrm.xy * 0.02).rgb : texture(uStyle, vUv + nrm.xy * 0.02).rgb;
            float lap = luma(n0 + n1 + n2 + n3) - 4.0 * luma(prev);
            float caustic = clamp(lap * 4.0, 0.0, 1.0);
            float rim = pow(1.0 - nrm.z, 2.0);
            shown = mix(liquid, bent, 0.65) * (1.0 + caustic * 0.5) + rim * 0.25 + spec * (0.15 + 0.35 * uLight);
        }
    }
    outDisplay = vec4(clamp(shown, 0.0, 1.0), 1.0);
}
