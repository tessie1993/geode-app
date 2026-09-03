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

// Chroma Orb: a glass sphere skinned with a four-fold kaleidoscope of neon dot
// lattices, a ringed tunnel bored through its centre, and the whole thing
// drawn three times at slightly different scales so every dot carries a
// green core with blue and orange fringes. Around it, a dark green field of
// soft bokeh. uv -> sphere map -> fold -> lattices (+ tunnel inside the
// window) -> RGB split -> palette.

#define ORB_TAU 6.2831853
#define ORB_RADIUS 0.94
#define ORB_WINDOW 0.45

vec2 rot(vec2 p, float a) { return mat2(cos(a), -sin(a), sin(a), cos(a)) * p; }

vec2 sphereUv(vec2 p) {
    vec3 n = vec3(p, sqrt(max(0.0, 1.0 - dot(p, p))));
    return vec2(atan(n.x, n.z), asin(n.y));
}

vec2 fold(vec2 p, float n) {
    float w = ORB_TAU / n;
    float a = mod(atan(p.y, p.x), w);
    a = abs(a - 0.5 * w);
    return length(p) * vec2(cos(a), sin(a));
}

float disc(vec2 g, float radius) { return smoothstep(radius, radius - 0.06, length(g)); }

// Three square lattices of discs, the second offset half a cell and the third
// turned 45 degrees, so the rows read as woven rather than as one grid.
float lattice(vec2 p, float pulse) {
    float a = disc(fract(p * 3.4) - 0.5, 0.30 + 0.05 * pulse);
    float b = disc(fract(p * 6.2 + 0.5) - 0.5, 0.25 + 0.04 * pulse);
    float c = disc(fract(rot(p, 0.7854) * 11.0) - 0.5, 0.17);
    return a + 0.75 * b + 0.3 * c;
}

// The tunnel through the window: rings of discs in (angle, log depth) space,
// so they shrink toward the centre and drift outward as time runs.
float tunnel(vec2 p, float folds, float pulse) {
    vec2 q = fold(p, folds);
    float ang = atan(q.y, q.x);
    float depth = log(ORB_WINDOW / max(length(p), 1e-4));
    float ring = depth * 3.2 - uFlowPhase * 1.5;
    // Alternate rings are offset half a dot so the dots pack like a honeycomb
    // instead of lining up into spokes.
    float stagger = 0.5 * mod(floor(ring), 2.0);
    vec2 cell = vec2(ang / ORB_TAU * 20.0 + stagger, ring);
    float rings = disc(fract(cell) - 0.5, 0.33 + 0.05 * pulse);
    // The dark diamond the rings converge on.
    float hole = smoothstep(0.035, 0.09, q.x + q.y);
    float fog = exp(-depth * 0.45) * hole;
    return rings * fog;
}

// The user's fold count when the kaleidoscope is on; four arms otherwise,
// which is what puts the diamond through the middle.
float foldCount() { return (uKaleido > 0.5 && uSymmetry >= 2.0) ? uSymmetry : 4.0; }

float skin(vec2 p, float folds, float spin, float pulse) {
    float rr = length(p);
    vec2 s = fold(rot(sphereUv(p), spin), folds);
    // The fold's mirror lines stay visible as dark seams: the X through the
    // sphere in the reference is the kaleidoscope showing its joints.
    float wedge = 0.5 * ORB_TAU / folds;
    float a = atan(s.y, s.x);
    float seam = smoothstep(0.0, 0.05, a) * smoothstep(0.0, 0.05, wedge - a);
    float dots = lattice(s * 1.6, pulse) * (0.3 + 0.7 * seam);
    float window = smoothstep(ORB_WINDOW + 0.05, ORB_WINDOW - 0.05, rr);
    // A dark ring marks the window's edge, as a bored hole would.
    float lip = 1.0 - 0.6 * smoothstep(0.06, 0.0, abs(rr - ORB_WINDOW - 0.02));
    return mix(dots, tunnel(p, folds, pulse), window) * lip;
}

