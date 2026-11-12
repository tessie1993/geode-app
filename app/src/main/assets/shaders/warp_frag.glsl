#version 300 es
precision highp float;
precision highp sampler2D;

// Morph, Warp and Turbulence are spent on the streak field below, so view()
// must not bend the screen a second time.
#define VIEW_OWNS_SHAPE

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3

// Warp: the jump to hyperspace, flown for real. The 2D style drew star
// streaks racing out of a vanishing point; here every streak is a glowing
// capsule laid along the travel axis, standing in a grid of columns around an
// open corridor, and the rig flies down that corridor while the streaks race
// past it toward the eye. Perspective makes the vanishing point; nothing is
// faked in screen space. Each streak keeps the old identity: a hashed palette
// colour, and a brightness read from one band of the spectrum, so the field
// lights up as an equaliser seen edge-on. Far away the streaks fade into a
// glow at the end of the corridor, the destination of the jump.
//
// The camera is the shared rig (camFly). The world is a grid of 2 x 2 columns
// in xy; along z each column is cut into 8-unit cells whose hash repeats every
// 128 cells, so the world tiles every 1024 world units. 1024 divides the rig's
// 8192 travel wrap, so the wrap is invisible, and main() rebases the eye into
// 0..1024 for precision. Every other z term (the bend) has a period dividing
// 1024.
//
// ---- audio ------------------------------------------------------------------
//
// Throttle (SND_ENERGY_TREND with sndShaped(SND_LEVEL)) stretches every streak:
//   louder passages read as faster, the motion blur of the 2D throttle.
// The streaks travel toward the eye on uFlowTime, whose rate loudness sets and
//   which never runs backwards, each column at its own hashed speed.
// Kick (SND_KICK_ENV) stretches the streaks just ahead of the eye.
// Drop (SND_HELD_SURGE) is the jump: every streak lengthens and the corridor's
//   end flares, on a bump so it glides in and out.
// Per streak, sndEq at its hashed band sets the brightness, as in the 2D.
// Pitch change (SND_HELD_NOTE_CHANGE) slides the field's hue.
// SND_ENERGY_TREND sets the overall level.
// The mids (SND_MIDDLE) turn the eye's look aside while the mids climb and ease
//   it back as they fall, as lib_terra's terraTurn and lib_truchet's
//   truchetTurn do: 0.1 x sndShaped x SND_DRIVE screen units (0.15 at most, 0
//   in silence), the gFlameTurn band on a structure that holds still. Mid gain
//   scales it.
//
// ---- params and knobs -------------------------------------------------------
//
// Morph   streaks to stars: the capsules shorten toward points (sublight).
// Warp    bends the corridor into a slow curve, so the jump banks through space.
// Turbulence  the columns shimmer sideways inside their cells.
// Intensity and the palette controls reach it through grade() and pal().
//
// k0 PULSE  how far the kick stretches the near streaks.
// k1 TEXTURE  streak thickness.
// k2 FLOW   streak length on top of the throttle.
// k3 TONE   the field's hue.
// k4 BUILD  how many cells hold a streak (each fades in on its own hash).
// k5 SPACE  the corridor's width.
//
// ---- why the estimate is safe ----------------------------------------------
//
// Each cell measures only its own capsule, and every capsule stays inside its
// cell with at least WP_MARGIN to spare: half-length at most 3.4 plus radius
// 0.09 against a half-cell of 4 along z, jitter 0.35 plus shimmer 0.2 plus
// radius 0.09 against a half-cell of 1 in xy. Any neighbour's capsule lies
// beyond this cell's wall, so the distance is clamped to the wall plus the
// margin. A column's travel and shimmer move it rigidly, so they change no
// bound. The bend shifts xy by a function of z whose slope is at most
// 0.17 * |Warp|, so the estimate is divided by 1 + 0.17 * |Warp|. The corridor
// and a sphere around the eye are carved out by max(), which keeps a lower
// bound, so the eye is never inside a streak.

#define WP_MAX_STEPS 88
#define WP_FAR 56.0
#define WP_TAU 6.2831853
/** Column pitch in xy. */
#define WP_CELL 2.0
/** Cell length along z: 128 of them make WP_ZWRAP. */
#define WP_ZCELL 8.0
#define WP_ZCELLS 128.0
/** The world's z period, which divides the rig's 8192 wrap. */
#define WP_ZWRAP 1024.0
/** The streak speed factors are k / 16, so a phase pre-wrapped by 16 * 1024 wraps every column exactly. */
#define WP_PHASE_WRAP 16384.0
#define WP_MARGIN 0.3
#define WP_MAX_HALF 3.4
#define WP_BUBBLE 0.7

/** Per-frame shape, set in main(). */
float gLen;
float gRad;
float gDens;
float gPhase;
float gCorr;
float gTurb;
float gKick;
float gBendL;
vec2 gBendEye;
vec3 gEye;

/** The last map() call's own capsule: its distance, hue and brightness. */
float gOwn;
float gHue;
float gBright;

float knob(int i) { return clamp(uKnob[i], 0.0, 1.0) - 0.5; }

/** The corridor's curve. Periods 256 and 128 divide 1024; slope at most 0.17 * |uWarp|. */
vec2 bend(float z) {
    return uWarp * 3.0 * vec2(sin(z * (WP_TAU / 256.0)), 0.6 * sin(z * (WP_TAU / 128.0) + 1.3));
}

