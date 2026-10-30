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

// Bulb Contour: a power-9 bulb drawn as a hologram - a near-black glass body
// with no shading to speak of, wrapped in two families of thin neon lines:
// contours of the orbit trap (log of its closest approach, so the lines crowd
// into the creases and open out on the lobes) and rings of constant radius
// (a topographic survey of the crust). It stands over a receding neon floor
// grid, synthwave style, in a dark violet void.
//
// Lines are antialiased with fwidth(), which needs derivatives from every
// pixel of a quad, so the line coordinates are computed for all pixels and
// the lines drawn OUTSIDE the hit branch.
//
// ---- audio ------------------------------------------------------------------
//
// The BEAT PHASE moves the lines. Both families scroll by the analyser's
// phase-locked beat phase (uBeatPhase), so the contours flow outward over the
// body once per beat and stay locked to the tempo, wrapping seamlessly; when
// the tempo is lost they keep the last one. The AIR band (uBandsHi.z)
// thickens the lines and spreads their glow, so cymbals and breath fuzz the
// drawing. Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the floor's scroll. Complexity (uMorph) adds up to
// two fractal iterations (bulbIters) and a denser second line family. Chaos
// (uWarp, uTurbulence, uTwist) warps the body (bulbChaos, estimate divided by
// bulbChaosLip). Energy (uIntensity) scales the air's thickening. Colour is
// pal(); Space is the rig (camAround).

#define BW_MAX_STEPS 128
#define BW_BOUND 1.22
#define BW_CAM_R 3.6
#define BW_FLOOR -1.3
const float BW_SPIN = 3.0 * CAM_SPIN_UNIT;

int gIters;
vec4 gTrap;

float map(vec3 p) {
    vec4 tr;
    float d = bulbDE(bulbChaos(p), 9.0, 9.0, vec2(0.0), gIters, tr) / bulbChaosLip();
    gTrap = tr;
    return d;
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

/** A thin line on every integer of f, `w` pixels wide, antialiased. */
float lineOn(float f, float w) {
    float fw = max(fwidth(f), 1e-4);
    float dd = abs(fract(f + 0.5) - 0.5) / fw;
    return 1.0 - smoothstep(0.5 * w, 0.5 * w + 1.0, dd);
}

void main() {
    vec2 uv = view();
    float swell = clamp(uSwell, 0.0, 1.5);
    float air = clamp(clamp(uBandsHi.z, 0.0, 1.0) * bulbEnergy(), 0.0, 1.5);
    float phase = uBeatPhase;
    float dense = 1.0 + clamp(uMorph, 0.0, 1.0);
    gIters = bulbIters(6);

    vec3 ro;
    vec3 rd;
    camAround(uv, BW_CAM_R, BW_SPIN, ro, rd);

    float hue = 0.78 + 0.04 * sin(bulbTime() * 0.017);

    float hitT = -1.0;
    float used = 0.0;
    vec4 trap = vec4(1.0);
    vec2 tb = bulbBound(ro, rd, vec3(0.0), BW_BOUND);
    if (tb.y > 0.0) {
        float t = tb.x;
        for (int i = 0; i < BW_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (t > tb.y) break;
            used = float(i);
            float d = map(ro + rd * t);
            float eps = bulbEps(t);
            if (d < eps) {
                hitT = t;
                trap = gTrap;
                break;
            }
            t += max(d * 0.9, eps * 0.5);
        }
    }

    // Line coordinates, for every pixel (zero where they do not apply).
    float fA = 0.0;
    float fB = 0.0;
    float rim = 0.0;
    float ao = 1.0;
    vec3 p = ro + rd * max(hitT, 0.0);
    if (hitT > 0.0) {
        vec3 n = normalAt(p, bulbEps(hitT));
        rim = pow(1.0 - max(dot(n, -rd), 0.0), 2.5);
        ao = bulbStepAO(used);
        fA = log2(max(trap.x, 1e-4)) * 2.2 - phase;
        fB = length(p) * 16.0 * dense - phase;
    }
    // The floor plane, analytically.
    float fT = rd.y < -1e-3 ? (BW_FLOOR - ro.y) / rd.y : -1.0;
    vec2 fp = ro.xz + rd.xz * max(fT, 0.0);
    fp.y += bulbTime() * 0.4;

    float w = 0.8 + 1.8 * air;
    float lA = lineOn(fA, w);
    float lB = lineOn(fB, w * 0.7);
    float gx = lineOn(fp.x * 2.0, 1.0);
    float gz = lineOn(fp.y * 2.0, 1.0);

    vec3 col = mix(vec3(0.010, 0.004, 0.022), pal(hue + 0.1) * 0.06, smoothstep(-0.2, 0.5, rd.y));
    if (fT > 0.0 && (hitT < 0.0 || fT < hitT)) {
        float fade = exp(-fT * 0.18);
        col += pal(hue + 0.5) * max(gx, gz) * fade * 0.7;
        col += pal(hue + 0.5) * 0.04 * fade;
    }
    if (hitT > 0.0) {
        vec3 glass = vec3(0.012, 0.010, 0.02);
        col = glass + pal(hue + 0.55) * rim * (0.25 + 0.2 * air);
        col += pal(hue) * lA * (0.9 + 0.3 * air) * ao;
        col += pal(hue + 0.33) * lB * 0.55 * ao;
    }

    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