vec3 chroma(vec2 p, float folds, float spin, float pulse, float split) {
    // Blue is the biggest copy, orange the smallest: the fringe order of a lens.
    return vec3(skin(p * (1.0 + split), folds, spin, pulse),
                skin(p, folds, spin, pulse),
                skin(p * (1.0 - split), folds, spin, pulse));
}

vec3 bokeh(vec2 uv) {
    vec3 field = pal(0.66) * 0.09;
    vec2 g = uv * 3.0 + flowOffset(0.4);
    vec2 cell = floor(g);
    vec2 l = fract(g) - 0.5;
    float seed = fract(sin(dot(cell, vec2(127.1, 311.7))) * 43758.5);
    l += 0.18 * vec2(cos(seed * 40.0), sin(seed * 27.0));
    float blob = smoothstep(0.3, 0.04, length(l)) * (0.25 + 0.75 * seed);
    return field + pal(0.62) * blob * 0.16 * (0.6 + 0.4 * uEnergySmooth);
}

void main() {
    vec2 uv = view();
    // Smoothed bass breathes the lattice; uSpike swells the fringes. Neither can
    // move far in one frame, which is what keeps the dots from strobing.
    float pulse = clamp(uBassSmooth * uBeatResponse, 0.0, 1.5);
    float beat = clamp(uSpike * uBeatResponse, 0.0, 1.0);
    float folds = foldCount();
    // Integrated, so loudness changes the spin RATE instead of jumping its angle.
    float spin = uFlowPhase * 0.55 + uTime * 0.03;
    float split = 0.035 + 0.03 * beat;
    float finger = touchFalloff(uv, 0.45);

    // The sphere fits the shorter axis, so a portrait phone shows the whole
    // orb in its field instead of a crop of its middle.
    vec2 p = uv / (ORB_RADIUS * min(1.0, uResolution.x / uResolution.y));
    float rr = length(p);
    vec3 col = bokeh(uv);
    // Halo the sphere sits in; wider on a hit.
    col += pal(0.62) * 0.14 * exp(-max(rr - 1.0, 0.0) * (9.0 - 3.0 * beat));

    // The particle layer, outside the glass only, drifting with the field.
    col += pal(0.55) * fluidMotes(uv * 0.7, 4.5, 0.17) * 0.15 * smoothstep(0.9, 1.3, rr);

    if (rr < 1.0) {
        vec3 rgb = chroma(p, folds, spin, pulse + finger, split + 0.03 * finger);
        vec3 green = mix(pal(0.62), pal(0.55), 0.3);
        vec3 blue = pal(0.33);
        vec3 orange = pal(0.94);
        // Green is the core of every dot; blue and orange are strongest where
        // the green copy has ended, which is what a lens fringe is, but keep
        // some of each under the core so the overlaps go cyan and yellow as
        // three offset copies really do.
        float core = clamp(rgb.g, 0.0, 1.0);
        float under = 1.0 - 0.6 * core;
        vec3 dots = green * core * 1.0 + blue * rgb.b * under * 0.9 + orange * rgb.r * under * 0.85;
        dots = min(dots, vec3(1.15));
        // Glass: limb darkening, a soft top-left highlight, a bright rim.
        vec3 n = vec3(p, sqrt(max(0.0, 1.0 - rr * rr)));
        vec3 light = normalize(vec3(-0.45, 0.6, 0.66));
        float limb = 0.5 + 0.5 * n.z;
        float highlight = pow(max(dot(n, light), 0.0), 28.0);
        float rim = smoothstep(0.86, 1.0, rr);
        vec3 sphere = pal(0.62) * 0.04 + dots * limb;
        sphere += vec3(0.25, 0.45, 0.35) * highlight * 0.25;
        sphere += pal(0.55) * rim * (0.25 + 0.3 * beat);
        float edge = smoothstep(1.0, 0.985, rr);
        col = mix(col, sphere, edge);
    }

    fragColor = vec4(grade(col), 1.0);
}
