#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_palette
//#include lib_scene_grade
//#include lib_touch

// Blacklight Bloom: a dense mirrored tapestry in violet and magenta, the kind
// that looks painted in UV ink - every edge outlined in cyan and green because
// the whole thing is drawn three times a hair apart. Flat spiral discs and
// wire tetrahedra float in front of it, and a soap bubble sits in the middle
// with the tapestry refracting through it.
//
// uv -> kaleidoscope fold -> folded-plane tapestry -> floaters -> bubble.
//
// The tapestry itself is a 2D fold-and-scale iteration (the flat cousin of a
// KIFS), so the pattern repeats at every scale the way the reference does,
// without a raymarch: this stays off MARCHED_SCENES.
//
// Audio: uSwell breathes the fold scale, uBassSmooth the ink weight,
// uTrebleSmooth the outline separation. A spike picks a new fold count
// (uFormPhase), re-seeds which floaters are present (uSpawnSeed) and re-aims
// the drift (uMoveDir). No term steps, and nothing keys brightness off a raw
// envelope.

#define BB_TAU 6.2831853
#define BB_ITERS 6

vec2 rot(vec2 p, float a) { return mat2(cos(a), -sin(a), sin(a), cos(a)) * p; }

float hash11(float n) { return fract(sin(n * 127.1) * 43758.5453); }

vec2 kaleido(vec2 p, float n) {
    float w = BB_TAU / max(n, 2.0);
    float a = mod(atan(p.y, p.x), w);
    a = abs(a - 0.5 * w);
    return length(p) * vec2(cos(a), sin(a));
}

// The tapestry: fold the plane into the positive quadrant, scale up, repeat.
// The orbit trap - how close the point ever came to the fold corner - is what
// turns the iteration into line work rather than into a blur.
float tapestry(vec2 p, float scale, float turn) {
    float trap = 1e9;
    float w = 1.0;
    for (int i = 0; i < BB_ITERS; i++) {
        p = abs(p) - 0.62;
        p = rot(p, turn);
        p *= scale;
        w *= scale;
        // Distance to the nearest axis, in the ORIGINAL scale, so a deep
        // iteration draws a thin line and not a thick one.
        trap = min(trap, min(abs(p.x), abs(p.y)) / w);
    }
    return smoothstep(0.035, 0.0, trap);
}

// A flat spiral disc, seen face on: an Archimedean arm wound a few turns.
float spiralDisc(vec2 p, float radius, float turns) {
    float rr = length(p);
    if (rr > radius) return 0.0;
    float a = atan(p.y, p.x);
    float arm = fract((rr / radius) * turns - a / BB_TAU);
    float line = smoothstep(0.16, 0.0, min(arm, 1.0 - arm));
    return line * smoothstep(radius, radius * 0.7, rr);
}

// A wire triangle, for the tetrahedra tumbling through the field.
float wireTri(vec2 p, float size, float width) {
    float best = 1e9;
    for (int i = 0; i < 3; i++) {
        float a = float(i) * (BB_TAU / 3.0);
        float b = a + BB_TAU / 3.0;
        vec2 v = vec2(cos(a), sin(a)) * size;
        vec2 e = vec2(cos(b), sin(b)) * size - v;
        float t = clamp(dot(p - v, e) / max(dot(e, e), 1e-6), 0.0, 1.0);
        best = min(best, length(p - v - e * t));
    }
    return smoothstep(width, width * 0.2, best);
}

// The ink, drawn three times a hair apart. Green sits under the core, cyan and
// magenta fringe it: that offset trio is where the blacklight look comes from.
vec3 ink(vec2 p, float scale, float turn, float split) {
    float g = tapestry(p, scale, turn);
    float c = tapestry(p * (1.0 + split), scale, turn);
    float m = tapestry(p * (1.0 - split), scale, turn);
    vec3 col = pal(0.62) * g;
    col += pal(0.46) * c * (1.0 - 0.55 * g);
    col += pal(0.88) * m * (1.0 - 0.55 * g);
    return col;
}

