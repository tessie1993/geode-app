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

// Rod Tunnel: a spiral tunnel of bead-chain rods flying toward a bright core.
// The camera sits on the axis of a cylinder looking down +z; the wall repeats
// in angle (strands) and depth (cell), the angle twists with depth, and each
// cell holds one capsule whose radius is modulated into beads.

#define ROD_TAU 6.2831853
/** Compile-time ceiling on the march: MarchBudget.MAX_STEPS. uSteps is the runtime budget. */
#define ROD_MAX_STEPS 128
#define ROD_CELL 1.0
#define ROD_RADIUS 1.6
#define ROD_FOCAL 1.3
#define ROD_FAR 26.0
#define ROD_STRANDS 14.0
#define ROD_FOG 0.11

float gTwist;
float gCellA;
float gCellZ;

float sdCapsule(vec3 p, vec3 a, vec3 b, float r) {
    vec3 pa = p - a;
    vec3 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - r;
}

float map(vec3 p) {
    float rad = length(p.xy);
    float ang = atan(p.y, p.x) + p.z * gTwist;
    float sector = ROD_TAU / ROD_STRANDS;
    gCellA = floor(ang / sector);
    ang = mod(ang, sector) - 0.5 * sector;
    gCellZ = floor(p.z / ROD_CELL);
    float z = mod(p.z, ROD_CELL) - 0.5 * ROD_CELL;
    vec3 q = vec3(ROD_RADIUS - rad, ang * ROD_RADIUS, z);
    float beads = 0.03 * sin(q.z * 40.0);
    return sdCapsule(q, vec3(0.0, 0.0, -0.4), vec3(0.0, 0.0, 0.4), 0.14 + beads);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

void main() {
    vec2 uv = view();
    float bassA = min(uBass, 1.3);
    float midA = min(uMid, 1.3);
    float trebA = min(uTreble, 1.3);
    float enA = min(uEnergy, 1.3);
    float beatEnv = clamp(uBeat, 0.0, 1.0);
    float beatBump = pow(0.5 + 0.5 * cos(ROD_TAU * uBeatPhase), 2.0);
    float hit = beatEnv * beatEnv * beatBump;

    gTwist = 0.25 + 0.55 * midA;
    float fly = uTime * (1.4 + 2.2 * enA);
    vec3 ro = vec3(0.25 * sin(uTime * 0.31), 0.25 * cos(uTime * 0.23), fly);
    vec3 rd = normalize(vec3(uv, ROD_FOCAL));
    rd.xy = rot2(uTime * 0.08) * rd.xy;

    float t = 0.02;
    float hitT = -1.0;
    for (int i = 0; i < ROD_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        if (t > ROD_FAR) break;
        vec3 p = ro + rd * t;
        float d = map(p);
        float eps = 0.0008 * t + 0.0004;
        if (d < eps) {
            hitT = t;
            break;
        }
        // The angular repeat and the twist stretch space, so the estimate can
        // overshoot; 0.6 of it keeps every step inside the true clearance.
        t += max(d * 0.6, eps);
    }

    float hueShift = 0.12 * hit * clamp(uBeatResponse, 0.0, 2.0);
    vec3 coreCol = pal(0.08 + hueShift);
    vec3 fogCol = pal(0.62 + hueShift) * 0.08;
    float onAxis = pow(max(rd.z, 0.0), 28.0);
    float tGlow = hitT > 0.0 ? hitT : ROD_FAR;

    vec3 col;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        map(p);
        float cellA = gCellA;
        float cellZ = gCellZ;
        float e = max(0.0012 * hitT, 0.0005);
        vec3 n = normalAt(p, e);
        float band = hash11(cellA * 7.13 + cellZ * 3.71);
        vec3 body = pal(band * 0.5 + hueShift + 0.015 * cellZ);
        vec3 rimCol = pal(band * 0.5 + 0.3 + hueShift);
        vec3 fromAxis = normalize(vec3(p.xy - ro.xy, 0.0));
        float dif = clamp(dot(n, -fromAxis), 0.0, 1.0);
        float rim = pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 3.0);
        float spec = pow(clamp(dot(n, normalize(vec3(0.0, 0.0, 1.0) - rd)), 0.0, 1.0), 30.0 + 60.0 * trebA);
        col = body * (0.15 + 0.85 * dif);
        col += rimCol * rim * (0.5 + 0.6 * trebA);
        col += mix(vec3(1.0), rimCol, 0.4) * spec * 0.4;
        col = mix(col, fogCol, 1.0 - exp(-hitT * ROD_FOG));
    } else {
        col = fogCol;
    }
    col += coreCol * (0.6 + 0.9 * bassA) * onAxis * 2.0 / (1.0 + 0.02 * tGlow * tGlow);
    col += coreCol * 0.06 * (0.6 + 0.9 * bassA) / (1.0 + dot(uv, uv) * 3.0);

    if (!touchIdle()) {
        col += pal(0.5 + 0.15 * sin(uTime * 0.05)) * min(touchWake(uv), 3.0) * 0.05;
    }
    col *= 0.7 + 0.3 * smoothstep(2.2, 0.5, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
