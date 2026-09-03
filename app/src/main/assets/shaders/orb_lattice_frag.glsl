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

// Orb Lattice: a spherical kaleidoscope of neon dot lattices with RGB fringes
// over a dark green field. uv -> sphere map -> kaleidoscope fold -> three dot
// lattices -> RGB split -> palette.

#define ORB_TAU 6.2831853

vec2 sphereUv(vec2 p) {
    vec3 n = vec3(p, sqrt(max(0.0, 1.0 - dot(p, p))));
    return vec2(atan(n.x, n.z), asin(n.y));
}

vec2 kaleido(vec2 p, float n) {
    float w = ORB_TAU / n;
    float a = mod(atan(p.y, p.x), w);
    a = abs(a - 0.5 * w);
    return length(p) * vec2(cos(a), sin(a));
}

float dots(vec2 uv, float scale, float radius) {
    vec2 g = fract(uv * scale) - 0.5;
    return smoothstep(radius, radius - 0.04, length(g));
}

float pattern(vec2 p) {
    return dots(p, 6.0, 0.30) + 0.6 * dots(p, 11.0, 0.22) + 0.4 * dots(p, 19.0, 0.16);
}

vec3 rgbSplit(vec2 p, float k) {
    return vec3(pattern(p * (1.0 - k)), pattern(p), pattern(p * (1.0 + k)));
}

// The user's fold count when the kaleidoscope is on; otherwise the arm count
// the last spike chose.
//
// This used to step 8 -> 6 -> 4 as the beat envelope decayed, which meant the
// whole kaleidoscope re-folded twice on the way down from every hit - three
// different pictures per beat, none of them held. uFormPhase is a plateau: a
// spike picks the next arm count and it stays there until the next one.
float foldCount() {
    if (uKaleido > 0.5 && uSymmetry >= 2.0) return uSymmetry;
    return 4.0 + 2.0 * floor(uFormPhase * 3.0);
}

vec3 latticeColour(vec2 q, float split, float glow) {
    vec3 rgb = rgbSplit(q, split);
    vec3 col = rgb.g * pal(0.55 + 0.08 * sin(q.x * 0.7 + uTime * 0.2));
    col += rgb.b * pal(0.68) * 0.7;
    col += rgb.r * (1.0 - rgb.g) * pal(0.02) * 0.8;
    return col * glow;
}

void main() {
    vec2 uv = view();
    // The slew-limited companions, not the raw envelopes: a band that jumps 0 -> 1
    // between two frames used to take the zoom, the fringe width and the glow with it.
    float bassA = min(uBassSmooth, 1.3);
    float trebA = min(uTrebleSmooth, 1.3);
    float enA = min(uEnergySmooth, 1.3);
    // uSpike rather than the beat-phase bump: it rises over ~120ms, so the accent
    // swells instead of flashing on one frame.
    float hit = uSpike;

    float folds = foldCount();
    float split = mix(0.01, 0.04, clamp(trebA, 0.0, 1.0));
    float zoomPulse = 1.0 + 0.12 * bassA * uBeatResponse;
    vec2 p = uv / zoomPulse;
    float r2 = dot(p, p);

    vec3 col = vec3(0.0, 0.08, 0.04);
    // Advected along the direction the last spike aimed at, through the integrated
    // travel phase. `uTime * rate` would teleport the field whenever the rate moved.
    vec2 fieldQ = kaleido(fluidWarp(uv * 0.55, 1.1, 0.09) + flowOffset(0.35), folds);
    col += latticeColour(fieldQ, split * 0.5, 0.18) * smoothstep(0.9, 1.6, length(uv));

    if (r2 < 1.0) {
        vec2 s = sphereUv(p) + flowOffset(0.9) + vec2(0.0, 0.02 * sin(uTime * 0.17));
        vec2 q = kaleido(s * 1.25, folds);
        // The new spawn grows its offset in over a second rather than cutting to it.
        q += 0.12 * vec2(sin(uTime * 0.19), cos(uTime * 0.23));
        q += 0.18 * (uSpawnSeed - 0.5) * spawnGrow(1.1) * uMoveDir;
        float rim = sqrt(1.0 - r2);
        vec3 orb = latticeColour(q, split, 0.35 + 0.65 * rim);
        orb += pal(0.5) * pow(rim, 6.0) * 0.08;
        orb *= 1.0 + 0.25 * hit;
        col = mix(col, orb, smoothstep(1.0, 0.97, r2));
    }

    col += enA * 0.35 * col * col;
    // The particle half: motes riding the same curl field the lattice is warped by.
    col += pal(0.55) * fluidMotes(uv * 0.8, 5.0, 0.16) * 0.22;
    if (!touchIdle()) {
        col += pal(0.5 + 0.15 * sin(uTime * 0.05)) * min(touchWake(uv), 3.0) * 0.05;
    }
    col *= 0.6 + 0.4 * smoothstep(2.2, 0.5, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
