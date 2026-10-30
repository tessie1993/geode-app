#version 300 es
#define VIEW_OWNS_SHAPE
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
//#include lib_bulb

// Bulb Slice: a power-10 bulb cut open like a specimen. A plane facing the
// eye removes the near half, so the frame is mostly the CUT FACE: the inside
// of the set, which no surface render ever shows, painted as a scan - a heat
// map of the orbit trap with contour lines of depth below the skin, like an
// MRI slice through a fossil. What is left of the outer surface is matte
// white clay, so the colour lives only on the cut. Behind it, a dark lab
// slate with a faint measuring grid.
//
// The plane is an intersection (max with the bulb's estimate), which is
// 1-Lipschitz, so the march needs no extra care. A hit belongs to the cut
// face when the plane term is the one that won the max.
//
// ---- audio ------------------------------------------------------------------
//
// The KICK drives the knife. Each kick (uHits.x) pushes the slice plane deeper
// into the body and it drifts back out as the kick decays, so every beat
// reveals a new section of the interior, and the heat map re-forms under it.
// The plane's tilt wanders on uOrbit, re-targeted on section boundaries.
// Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the knife's breathing. Complexity (uMorph) adds up to two fractal
// iterations (bulbIters). Chaos (uWarp, uTurbulence, uTwist) warps and twists
// space before the fractal (bulbChaos, estimate divided by bulbChaosLip).
// Energy (uIntensity) scales the kick's push on the knife. Colour is pal() throughout; Space is
// the rig (camAround).

#define BS_MAX_STEPS 128
#define BS_BOUND 1.22
#define BS_CAM_R 3.1
const float BS_SPIN = 3.0 * CAM_SPIN_UNIT;

vec3 gN;
float gH;
int gIters;
vec4 gTrap;
float gBulbD;

float map(vec3 p) {
    vec4 tr;
    float db = bulbDE(bulbChaos(p), 10.0, 10.0, vec2(0.0), gIters, tr) / bulbChaosLip();
    gTrap = tr;
    gBulbD = db;
    return max(db, dot(p, gN) - gH);
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

void main() {
    vec2 uv = view();
    float swell = clamp(uSwell, 0.0, 1.5);
    float kick = clamp(uHits.x, 0.0, 1.0);
    gIters = bulbIters(6);

    vec3 ro;
    vec3 rd;
    camAround(uv, BS_CAM_R, BS_SPIN, ro, rd);

    // The knife faces the eye, tilted off it so a sliver of the outer
    // surface shows as a rim around the cut.
    gN = normalize(normalize(ro) + vec3(0.35 * uOrbit.x, 0.28 + 0.2 * uOrbit.y, 0.0));
    gH = 0.32 + 0.06 * sin(bulbTime() * 0.13) - 0.5 * kick * bulbEnergy();

    float hue = 0.02 + 0.05 * uOrbit.y;

    float hitT = -1.0;
    float used = 0.0;
    float cutW = 0.0;
    float bulbD = 0.0;
    vec4 trap = vec4(1.0);
    vec2 tb = bulbBound(ro, rd, vec3(0.0), BS_BOUND);
    if (tb.y > 0.0) {
        float t = tb.x;
        for (int i = 0; i < BS_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (t > tb.y) break;
            used = float(i);
            vec3 p = ro + rd * t;
            float d = map(p);
            float eps = bulbEps(t);
            if (d < eps) {
                hitT = t;
                trap = gTrap;
                bulbD = gBulbD;
                cutW = step(gBulbD, dot(p, gN) - gH);
                break;
            }
            t += max(d * 0.9, eps * 0.5);
        }
    }

    // Lab slate and a faint millimetre grid in screen space.
    vec2 g = abs(fract(uv * 6.0) - 0.5);
    float grid = smoothstep(0.47, 0.5, max(g.x, g.y));
    vec3 col = vec3(0.020, 0.026, 0.034) + vec3(0.02, 0.03, 0.04) * grid;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 key = normalize(vec3(-0.4, 0.8, -0.45));
        if (cutW > 0.5) {
            // The scan. Depth below the skin in contour lines, the trap as heat.
            float depth = max(-bulbD, 0.0);
            float heat = clamp(1.6 * sqrt(trap.x) + 0.8 * trap.y, 0.0, 3.0);
            vec3 base = pal(hue + 0.35 * heat);
            float iso = abs(fract(depth * 26.0) - 0.5);
            float line = smoothstep(0.08, 0.0, iso) * smoothstep(0.0, 0.02, depth);
            float rimGlow = exp(-depth * 40.0);
            col = base * (0.55 + 0.25 * smoothstep(0.0, 0.3, depth));
            col = mix(col, col * 0.35, line * 0.7);
            col += pal(hue + 0.6) * rimGlow * 0.35;
            // A thin sheen so the face still reads as a plane in the light.
            col *= 0.85 + 0.15 * max(dot(gN, key), 0.0);
        } else {
            vec3 n = normalAt(p, bulbEps(hitT));
            float dif = max(dot(n, key), 0.0);
            float fill = 0.5 + 0.5 * n.y;
            float ao = bulbStepAO(used) * (0.5 + 0.5 * smoothstep(0.1, 0.9, trap.x));
            vec3 clay = mix(vec3(0.80, 0.77, 0.72), pal(hue + 0.1), 0.12);
            col = clay * (0.10 + 0.75 * dif + 0.22 * fill) * ao;
        }
    }

    col *= 0.82 + 0.2 * swell;
    fragColor = vec4(grade(col), 1.0);
}
