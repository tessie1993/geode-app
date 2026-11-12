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

// Bulb Julia: a power-6 Julia bulb - the orbit starts at the point and adds a
// FIXED c - so the body is not the one bulb but a family of them, one per c,
// and walking c sculpts it continuously: lobes bud, fuse, hollow and pinch
// off.
//
// Grid cell MACRO x FLAT TRAP (see lib_bulb). The eye sits close over one
// flank of the body and looks into it, so the frame is crust from edge to
// edge, and the crust is painted with the orbit trap alone, with no light at
// all: the smooth, enamel-like colour fields of a trap render, where colour
// is how close each orbit came to a point, a plane and the pole axis. Only
// the free step-count occlusion darkens the deepest pits. Where Prism
// quantises its trap into hard panes with lead between them, Julia keeps
// every gradient continuous: soft flat colour, no edge. (It used to share
// lib_dmt's lit jewel material with Prism.) No normal: four fractal
// evaluations fewer per pixel, which pays for the seven iterations.
//
// ---- audio ------------------------------------------------------------------
//
// PITCH walks c. The detected key (uKeyHue, a pitch class eased over 3 s) is
// an angle on a closed loop of c values, so a key change carries the body
// round the loop to a new form and a song that sits in one key holds one
// sculpture. Each pitch change (SND_HELD_NOTE_CHANGE) kicks c off the loop sideways on a
// bump envelope, so melody makes the surface flinch and settle. Treble
// (sndShaped(SND_HIGH)) pushes the colour fields apart, a wider sweep of the
// ramp. Brightness follows SND_LEVEL only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed scales the drift of c round its loop. Complexity (uMorph) adds up to two fractal
// iterations (bulbIters). Chaos (uWarp, uTurbulence, uTwist) warps and twists
// space before the fractal (bulbChaos, estimate divided by bulbChaosLip);
// turbulence also wobbles c itself.
// Energy (uIntensity) scales the pitch kick on c. Colour is tone(), the enamel
// ramp pal() tints. Space is the rig: travel turns the eye slowly round the
// body, look and shake ride on top (bulbAim), the dolly moves it in.

#define BJ_MAX_STEPS 128
#define BJ_BOUND 1.4
/** The eye's distance from the centre, and the off-centre point it looks at. */
#define BJ_EYE_R 1.55
#define BJ_AIM_R 0.45
const float BJ_SPIN = 2.0 * CAM_SPIN_UNIT;

vec3 gC;
int gIters;

// Its own ramp: jade, teal and a citrus yellow.
vec3 tone(float t) {
    return bulbRamp(t, vec3(0.45, 0.58, 0.42), vec3(0.40, 0.35, 0.35), vec3(1.0, 1.0, 0.5), vec3(0.25, 0.10, 0.60));
}

/** The style's body: its distance and orbit trap (mat unused). */
BulbHit bulbMap(vec3 p) {
    vec4 tr;
    float d = bulbJuliaDE(bulbChaos(p), gC, 6.0, gIters, tr) / bulbChaosLip();
    return BulbHit(d, tr, 0.0);
}

//#include lib_bulb_field

void main() {
    vec2 uv = view();
    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    float pe = clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0);
    float pitchBump = 4.0 * pe * (1.0 - pe);
    float treble = clamp(sndShaped(SND_HIGH), 0.0, 1.5);

    // The loop of c: a tilted figure-of-eight in the horizontal plane, |c|
    // between 0.45 and 0.6, where power-6 Julia bulbs stay connected.
    float a = BULB_TAU * uKeyHue + 0.025 * bulbTime();
    gC = vec3(0.5 * cos(a), 0.12 + 0.22 * sin(2.0 * a), 0.5 * sin(a));
    gC += pitchBump * 0.12 * bulbEnergy() * vec3(-sin(a), 0.7, cos(a));
    // Chaos also wobbles c itself.
    gC += bulbSpan(bulbIn().stir, 0.0, 0.0, 0.08) * sin(vec3(1.3, 1.7, 2.3) * bulbTime());
    gIters = bulbIters(7);

    // MACRO: close over one flank, looking in past the centre.
    float e = uCamTravel * BJ_SPIN + 2.2;
    vec3 eye = normalize(vec3(cos(e), -0.35, sin(e))) * BJ_EYE_R * (1.0 - 0.12 * uCamDolly);
    vec3 aim = vec3(BJ_AIM_R * cos(e + 1.6), 0.25, BJ_AIM_R * sin(e + 1.6));
    vec3 ro;
    vec3 rd;
    bulbAim(uv, eye, aim, ro, rd);

    float hue = 0.05 * sin(bulbTime() * 0.017);

    float hitT = -1.0;
    float used = 0.0;
    float near = 1e3;
    vec4 trap = vec4(1.0);
    vec2 tb = bulbBound(ro, rd, vec3(0.0), BJ_BOUND);
    if (tb.y > 0.0) {
        float t = tb.x;
        for (int i = 0; i < BJ_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (t > tb.y) break;
            used = float(i);
            BulbHit h = bulbField(ro + rd * t);
            float d = h.d;
            near = min(near, d);
            float eps = bulbEps(t);
            if (d < eps) {
                hitT = t;
                trap = h.trap;
                break;
            }
            t += max(d * 0.85, eps * 0.5);
        }
    }

    // A flat deep ground of the ramp's far end, and a soft halo of it where a
    // ray only grazed the body.
    vec3 col = tone(hue + 0.62) * 0.08;
    col += tone(hue + 0.4) * exp(-max(near, 0.0) * 8.0) * 0.18;

    if (hitT > 0.0) {
        // Three traps, three fields, blended: no light, only the orbit.
        float spread = 0.9 + 0.5 * treble;
        float fPoint = sqrt(trap.x);
        float fPlane = smoothstep(0.0, 0.6, trap.y);
        float fAxis = (1.0 - smoothstep(0.0, 0.35, trap.z));
        // Escape colour (lib_shade): the smoothed escape count moves the field further along the ramp.
        vec3 field = tone(hue + spread * (0.8 * fPoint + 0.35 * trap.w) + shadeEscape(trap.w));
        field = mix(field, tone(hue + 0.5 + 0.3 * fPlane), 0.35 * (1.0 - fPlane));
        field = mix(field, vec3(1.0, 0.97, 0.88), 0.45 * fAxis);
        float ao = bulbStepAO(used) * (0.55 + 0.45 * smoothstep(0.05, 0.7, trap.x));
        col = field * ao;
    }
    // The step glow (lib_shade): a halo where a ray crept along the body, by Bloom.
    col += tone(hue + 0.4) * 0.4 * shadeStepGlow(used, uSteps);

    col *= 0.8 + 0.22 * swell;
    fragColor = vec4(grade(col), 1.0);
}
