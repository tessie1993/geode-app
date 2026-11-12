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
//#include lib_bulb

// Bulb Prism: six small power-5 jewels hung at the corners of an octahedron,
// floating apart round an empty centre. Space is folded into one forty-eighth
// of itself (abs on every axis, then sorting the coordinates - the octahedral
// mirror group) and a shrunken bulb is placed ON the fold's x axis, so the
// mirrors make exactly six copies, one per axis, each mirrored through its
// own axis into a symmetric gem. The copies are far enough apart not to
// touch: the one Bulb style that is a constellation rather than one body,
// with black sky showing between the stones. Every fold is a reflection and
// the rotation is rigid, so the estimate stays exact up to the bulb's own
// scale.
//
// Grid cell WHOLE x FLAT TRAP (see lib_bulb). The gems are STAINED GLASS: no
// light, no normal, no specular. The orbit trap is quantised into six flat
// panes of the member's own rainbow ramp, each pane edged with a black lead
// line where the trap crosses a pane boundary, and only the free step-count
// occlusion darkens the deepest cuts. Six flat-coloured mosaics on black read
// apart from every lit bulb even desaturated: hard flat cells, no gradient.
// (It used to share lib_dmt's lit jewel material with Julia.) Skipping the
// normal also makes it the cheapest whole-body bulb: four fractal
// evaluations fewer per hit pixel.
//
// ---- audio ------------------------------------------------------------------
//
// ONSETS re-cut the jewels. Every onset bumps floor(SND_HIT_TIME), which hashes a new
// rotation for the seed bulb inside its gem; the onset envelope (SND_ONSET_ENV,
// 0.22 s) glides from the previous cut to the new one, so each attack makes
// all six stones re-facet in a quick, smooth lurch and busy passages keep
// them churning. Bass (sndShaped(SND_LOW)) breathes the constellation open: the
// gems drift out along their axes and back. The seed's own spin follows the accumulated rotation uDrift.
// Treble (SND_TREBLE_PRESENCE) backlights the glass: the panes brighten and the leads
// thin. Brightness follows SND_INTENSITY only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed scales the panes' hue drift. Complexity (uMorph) adds up to
// two fractal iterations (bulbIters). Chaos (uWarp, uTurbulence, uTwist) bends
// the seed bulb inside its wedge (bulbChaos, estimate divided by
// bulbChaosLip). Energy (uIntensity) scales how far each onset re-seed moves
// the seed. Colour is tone(), the stained-glass ramp pal() tints; Space is
// the rig (camAround), its radius scaled by k5 SPACE; the other knobs as lib_bulb.

#define BP_MAX_STEPS 128
#define BP_BOUND 1.75
#define BP_CAM_R 4.1
/** The seed bulb is shrunk by this much, so a gem is about 0.75 across. */
#define BP_SCALE 3.0
/** How far out along its axis a gem sits at rest; bass pushes it further. */
#define BP_REACH 0.95
/** Panes per unit of the trap coordinate. */
#define BP_PANES 6.0
const float BP_SPIN = 3.0 * CAM_SPIN_UNIT;

mat3 gRot;
vec3 gOff;
mat3 gTurn;
int gIters;
vec4 gTrap;

// Its own ramp: a full, saturated rainbow, the cathedral-window colours.
vec3 tone(float t) {
    return bulbRamp(t, vec3(0.5), vec3(0.5), vec3(1.0), vec3(0.00, 0.33, 0.67));
}

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

void main() {
    vec2 uv = view();
    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float onset = clamp(SND_ONSET_ENV, 0.0, 1.0);
    float glide = smoothstep(0.0, 1.0, 1.0 - onset);
    float en = bulbEnergy();
    float treble = clamp(SND_TREBLE_PRESENCE, 0.0, 1.5);
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
    camAround(uv, BP_CAM_R * (1.0 + 0.25 * bulbKnob(5)), BP_SPIN, ro, rd);

    float hue = 0.04 * sin(bulbTime() * 0.015);

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

    // Black sky, and a thin lead outline round each stone's silhouette.
    vec3 col = vec3(0.006, 0.006, 0.010);
    col = mix(col, vec3(0.0), (1.0 - smoothstep(0.0, 0.03, near)) * 0.8);

    // The pane coordinate for every pixel (misses carry the default trap):
    // fwidth needs all four pixels of a quad, so it is taken outside the hit
    // branch, as Contour does.
    float band = BP_PANES * (1.2 * sqrt(trap.x) + 0.8 * trap.y);
    float fw = clamp(fwidth(band), 1e-3, 0.2);
    if (hitT > 0.0) {
        // One flat colour per pane: the trap coordinate quantised.
        float pane = floor(band);
        vec3 glass = tone(hue + pane / BP_PANES + 0.37 * hash11(pane * 0.731));
        // Lead between panes, about two pixels wide, thinner under treble.
        float edge = abs(fract(band) - 0.5);
        float leadW = 0.5 - mix(2.2, 1.2, clamp(treble, 0.0, 1.0)) * fw;
        float lead = smoothstep(leadW - fw, leadW, edge);
        float ao = bulbStepAO(used);
        col = glass * (0.78 + 0.22 * treble) * ao;
        col = mix(col, vec3(0.01), lead);
    }

    col *= 0.82 + 0.2 * swell;
    fragColor = vec4(grade(col), 1.0);
}
