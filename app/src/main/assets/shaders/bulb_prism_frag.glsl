#version 300 es
#define VIEW_OWNS_SHAPE
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
//#include lib_dmt
//#include lib_bulb

// Bulb Prism: six small power-5 jewels hung at the corners of an octahedron,
// floating apart round an empty centre. Space is folded into one forty-eighth
// of itself (abs on every axis, then sorting the coordinates - the octahedral
// mirror group) and a shrunken bulb is placed ON the fold's x axis, so the
// mirrors make exactly six copies, one per axis, each mirrored through its
// own axis into a symmetric gem. The copies are far enough apart not to
// touch: the one Bulb style that is a constellation rather than one body,
// with the jewel sky showing between the stones. (It used to sit the seed
// off-axis, where the 48 copies fused into one lumpy ball that read as
// Classic in another colour.) Every fold is a reflection and the rotation is
// rigid, so the estimate stays exact up to the bulb's own scale.
//
// Shaded with lib_dmt's jewel material and set against its chrysanthemum sky,
// the one bulb that lives in the visionary-art look the rest of the app's
// marched styles share.
//
// ---- audio ------------------------------------------------------------------
//
// ONSETS re-cut the jewels. Every onset bumps floor(SND_HIT_TIME), which hashes a new
// rotation for the seed bulb inside its gem; the onset envelope (SND_ONSET_ENV,
// 0.22 s) glides from the previous cut to the new one, so each attack makes
// all six stones re-facet in a quick, smooth lurch and busy passages keep
// them churning. Bass (sndShaped(SND_LOW)) breathes the constellation open: the
// gems drift out along their axes and back. The seed's own spin follows the accumulated rotation uDrift.
// Treble sharpens the jewel's specular. Brightness follows SND_INTENSITY only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the sky's hue drift. Complexity (uMorph) adds up to
// two fractal iterations (bulbIters). Chaos (uWarp, uTurbulence, uTwist) bends
// the seed bulb inside its wedge (bulbChaos, estimate divided by
// bulbChaosLip). Energy (uIntensity) scales how far each onset re-seed moves
// the seed. Colour is pal(); Space is the rig (camAround).

#define BP_MAX_STEPS 128
#define BP_BOUND 1.75
#define BP_CAM_R 4.1
/** The seed bulb is shrunk by this much, so a gem is about 0.75 across. */
#define BP_SCALE 3.0
/** How far out along its axis a gem sits at rest; bass pushes it further. */
#define BP_REACH 0.95
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
    vec3 q = gRot * (p - gOff) * BP_SCALE;
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
    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float onset = clamp(SND_ONSET_ENV, 0.0, 1.0);
    float glide = smoothstep(0.0, 1.0, 1.0 - onset);
    float en = bulbEnergy();
    vec3 angOld = seedAngles(floor(SND_HIT_TIME) - 1.0);
    vec3 angNew = seedAngles(floor(SND_HIT_TIME));
    vec3 ang = mix(angOld, angNew, glide) * en;
    gRot = rotX(ang.x) * rotY(ang.y + 0.3 * uDrift) * rotZ(ang.z);
    // On the x axis, so the mirrors make six gems, not forty-eight fused ones.
    gOff = vec3(BP_REACH + 0.22 * clamp(sndShaped(SND_LOW), 0.0, 1.5) * en, 0.0, 0.0);
    gTurn = rotY(0.07 * uDrift) * rotX(0.6);
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

    vec3 col = dmtChrysanthemum(rd, hue + 0.5, clamp(((SND_BASS_PRESENCE + SND_MID_PRESENCE + SND_TREBLE_PRESENCE) / 3.0), 0.0, 1.5)) * 0.7;
    col += dmtHalo(near, hue + 0.2, 10.0, 0.18);

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, bulbEps(hitT));
        float ao = bulbStepAO(used) * (0.5 + 0.5 * smoothstep(0.1, 0.9, trap.x));
        float band = 1.2 * sqrt(trap.x) + 0.8 * trap.y;
        col = dmtShade(n, rd, hue, band, ao, smoothstep(0.3, 0.0, trap.z), clamp(SND_TREBLE_PRESENCE, 0.0, 1.5)) * 0.85;
    }

    col *= 0.82 + 0.2 * swell;
    fragColor = vec4(grade(col), 1.0);
}
