#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_touch

// Neon Tiles: a mirrored neon tile kaleidoscope with a refractive sphere at
// the centre. mirror x -> kaleidoscope fold -> tiled SDF motifs (star + cross
// on two lattice scales, repeated at three nested scales for the recursive
// edges) -> neon outline -> sphere override -> posterize + bloom.

#define NEON_TAU 6.2831853
#define NEON_LEVELS 7.0
#define NEON_IOR 1.45
#define NEON_RECURSION 3

vec2 kaleido(vec2 p, float n) {
    float w = NEON_TAU / n;
    float a = mod(atan(p.y, p.x), w);
    a = abs(a - 0.5 * w);
    return length(p) * vec2(cos(a), sin(a));
}

float ndot(vec2 a, vec2 b) {
    return a.x * b.x - a.y * b.y;
}

float sdRhombus(vec2 p, vec2 b) {
    p = abs(p);
    float h = clamp(ndot(b - 2.0 * p, b) / dot(b, b), -1.0, 1.0);
    float d = length(p - 0.5 * b * vec2(1.0 - h, 1.0 + h));
    return d * sign(p.x * b.y + p.y * b.x - b.x * b.y);
}

float sdBox2(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float sdStar(vec2 p, float r) {
    return min(sdRhombus(p, vec2(r, r * 0.35)), sdRhombus(p, vec2(r * 0.35, r)));
}

float sdCross(vec2 p, float w, float l) {
    return min(sdBox2(p, vec2(l, w)), sdBox2(p, vec2(w, l)));
}

// x = distance to the nearest motif edge, y = 1 for the star, 0 for the cross.
vec2 tiles(vec2 p, float scale) {
    vec2 c = fract(p * scale) - 0.5;
    float star = sdStar(c, 0.42) / scale;
    float cross = sdCross(c, 0.06, 0.45) / scale;
    return star < cross ? vec2(star, 1.0) : vec2(cross, 0.0);
}

vec2 motif(vec2 p) {
    vec2 a = tiles(p, 3.0);
    vec2 b = tiles(rot2(0.7853982) * p + 0.5, 6.0);
    return a.x < b.x ? a : b;
}

float neon(float d, float w) {
    return smoothstep(w, 0.0, abs(d)) + 0.25 * smoothstep(w * 6.0, 0.0, abs(d));
}

float gWidth;
float gFolds;

vec3 patternOnPlane(vec2 q) {
    vec3 col = vec3(0.0);
    float scale = 1.0;
    for (int i = 0; i < NEON_RECURSION; i++) {
        vec2 m = motif(q);
        float hue = m.y > 0.5 ? mix(0.61, 0.89, fract(0.37 * float(i) + 0.1 * sin(uTime * 0.11))) : 0.33;
        float glow = neon(m.x * scale, gWidth) / (1.0 + 0.6 * float(i));
        col += pal(hue) * glow;
        q = kaleido(q * 1.8 + vec2(0.31, 0.17), gFolds);
        scale *= 1.8;
    }
    return col;
}

vec3 patternOnSphere(vec2 q) {
    return patternOnPlane(q * 1.6 + vec2(0.0, uTime * 0.05));
}

// Returns vec3(-1) outside the sphere.
vec3 sphereLook(vec2 p, float R, float ior, float distort) {
    float r2 = dot(p, p);
    if (r2 > R * R) return vec3(-1.0);
    vec3 n = vec3(p, sqrt(R * R - r2)) / R;
    vec3 v = vec3(0.0, 0.0, -1.0);
    vec3 rf = refract(v, n, 1.0 / ior);
    vec3 rl = reflect(v, n);
    float fres = pow(1.0 - max(dot(n, -v), 0.0), 3.0);
    return mix(patternOnSphere(rf.xy * distort), patternOnSphere(rl.xy), fres);
}

void main() {
    vec2 uv = view();
    float bassA = min(uBass, 1.3);
    float midA = min(uMid, 1.3);
    float trebA = min(uTreble, 1.3);
    float enA = min(uEnergy, 1.3);
    float beatEnv = clamp(uBeat, 0.0, 1.0);
    float beatBump = pow(0.5 + 0.5 * cos(NEON_TAU * uBeatPhase), 2.0);
    float hit = beatEnv * beatEnv * beatBump;

    gWidth = mix(0.012, 0.035, clamp(trebA, 0.0, 1.0));
    gFolds = (uKaleido > 0.5 && uSymmetry >= 2.0) ? uSymmetry : 6.0;

    vec2 m = vec2(abs(uv.x), uv.y);
    m = rot2(uTime * 0.05 * (0.5 + midA)) * m;
    vec2 q = kaleido(m, gFolds);
    vec3 col = patternOnPlane(q + vec2(uTime * 0.03, 0.0));

    float R = 0.55 + 0.12 * hit * clamp(uBeatResponse, 0.0, 2.0);
    float distort = 1.0 + 0.8 * bassA;
    vec3 sphere = sphereLook(uv, R, NEON_IOR, distort);
    if (sphere.x >= 0.0) {
        float edge = smoothstep(R, R - 0.02, length(uv));
        col = mix(col, sphere * 1.15 + pal(0.75) * 0.05, edge);
    }

    col = floor(col * NEON_LEVELS + 0.5) / NEON_LEVELS;
    col += (0.25 + 0.4 * enA) * col * col;
    if (!touchIdle()) {
        col += pal(0.5 + 0.15 * sin(uTime * 0.05)) * min(touchWake(uv), 3.0) * 0.05;
    }
    col *= 0.7 + 0.3 * smoothstep(2.2, 0.5, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
