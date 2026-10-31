#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_arbor

// Arbor Coral: gliding low over a reef of branching coral colonies, polyps
// on every tip, caustic light rippling over everything from the surface far
// above. Raymarched, flown with camFly.
//
// Each colony is a THREE-armed arborTree (polar wedges rather than a mirror)
// with negative gravity, so its branches curl upward toward the light the way
// staghorn coral grows, and a polyp bulb is smooth-unioned onto every tip
// through gArbTip. Colonies sit one to a 4x4 cell of the sea floor, jittered,
// turned and sized by a cell hash; the cell z index wraps at 64, so the reef
// repeats every 256 units and the rig's 8192 travel wrap is seamless. A small
// bubble around the eye is carved out of the field, so a camera path that
// dips into a colony passes through it instead of filling the screen.
//
// ---- audio ------------------------------------------------------------------
//
// Kick (SND_KICK_ENV) is the POLYPS: every tip bulb swells on the hit and shrinks
// back as the envelope falls, the whole reef puffing in time. Drop (SND_HELD_SURGE)
// is a surge of current: on the envelope's bump shape the colonies' twist
// swings and their branches lean downstream, then settle. Low-mid
// (sndSpectrum(0.41)) sharpens the caustic net into thin bright threads. Brightness follows SND_ENERGY_TREND only (the visibility through the water).
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the caustics and the current. Complexity: uMorph adds
// a fold level (arbLevels). Chaos: uTurbulence and uWarp scatter each
// colony's tilt and twist from its cell hash; uTwist adds a standing spiral.
// Colour: coral, polyps, sand and water from pal(). Energy: uIntensity scales
// the polyp swell, the surge and the shimmer. Space: camFly, the rig's path.

#define CR_MAX_STEPS 104
#define CR_FAR 20.0
#define CR_FLOOR -2.7
#define CR_CELL 4.0

ArborForm gF;
float gGrow;
float gPolyp;
float gChaos;
float gSurge;
vec3 gEye;
float gHue;
float gIsPolyp;

float sc0(float h2) {
    return 0.75 + 0.5 * h2;
}

float map(vec3 p) {
    vec2 cell = floor(p.xz / CR_CELL + 0.5);
    vec2 key = vec2(cell.x, mod(cell.y, 64.0));
    float h = hashCell(ivec3(int(key.x), 7, int(key.y)));
    float h2 = fract(h * 7.13 + 0.31);
    vec3 q = p;
    q.xz -= cell * CR_CELL + (vec2(h, h2) - 0.5) * 0.8;
    // The trunk is buried: staghorn forks from the sand, it has no bole.
    q.y -= CR_FLOOR - 0.45 * sc0(h2);
    q.xz = rot2(h * 6.2831) * q.xz;
    float sc = sc0(h2);

    ArborForm f = gF;
    f.tilt += (h - 0.5) * (0.2 + 0.4 * gChaos);
    f.twist += (h2 - 0.5) * 1.2 * gChaos + gSurge * (h - 0.3);
    float tree = arborTree(q / sc, f, gGrow) * sc;
    float polyp = gArbTip * sc - gPolyp * sc;
    float coral = smin(tree, polyp, 0.05 * sc);
    gIsPolyp = smoothstep(0.02, -0.02, polyp - tree);
    gHue = h;

    float sand = p.y - CR_FLOOR - 0.035 * sin(p.x * 4.0 + 2.0 * sin(p.z * 1.3));
    float d = min(coral, sand * 0.9);
    if (sand < coral) gIsPolyp = -1.0;
    return smax(d, 0.7 - length(p - gEye), 0.3);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

void main() {
    vec2 uv = arbView();

    float t = uTime * uSpeed;
    float energy = uIntensity;
    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float kick = clamp(SND_KICK_ENV * energy, 0.0, 1.0);
    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float dropBump = 4.0 * drop * (1.0 - drop) * energy;
    float lowmid = clamp(sndSpectrum(0.41), 0.0, 1.0) * energy;

    gChaos = arbChaos();
    gSurge = 0.9 * dropBump + 0.08 * sin(t * 0.3);
    gGrow = arbLevels(4.0, 1.0);
    gPolyp = 0.022 + 0.035 * kick;
    gF = ArborForm(3.0, 0.72, -0.38, 0.75 + 0.5 * uTwist, 0.80, 0.60, 0.12, 0.80);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    gEye = ro;

    float hue = 0.55 + 0.03 * sin(t * 0.05);
    vec3 water = pal(hue) * 0.18 + vec3(0.0, 0.03, 0.05);
    vec3 fogCol = water * (0.6 + 0.9 * pow(max(rd.y * 0.5 + 0.5, 0.0), 3.0));

    float tm = 0.05;
    float hitT = -1.0;
    float used = 0.0;
    float mat = 0.0;
    float colonyHue = 0.0;
    float glow = 0.0;
    for (int i = 0; i < CR_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        if (tm > CR_FAR) break;
        used = float(i);
        float d = map(ro + rd * tm);
        float eps = 0.0008 * tm + 0.0005;
        if (d < eps) {
            hitT = tm;
            mat = gIsPolyp;
            colonyHue = gHue;
            break;
        }
        // Polyps glow faintly into the water around them.
        glow += max(gIsPolyp, 0.0) * exp(-d * 10.0) * 0.008;
        tm += d * 0.8;
    }

    vec3 col = fogCol;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, 0.0012 * hitT + 0.0006);
        vec3 sun = normalize(vec3(0.2, 1.0, 0.3));
        vec3 lit = arbLight(n, rd, sun, 0.4, 24.0);
        float ao = arbStepAO(used, uSteps);
        // Caustics: two crossed travelling sines, sharpened, from above only.
        float c1 = sin(p.x * 3.7 + sin(p.z * 2.3 + t * 0.9) + t * 0.6);
        float c2 = sin(p.z * 4.1 + sin(p.x * 2.9 - t * 0.7) - t * 0.5);
        float caust = pow(clamp(0.5 + 0.5 * c1 * c2, 0.0, 1.0), 3.0 + 4.0 * lowmid) * max(n.y, 0.0);
        vec3 base;
        if (mat < -0.5) {
            base = mix(vec3(0.55, 0.50, 0.40), pal(hue + 0.1), 0.3) * 0.7;
        } else {
            vec3 body = pal(hue + 0.35 + 0.45 * colonyHue);
            vec3 tipCol = mix(vec3(1.0, 0.9, 0.75), pal(hue + 0.7 + 0.3 * colonyHue), 0.5);
            base = mix(body, tipCol, mat);
        }
        col = base * (0.15 + 0.8 * lit.x) * ao + base * caust * 0.9;
        col += pal(hue + 0.2) * lit.y * 0.2 * ao + vec3(lit.z) * 0.1;
        col += pal(hue + 0.7 + 0.3 * colonyHue) * max(mat, 0.0) * 0.18;
        col = mix(col, fogCol, 1.0 - exp(-hitT * (0.16 - 0.04 * swell)));
    }
    col += pal(hue + 0.6) * glow;
    // Light shafts from the surface: a soft brightening looking up.
    col += water * 0.8 * pow(max(rd.y, 0.0), 2.0) * (0.6 + 0.4 * sin(rd.x * 9.0 + t * 0.4));

    col *= 0.85 + 0.2 * swell;
    col *= 0.75 + 0.25 * smoothstep(2.2, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
