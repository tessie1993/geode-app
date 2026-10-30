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
//#include lib_bulb

// Bulb Ink: the POWER-2 bulb - the lowest power, the one whose pole axis is
// exactly the real line of the Mandelbrot set - drawn as a cartoon.
// Its long axis is the pole (the set runs from c = -2 to 1/4 along it), so it
// stands upright like a whipped spire, cardioid at the foot. Three flat tones
// of cel shading, black ink on the outline and in the creases, a single round
// highlight, on a muted halftone backdrop. No gradients on the body at all.
//
// The outline is found during the march: a ray that MISSES but came within a
// pixel-scaled distance of the surface is on the silhouette, so it is inked.
// That costs nothing - the march was already tracking its closest approach.
//
// ---- audio ------------------------------------------------------------------
//
// The SNARE squashes and stretches the body, cartoon style: each hit (uHits.y)
// flattens it vertically and bulges it sideways, and it springs back as the
// hit decays. The estimate is divided by the larger stretch so the squashed
// march stays safe. Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the hue drift. Complexity (uMorph) adds up to two fractal
// iterations (bulbIters). Chaos (uWarp, uTurbulence, uTwist) warps and twists
// space before the fractal (bulbChaos, estimate divided by bulbChaosLip).
// Energy (uIntensity) scales the snare squash. Colour is pal() throughout; Space is
// the rig (camAround).

#define BI_MAX_STEPS 128
#define BI_BOUND 1.75
#define BI_CAM_R 3.9
/** Fractal-space pole coordinate that lands at the world origin: the middle of the body. */
#define BI_MID -0.85
const float BI_SPIN = 5.0 * CAM_SPIN_UNIT;

vec3 gSquash;
float gLip;
int gIters;
vec4 gTrap;

float map(vec3 p) {
    vec3 q = bulbChaos(p) / gSquash;
    // Upright: the needle (c = -2) up, the cardioid down.
    q = vec3(q.x, -q.y + BI_MID, q.z);
    vec4 tr;
    float d = bulbDE(q, 2.0, 2.0, vec2(0.0), gIters, tr);
    gTrap = tr;
    return d * gLip / bulbChaosLip();
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

void main() {
    vec2 uv = view();
    float swell = clamp(uSwell, 0.0, 1.5);
    float snare = clamp(uHits.y, 0.0, 1.0);
    float s = 0.22 * snare * bulbEnergy();
    gSquash = vec3(1.0 + 0.6 * s, 1.0 - s, 1.0 + 0.6 * s);
    gLip = min(gSquash.x, gSquash.y);
    gIters = bulbIters(8);

    vec3 ro;
    vec3 rd;
    camAround(uv, BI_CAM_R, BI_SPIN, ro, rd);

    float hue = 0.58 + 0.03 * sin(bulbTime() * 0.02);

    float hitT = -1.0;
    float used = 0.0;
    float nearPx = 1e3;
    vec4 trap = vec4(1.0);
    vec2 tb = bulbBound(ro, rd, vec3(0.0), BI_BOUND);
    if (tb.y > 0.0) {
        float t = tb.x;
        for (int i = 0; i < BI_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (t > tb.y) break;
            used = float(i);
            float d = map(ro + rd * t);
            nearPx = min(nearPx, d / t);
            float eps = bulbEps(t);
            if (d < eps) {
                hitT = t;
                trap = gTrap;
                break;
            }
            t += max(d * 0.9, eps * 0.5);
        }
    }

    // Halftone backdrop: dots that grow toward the bottom of the frame.
    vec2 cell = fract(uv * 22.0) - 0.5;
    float dotR = mix(0.12, 0.42, smoothstep(1.0, -1.0, uv.y));
    float dots = smoothstep(dotR, dotR - 0.06, length(cell));
    vec3 paper = pal(hue + 0.5) * 0.10 + vec3(0.05, 0.045, 0.06);
    vec3 col = mix(paper, paper * 1.9, dots);
    vec3 ink = vec3(0.012, 0.01, 0.018);

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, bulbEps(hitT));
        vec3 key = normalize(vec3(0.6, 0.7, -0.4));
        float lit = max(dot(n, key), 0.0) * (0.6 + 0.4 * smoothstep(0.1, 0.8, trap.x));
        // Three tones, hard steps.
        float tone = lit > 0.55 ? 1.0 : (lit > 0.18 ? 0.62 : 0.30);
        vec3 body = pal(hue + 0.18 * floor(trap.y * 5.0) / 5.0);
        col = body * tone;
        float hl = pow(max(dot(reflect(rd, n), key), 0.0), 30.0);
        col = mix(col, vec3(1.0, 0.98, 0.94), step(0.6, hl));
        // Ink: grazing silhouette from inside, and the creases.
        float ndv = dot(n, -rd);
        float edge = step(ndv, 0.24);
        float crease = step(0.62, used / max(uSteps, 1.0));
        col = mix(col, ink, max(edge, crease));
    } else if (nearPx < 0.010) {
        col = ink;
    }

    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
