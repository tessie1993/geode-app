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
//#include lib_dmt
//#include lib_bulb

// Bulb Prism: a kaleidoscope of small power-5 bulbs. Space is folded into one
// forty-eighth of itself (abs on every axis, then sorting the coordinates -
// the octahedral mirror group) before a seeded rotation and offset, and a
// shrunken bulb is placed in that wedge, so the mirrors multiply it into a
// cluster of up to forty-eight five-lobed seeds meeting at their creases: a
// crystal grown out of one fractal. Every fold is a reflection and the
// rotation is rigid, so the estimate stays exact up to the bulb's own scale.
//
// Shaded with lib_dmt's jewel material and set against its chrysanthemum sky,
// the one bulb that lives in the visionary-art look the rest of the app's
// marched styles share.
//
// ---- audio ------------------------------------------------------------------
//
// ONSETS re-seed the crystal. Every onset bumps uHitCount, which hashes a new
// rotation and offset for the seed bulb; the onset envelope (uHits.w, 0.22 s)
// glides from the previous seed to the new one, so each attack makes the
// crystal re-facet itself in a quick, smooth lurch and busy passages keep it
// churning. The seed's own spin follows the accumulated rotation uDrift.
// Treble sharpens the jewel's specular. Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the sky's hue drift. Complexity (uMorph) adds up to
// two fractal iterations (bulbIters). Chaos (uWarp, uTurbulence, uTwist) bends
// the seed bulb inside its wedge (bulbChaos, estimate divided by
// bulbChaosLip). Energy (uIntensity) scales how far each onset re-seed moves
// the seed. Colour is pal(); Space is the rig (camAround).

#define BP_MAX_STEPS 128
#define BP_BOUND 1.6
#define BP_CAM_R 3.9
/** The seed bulb is shrunk by this much, so a copy is about 0.5 across. */
#define BP_SCALE 2.4
const float BP_SPIN = 3.0 * CAM_SPIN_UNIT;

mat3 gRot;
vec3 gOff;
mat3 gTurn;
int gIters;
vec4 gTrap;

vec3 seedAngles(float n) {
    return vec3(hash11(n * 0.618 + 0.11), hash11(n * 0.618 + 0.53), hash11(n * 0.618 + 0.97)) - 0.5;
}

float map(vec3 p) {
    p = gTurn * p;
    p = abs(p);
    if (p.x < p.y) p.xy = p.yx;
    if (p.y < p.z) p.yz = p.zy;
    if (p.x < p.y) p.xy = p.yx;
    vec3 q = (gRot * p - gOff) * BP_SCALE;
    vec4 tr;
    float d = bulbDE(bulbChaos(q), 5.0, 5.0, vec2(0.0), gIters, tr) / (bulbChaosLip() * BP_SCALE);
    gTrap = tr;
    return d;
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

void main() {
    vec2 uv = view();
    float swell = clamp(uSwell, 0.0, 1.5);
    float onset = clamp(uHits.w, 0.0, 1.0);
    float glide = smoothstep(0.0, 1.0, 1.0 - onset);
    float en = bulbEnergy();
    vec3 angOld = seedAngles(uHitCount - 1.0);
    vec3 angNew = seedAngles(uHitCount);
    vec3 ang = mix(angOld, angNew, glide) * en;
    gRot = rotX(ang.x) * rotY(ang.y + 0.3 * uDrift) * rotZ(ang.z);
    gOff = vec3(0.58, 0.30, 0.12) + vec3(0.12, 0.10, 0.08) * mix(angOld.zxy, angNew.zxy, glide) * en;
    gTurn = rotY(0.07 * uDrift);
    gIters = bulbIters(6);

    vec3 ro;
    vec3 rd;
    camAround(uv, BP_CAM_R, BP_SPIN, ro, rd);

    float hue = 0.82 + 0.05 * sin(bulbTime() * 0.015);

    float hitT = -1.0;
    float used = 0.0;
    float near = 1e3;
    vec4 trap = vec4(1.0);
    vec2 tb = bulbBound(ro, rd, vec3(0.0), BP_BOUND);
    if (tb.y > 0.0) {
        float t = tb.x;
        for (int i = 0; i < BP_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (t > tb.y) break;
            used = float(i);
            float d = map(ro + rd * t);
            near = min(near, d);
            float eps = bulbEps(t);
            if (d < eps) {
                hitT = t;
                trap = gTrap;
                break;
            }
            t += max(d * 0.9, eps * 0.5);
        }
    }

    vec3 col = dmtChrysanthemum(rd, hue + 0.5, clamp(uEnergySmooth, 0.0, 1.5)) * 0.7;
    col += dmtHalo(near, hue + 0.2, 10.0, 0.18);

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, bulbEps(hitT));
        float ao = bulbStepAO(used) * (0.5 + 0.5 * smoothstep(0.1, 0.9, trap.x));
        float band = 1.2 * sqrt(trap.x) + 0.8 * trap.y;
        col = dmtShade(n, rd, hue, band, ao, smoothstep(0.3, 0.0, trap.z), clamp(uTrebleSmooth, 0.0, 1.5)) * 0.85;
    }

    col *= 0.82 + 0.2 * swell;
    fragColor = vec4(grade(col), 1.0);
}