float map(vec3 p) {
    // Bent so the corridor passes through the eye wherever the curve is.
    vec2 q = p.xy - (bend(p.z) - gBendEye);
    vec2 cell = floor(q / WP_CELL);
    vec2 f = q - (cell + 0.5) * WP_CELL;
    uint u = uhash3(uvec3(ivec3(int(cell.x), int(cell.y), 7)));
    float h = uhashUnit(u);
    float h2 = uhashUnit(uhash(u + 1u));
    float h3 = uhashUnit(uhash(u + 2u));

    // The column's travel: a hashed speed of k / 16 (see WP_PHASE_WRAP).
    float speed = 0.5 + floor(h * 16.0) / 16.0;
    float s = mod(gPhase * speed, WP_ZWRAP);
    float z = p.z + s;
    float cz = floor(z / WP_ZCELL);
    float fz = z - (cz + 0.5) * WP_ZCELL;

    vec2 jit = (vec2(h2, h3) - 0.5) * 0.7
             + gTurb * vec2(sin(uTime * 1.7 + h * 40.0), cos(uTime * 1.3 + h2 * 40.0));

    uint v = uhash3(uvec3(ivec3(int(cell.x), int(cell.y), int(mod(cz, WP_ZCELLS)))));
    float hv = uhashUnit(v);
    float hv2 = uhashUnit(uhash(v + 1u));
    float hb = uhashUnit(uhash(v + 2u));
    // Fades in on its own hash as the density rises, so no streak pops.
    float thr = 1.0 - gDens;
    float w = smoothstep(thr, thr + 0.1, hv);

    // Only the streaks just ahead of the eye answer the kick.
    float ahead = (cz + 0.5) * WP_ZCELL - s - gEye.z;
    float kick = gKick * exp(-abs(ahead - 6.0) * 0.15);
    float hl = min(gLen * (0.55 + 0.45 * hv2) + kick, WP_MAX_HALF);
    float r = gRad * (0.7 + 0.3 * hv) * w;

    float own = 1e3;
    if (w > 0.0) {
        float dz = max(abs(fz) - hl, 0.0);
        own = length(vec3(f - jit, dz)) - r;
    }
    float toWall = min(WP_CELL * 0.5 - max(abs(f.x), abs(f.y)), WP_ZCELL * 0.5 - abs(fz));
    float d = min(own, toWall + WP_MARGIN);
    d = max(d, gCorr - length(q));
    d /= gBendL;
    d = max(d, WP_BUBBLE - length(p - gEye));

    gOwn = own;
    gHue = h + 0.37 * hv;
    gBright = w * (0.4 + 0.7 * clamp(sndEq(hb), 0.0, 1.5));
    return d;
}

void main() {
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    // The mids turn the look aside (see the header).
    camFly(uv - vec2(0.1 * sndShaped(SND_MIDDLE) * clamp(SND_DRIVE, 0.0, 1.0), 0.0), ro, rd);
    // The world tiles every 1024 along z (a power of two), so this shift is
    // invisible and exact. Without it, far down the 8192 travel a highp z has
    // ~0.001 of fraction left and the thin streaks quantise into speckle.
    ro.z = mod(ro.z, WP_ZWRAP);
    gEye = ro;

    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float throttle = clamp(0.5 * SND_ENERGY_TREND + 0.5 * sndShaped(SND_LEVEL), 0.0, 1.2);
    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float dropBump = 4.0 * drop * (1.0 - drop);
    float hueBase = 0.18 * clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0) + 0.3 * knob(3);

    float stars = mix(1.0, 0.3, clamp(uMorph, 0.0, 1.0));
    gLen = (0.5 + 1.5 * throttle + 1.2 * (knob(2) + 0.5)) * stars + 1.1 * dropBump;
    gRad = 0.05 + 0.04 * (knob(1) + 0.5);
    gDens = clamp(0.42 + 0.4 * knob(4), 0.1, 0.9);
    gCorr = 1.3 + 1.0 * knob(5);
    gTurb = 0.2 * clamp(uTurbulence, 0.0, 1.0);
    gKick = clamp(SND_KICK_ENV, 0.0, 1.0) * (0.6 + 1.2 * (knob(0) + 0.5));
    gBendL = 1.0 + 0.17 * abs(uWarp);
    gBendEye = bend(ro.z);
    // A fixed rate: a live dial times an accumulated clock would jump.
    gPhase = mod(uFlowTime * 9.0, WP_PHASE_WRAP);

    float t = 0.05;
    float hitT = -1.0;
    vec3 glow = vec3(0.0);
    for (int i = 0; i < WP_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        float d = map(ro + rd * t);
        if (gOwn < 0.8) {
            float fade = exp(-t * 0.05);
            glow += pal(hueBase + gHue) * gBright * exp(-gOwn * 9.0) * 0.05 * fade;
        }
        if (d < 0.001 + 0.0008 * t) {
            hitT = t;
            break;
        }
        t += d * 0.9;
        if (t > WP_FAR) break;
    }

    // Deep space: near black, a faint dust of stars, and the corridor's end
    // glowing straight down the travel axis.
    float aheadGlow = max(rd.z, 0.0);
    vec3 sky = pal(hueBase + 0.6) * 0.02;
    sky += pal(hueBase + 0.1) * (0.10 * pow(aheadGlow, 24.0) + 0.04 * aheadGlow * aheadGlow)
         * (1.0 + 1.5 * dropBump);
    vec3 sp = rd * 160.0;
    float sh = hashCell(ivec3(floor(sp)));
    sky += vec3(0.6) * step(0.997, sh) * (1.0 - smoothstep(0.0, 0.35, length(fract(sp) - 0.5)));

    vec3 col = sky;
    if (hitT > 0.0) {
        // The streaks are light, not surfaces: a white-hot core in the
        // streak's colour, fading into the destination glow with distance.
        vec3 core = mix(pal(hueBase + gHue), vec3(1.0), 0.55) * (0.5 + gBright);
        col = mix(core, sky, 1.0 - exp(-hitT * 0.05));
    }
    col += glow;

    col *= 0.8 + 0.25 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
