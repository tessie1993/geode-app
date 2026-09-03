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

// Mandala Dome: a tapestry of eight-point stars, rings and diamonds, filled in
// purples, blues and magentas and outlined in green, cyan and red, wrapped
// over two domes bulging in from the sides and a patterned sphere in the
// middle. Mirrored left/right. The flat field behind them recedes upward.
// uv -> mirror -> (dome? sphere map : perspective plane) -> ornament -> colour.

#define DOME_TAU 6.2831853

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

vec2 rot(vec2 p, float a) { return mat2(cos(a), -sin(a), sin(a), cos(a)) * p; }

vec2 sphereUv(vec2 p) {
    vec3 n = vec3(p, sqrt(max(0.0, 1.0 - dot(p, p))));
    return vec2(atan(n.x, n.z), asin(n.y));
}

// Signed distance to an n-point star drawn as a modulated circle.
float star(vec2 l, float n, float r0, float amp) {
    return length(l) - (r0 + amp * cos(n * atan(l.y, l.x)));
}

struct Ornament {
    vec3 fill;
    vec3 line;
};

// One tile of the tapestry: a square frame around one of three motifs - an
// eight-point star in a ring, nested squares around a four-point star, or
// concentric rings - each with a wreath of dots, and a four-point star on
// every corner. The motif and the fills come from the cell's own hash so no
// two neighbours match; lines keep fixed colours so the outline hierarchy
// stays readable. Nothing is left as bare background: the reference has no
// rest.
Ornament ornament(vec2 p, float glow) {
    vec2 cell = floor(p);
    vec2 l = fract(p) - 0.5;
    vec2 corner = fract(p + 0.5) - 0.5;
    float seed = hash21(cell);
    float motif = floor(hash21(cell + 17.3) * 3.0);
    float dFrame = max(abs(l.x), abs(l.y)) - 0.47;
    float dRing = abs(length(l) - 0.38) - 0.012;
    float dStar;
    float dInner;
    if (motif < 1.0) {
        dStar = star(l, 8.0, 0.23, 0.08);
        dInner = star(rot(l, 0.3927), 8.0, 0.09, 0.035);
    } else if (motif < 2.0) {
        float sq = max(abs(l.x), abs(l.y));
        dStar = min(abs(sq - 0.27) - 0.01, abs(sq - 0.19) - 0.01);
        dInner = star(l, 4.0, 0.08, 0.05);
    } else {
        dStar = min(abs(length(l) - 0.26) - 0.01, abs(length(l) - 0.17) - 0.01);
        dInner = length(l) - 0.07;
    }
    float wreathA = atan(l.y, l.x);
    vec2 wreathP = l - 0.31 * vec2(cos(floor(wreathA / DOME_TAU * 16.0 + 0.5) * DOME_TAU / 16.0),
                                   sin(floor(wreathA / DOME_TAU * 16.0 + 0.5) * DOME_TAU / 16.0));
    float dWreath = length(wreathP) - 0.03;
    float dCorner = star(corner, 4.0, 0.12, 0.07);
    float dCornerDot = length(corner) - 0.035;

    // Fills, chosen from three purples/blues per cell.
    vec3 violet = pal(0.20);
    vec3 blue = pal(0.31);
    vec3 magenta = pal(0.12);
    vec3 base = mix(pal(0.25), pal(0.19), seed) * 0.22;
    vec3 outer = mix(mix(violet, blue, step(0.33, seed)), magenta, step(0.66, seed)) * 0.7;
    vec3 fill = base;
    fill = mix(fill, outer, smoothstep(0.01, -0.01, dRing + 0.012));   // inside the ring
    fill = mix(fill, magenta * 0.9, smoothstep(0.01, -0.01, dStar));    // the star
    fill = mix(fill, blue, smoothstep(0.01, -0.01, dInner));            // its heart
    fill = mix(fill, violet * 0.75, smoothstep(0.01, -0.01, dCorner));  // corner stars

    // Lines: cyan frames and rings, green stars, red corner stars, white hearts and dots.
    float w = 0.02;
    float lFrame = smoothstep(w, 0.0, abs(dFrame));
    float lRing = smoothstep(w, 0.0, abs(dRing));
    float lStar = smoothstep(w, 0.0, abs(dStar));
    float lInner = smoothstep(w, 0.0, abs(dInner));
    float lWreath = smoothstep(w, 0.0, dWreath);
    float lCorner = smoothstep(w, 0.0, abs(dCorner));
    float lDot = smoothstep(w, 0.0, dCornerDot);
    vec3 line = pal(0.42) * 0.45 * lFrame + mix(pal(0.5), pal(0.6), 0.5) * 0.9 * lRing + pal(0.64) * lStar +
                pal(0.02) * 0.8 * lCorner + vec3(0.9, 0.8, 1.0) * lInner + pal(0.62) * 1.1 * lWreath + vec3(0.9, 0.95, 1.0) * lDot;
    return Ornament(fill, line * (1.0 + glow));
}