void main() {
    vec2 uv = view();

    float bass = clamp(uBassSmooth, 0.0, 1.5);
    float treb = clamp(uTrebleSmooth, 0.0, 1.5);
    float swell = clamp(uSwell, 0.0, 1.5);
    // Kaleidoscope arms: the user's when they have set one, otherwise the
    // plateau the last spike chose. Held, not stepped per beat.
    float folds = (uKaleido > 0.5 && uSymmetry >= 2.0) ? uSymmetry : 6.0 + 2.0 * floor(uFormPhase * 3.0);
    // The fold scale is what decides how deep the repeat reads. Kept inside
    // 1.28..1.42: past that the iteration outruns the trap and the tapestry
    // turns to noise.
    float scale = 1.30 + 0.10 * swell;
    float split = 0.014 + 0.016 * clamp(treb, 0.0, 1.0);

    // The plane, advected through the curl field so the whole tapestry
    // breathes like ink in water rather than sitting still.
    vec2 q = kaleido(uv, folds);
    q = fluidWarp(q * (1.0 + 0.05 * bass), 1.1, 0.10) + flowOffset(0.22);

    vec3 col = ink(q, scale, 0.42 + 0.08 * sin(uTime * 0.07), split);
    // A dim violet wash under the ink, so the black is never flat black.
    col += pal(0.70) * 0.05 * (0.5 + 0.5 * swell);

    // ---- floaters -----------------------------------------------------------
    //
    // Spiral discs and wire tetrahedra in front of the tapestry. Which ones
    // exist is re-rolled on a spike, and each fades in over its own second.
    for (int i = 0; i < 6; i++) {
        float fi = float(i);
        float seed = hash11(fi * 5.17 + floor(uSpawnSeed * 53.0));
        float depth = 0.55 + seed * 0.8;
        vec2 c = uv - (vec2(hash11(seed * 3.1), hash11(seed * 7.9)) * 2.2 - 1.1);
        c -= flowOffset(0.18) * (0.5 + seed);
        c += 0.06 * vec2(sin(uTime * (0.13 + seed * 0.09)), cos(uTime * (0.11 + seed * 0.07)));
        c = rot(c, uFlowPhase * (0.4 + seed * 0.5)) / depth;
        float show = spawnGrow(0.7 + seed * 0.8);
        vec3 tint = mix(pal(0.44), pal(0.92), seed);
        if (seed < 0.5) {
            float s = spiralDisc(c, 0.22, 3.0 + floor(seed * 6.0));
            col += mix(vec3(1.0), tint, 0.45) * s * 0.55 * show;
        } else {
            float t = wireTri(rot(c, 0.4), 0.24, 0.02);
            col += tint * t * 0.6 * show;
            // A pale face under the wire, so it reads as a solid facet.
            col += tint * 0.10 * smoothstep(0.26, 0.05, length(c)) * show;
        }
    }

    // ---- the bubble ---------------------------------------------------------
    //
    // A sphere in the middle that refracts the tapestry: the coordinate is
    // pushed outward by the surface normal, which is all a thin lens does.
    float R = 0.46 + 0.03 * swell;
    float rr = length(uv);
    if (rr < R) {
        vec3 n = vec3(uv / R, sqrt(max(0.0, 1.0 - (rr * rr) / (R * R))));
        // Refraction: bend harder toward the limb, which is where n.z is small.
        vec2 bent = kaleido(uv + n.xy * (0.30 + 0.10 * bass) * (1.0 - n.z), folds);
        bent = fluidWarp(bent, 1.4, 0.08) + flowOffset(0.22);
        vec3 inside = ink(bent, scale, 0.42, split * 1.8);
        // Thin-film colour: the interference band walks with the view angle,
        // which is what makes a bubble iridescent rather than merely shiny.
        vec3 film = pal(fract(0.2 + n.z * 0.8 + uFormPhase * 0.3));
        inside = inside * 1.15 + film * 0.10 * (1.0 - n.z);
        // Rim and highlight.
        float rim = smoothstep(0.86, 1.0, rr / R);
        inside += pal(0.50) * rim * (0.30 + 0.35 * uSpike);
        inside += vec3(0.9, 0.95, 1.0) * pow(max(dot(n, normalize(vec3(-0.4, 0.6, 0.7))), 0.0), 30.0) * 0.30;
        col = mix(col, inside, smoothstep(1.0, 0.97, rr / R));
    }

    // ---- the particle layer -------------------------------------------------
    col += mix(pal(0.46), vec3(1.0), 0.35) * fluidMotes(uv, 5.0, 0.15) * 0.24;

    if (!touchIdle()) {
        col += pal(0.5 + 0.15 * sin(uTime * 0.05)) * min(touchWake(uv), 3.0) * 0.05;
    }
    col *= 0.70 + 0.30 * smoothstep(2.2, 0.5, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
