#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_arbor

// Arbor Synapse: drifting through a lattice of neurons in a dark void, each
// cell body throwing out eight branching dendrite trees, sparks of signal
// racing outward along them. Raymarched, flown with camFly.
//
// A neuron is a soma plus an octant fold: abs() on all three axes folds space
// into one octant, and a single THREE-armed arborTree grown along that
// octant's diagonal becomes eight dendrite trees, one per octant. Neurons sit
// one per 4-unit cell (the z key wraps at 64 cells, so the lattice repeats
// every 256 units and the rig's 8192 travel wrap is seamless), turned and
// nudged by a cell hash so the lattice never reads as a grid. The signal
// sparks read the fold's own depth record (gArbLevel + gArbAlong) and branch
// id (gArbPath): a spark is a window of that depth sliding outward, and only
// some branches carry one.
//
// ---- audio ------------------------------------------------------------------
//
// Spectral flux (uBandsHi.w) makes the dendrites WRITHE: it adds to the twist
// of every fork, and the twist compounds down the levels, so a busy, changing
// spectrum sets the whole lattice squirming while a held chord lets it hang
// still. The sparks travel on uFlowPhase, the CPU's loudness-rated travel
// clock, so louder passages fire faster but never backwards. Drop (uEvents.y)
// swells every soma on its envelope's bump. Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the sparks' travel and the slow hue drift. Complexity:
// uMorph adds a fork level to every dendrite (arbLevels). Chaos: uTurbulence
// and uWarp scatter each neuron's fork angle and twist from its cell hash;
// uTwist adds a standing spiral. Colour: membranes, somas and sparks from
// pal(). Energy: uIntensity scales the flux writhe and the drop swell. Space:
// camFly, the rig's path.

#define SY_MAX_STEPS 104
#define SY_FAR 18.0
#define SY_CELL 4.0

ArborForm gF;
float gGrow;
float gSoma;
float gChaos;
vec3 gEye;
float gIsSoma;
float gCellH;

/** The octant's diagonal onto +y, as an orthonormal frame (rows). */
const vec3 SY_E1 = vec3(0.70710678, -0.70710678, 0.0);
const vec3 SY_N = vec3(0.57735027, 0.57735027, 0.57735027);
const vec3 SY_E2 = vec3(0.40824829, 0.40824829, -0.81649658);

float map(vec3 p) {
    vec3 c = floor(p / SY_CELL + 0.5);
    ivec3 key = ivec3(int(c.x), int(c.y), int(mod(c.z, 64.0)));
    float h = hashCell(key);
    vec3 q = p - c * SY_CELL;
    q += (vec3(h, fract(h * 5.3), fract(h * 9.1)) - 0.5) * 0.6;
    q = rotY(h * 6.2831) * rotX(fract(h * 3.7) * 3.0) * q;
    float soma = length(q) - gSoma;

    vec3 o = abs(q);
    vec3 b = vec3(dot(o, SY_E1), dot(o, SY_N) - gSoma * 0.6, dot(o, SY_E2));
    ArborForm f = gF;
    f.tilt += (h - 0.5) * 0.3 * gChaos;
    f.twist += (fract(h * 7.1) - 0.5) * 1.5 * gChaos;
    float tree = arborTree(b, f, gGrow);
    float d = smin(soma, tree, 0.12);
    gIsSoma = smoothstep(0.05, -0.05, soma - tree);
    gCellH = h;
    return smax(d, 0.6 - length(p - gEye), 0.3);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** A spark sliding outward along the dendrite, on some branches only. */
float spark(float level, float along, float path, float cellH, float phase) {
    float carries = step(0.55, arbBranchHash(path, 0.0, cellH * 91.0));
    float depth = level + along;
    float w = fract(depth * 0.4 - phase + arbBranchHash(path, 1.0, cellH));
    return carries * smoothstep(0.0, 0.08, w) * smoothstep(0.3, 0.08, w);
}

void main() {
    vec2 uv = arbView();
    float t = uTime * uSpeed;
    float energy = uIntensity;
    float swell = clamp(uSwell, 0.0, 1.5);
    float flux = clamp(uBandsHi.w, 0.0, 1.0) * energy;
    float drop = clamp(uEvents.y, 0.0, 1.0);
    float dropBump = 4.0 * drop * (1.0 - drop) * energy;
    float phase = uFlowPhase * 0.6 * uSpeed;

    gChaos = arbChaos();
    gGrow = arbLevels(5.0, 1.0);
    gSoma = 0.26 + 0.10 * dropBump;
    gF = ArborForm(3.0, 0.52, 0.0, 0.55 + 0.45 * flux + 0.6 * uTwist, 0.64, 0.70, 0.07, 0.64);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    gEye = ro;

    float hue = 0.62 + 0.002 * t;
    vec3 voidCol = pal(hue) * 0.025 + vec3(0.002, 0.003, 0.008);

    float tm = 0.05;
    float hitT = -1.0;
    float used = 0.0;
    float isSoma = 0.0;
    float level = 0.0;
    float along = 0.0;
    float path = 0.0;
    float cellH = 0.0;
    vec3 glow = vec3(0.0);
    vec3 sparkCol = mix(pal(hue + 0.45), vec3(1.0), 0.35);
    for (int i = 0; i < SY_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        if (tm > SY_FAR) break;
        used = float(i);
        float d = map(ro + rd * tm);
        float eps = 0.0008 * tm + 0.0005;
        if (d < eps) {
            hitT = tm;
            isSoma = gIsSoma;
            level = gArbLevel;
            along = gArbAlong;
            path = gArbPath;
            cellH = gCellH;
            break;
        }
        float near = exp(-d * 14.0);
        glow += sparkCol * spark(gArbLevel, gArbAlong, gArbPath, gCellH, phase) * near * 0.03 * (1.0 - gIsSoma);
        glow += pal(hue + 0.15) * gIsSoma * exp(-d * 5.0) * 0.006;
        tm += d * 0.8;
    }

    vec3 col = voidCol;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, 0.001 * hitT + 0.0005);
        vec3 lit = arbLight(n, rd, -rd, 0.0, 20.0);
        float ao = arbStepAO(used, uSteps);
        vec3 membrane = pal(hue + 0.05 * cellH);
        // Translucent membrane: dark facing you, glowing at its rim.
        col = membrane * (0.04 + 0.12 * lit.x) * ao + membrane * lit.y * 0.9;
        vec3 core = mix(pal(hue + 0.15), vec3(1.0, 0.9, 0.95), 0.3);
        col = mix(col, core * (0.35 + 0.5 * (1.0 - lit.y)), isSoma);
        col += sparkCol * spark(level, along, path, cellH, phase) * (1.0 - isSoma) * 1.2;
        col = mix(col, voidCol, 1.0 - exp(-hitT * 0.14));
    }
    col += glow * (0.8 + 0.3 * swell);

    col *= 0.85 + 0.2 * swell;
    col *= 0.75 + 0.25 * smoothstep(2.2, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