// The tapestry sampled three times with the outline colour split apart, so
// every edge carries the red/green fringe of the reference.
vec3 tapestry(vec2 p, float glow, float split) {
    Ornament o = ornament(p, glow);
    Ornament r = ornament(p * (1.0 + split), glow);
    Ornament b = ornament(p * (1.0 - split), glow);
    vec3 line = vec3(r.line.r, o.line.g, b.line.b);
    return o.fill + line;
}

// A dome: the tapestry seen through a hemisphere at [centre] of radius [r].
vec3 dome(vec2 uv, vec2 centre, float r, float freq, float spin, float glow, float split, out float mask) {
    vec2 p = (uv - centre) / r;
    float rr = length(p);
    mask = smoothstep(1.0, 0.985, rr);
    if (mask <= 0.0) return vec3(0.0);
    vec2 s = rot(sphereUv(p), spin) * freq;
    vec3 col = tapestry(s, glow, split * 0.6);
    vec3 n = vec3(p, sqrt(max(0.0, 1.0 - rr * rr)));
    float limb = 0.45 + 0.55 * n.z;
    float rim = smoothstep(0.8, 1.0, rr);
    float highlight = pow(max(dot(n, normalize(vec3(-0.4, 0.7, 0.6))), 0.0), 24.0);
    col = col * limb + pal(0.5) * rim * 0.5 + vec3(0.8, 0.85, 1.0) * highlight * 0.35;
    return col;
}

void main() {
    vec2 uv = view();
    float beat = clamp(uSpike * uBeatResponse, 0.0, 1.0);
    float breathe = 1.0 + 0.05 * clamp(uBassSmooth * uBeatResponse, 0.0, 1.5);
    // uTreble straight into a glow term was the flash: a cymbal took the whole hall
    // to white on one frame. Both halves are slew-limited now.
    float glow = 0.5 * beat + 0.4 * uTrebleSmooth;
    float split = 0.012 + 0.012 * beat;
    // Integrated spin: energy sets the rate, never the angle.
    float spin = uFlowPhase * 0.5 + uTime * 0.025;
    float finger = touchFalloff(uv, 0.5);

    // Mirror left/right: the reference is a symmetric hall, not a plane.
    vec2 m = vec2(abs(uv.x), uv.y);

    // The flat field: a plane receding toward the top, drifting slowly.
    float persp = 1.0 / (1.15 - 0.35 * m.y);
    vec2 field = m * persp * 3.1 * breathe + flowOffset(0.5);
    vec3 col = tapestry(fluidWarp(field, 0.9, 0.07), glow, split);
    col *= 0.7 + 0.3 * persp;

    // Side domes, then the centre sphere on top.
    float mask;
    vec3 side = dome(m, vec2(1.62, 0.3), 1.0, 2.6 * breathe, spin, glow, split, mask);
    col = mix(col, side, mask);
    vec3 centre = dome(m, vec2(0.0, -0.02), 0.44, 3.4 * breathe, -spin * 1.5, glow + finger, split, mask);
    col = mix(col, centre, mask);

    // A halo under the sphere so it reads as lit from within.
    col += pal(0.5) * 0.12 * exp(-max(length(uv) - 0.44, 0.0) * 6.0) * (0.6 + 0.4 * beat);
    // Motes drifting through the hall, each new spawn fading in over a second.
    col += pal(0.72) * fluidMotes(uv, 5.0, 0.16) * 0.2;
    fragColor = vec4(grade(col), 1.0);
}
