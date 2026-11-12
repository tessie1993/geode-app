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
//#include lib_touch
//#include lib_shell

// Spiral Eye: a flight down the throat of a galaxy that is also an eye. The
// 2D style wound a turbulent cloud into a logarithmic spiral around a small
// iridescent mandala; here that cloud is a real volume. It fills a thick
// cylindrical shell around the flight axis (+z), wound into 2, 3 or 4 spiral
// arms that twist one full turn every 8 world units, so the arms become
// helices the camera flies inside, with genuine black lanes between them.
// The dye is advected through the incompressible flow of lib_scene_motion
// (abcFlow, folded twice as fluidWarp3 does) and stretched along z, which is
// the old radial streak made solid: the cloud is pulled past the eye the way
// motion blur pulled it in 2D. The mandala sits at infinity at the far end of the throat, so it is always
// straight ahead down the axis and the cloud parts around it.
//
// ---- the camera ---------------------------------------------------------------
//
// The shared rig, camFly: the eye rides the path down +z inside the clear
// channel (radius SE_R0, wider than the rig's +-2 wander, and even a wander
// into the cloud is harmless in a volume). Every z term has a period dividing
// 16: the arm twist (8), the flow (abcFlow at TAU/8 and TAU/4) and the dye
// lattice (wrapped to 8 and 16 cells per 16 units). So the eye is rebased by
// mod 16, which divides the rig's 8192 wrap and keeps z precise.
//
// ---- audio --------------------------------------------------------------------
//
// sndShaped(SND_LOW) tightens the spiral's pitch, as it did in 2D. The swell,
// 0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE), thickens the cloud and opens
// its outer falloff. Treble (SND_TREBLE_PRESENCE) lights the filaments, the
// steep edges of the dye. A spike never brightens anything at once: it re-aims
// the flow (uMoveDir, through the abcFlow drift) and the pupil, and moves the
// arm count to a new plateau
// (uFormPhase), crossfaded so no frame steps. The galaxy, its cloud and the
// mandala's spokes turn on time alone, at the rate the loudness clock
// (uFlowPhase) used to add at a typical level: no sound spins them (owner
// 2026-11-12, no style spins to the music). Knobs: k0 density, k1 filament sharpness, k2 flow, k3 the
// arms' pitch and a small hue offset (a high melody winds them into tight
// coils round the throat, a low one lets them run out in long open sweeps),
// k4 the eye's halo, k5 the channel's width; each is an offset from 0.5.
//
// ---- why the march is safe ------------------------------------------------------
//
// There is no distance estimate to overshoot: the cloud is a volume and the
// ray takes bounded steps through it, accumulating emission and extinction,
// as nectar_flow does. The only skips are exact: inside the clear channel the
// ray jumps analytically to where it leaves the inner cylinder, and once it is
// outside the outer cylinder and moving outward it stops, since the shell is
// all there is. Both are ray-cylinder roots, not estimates.

#define SE_MAX_STEPS 72
#define SE_TAU 6.2831853
#define SE_ZWRAP 16.0
/** Radius of the clear flight channel. */
#define SE_R0 2.1
/** Outer radius of the cloud shell. */
#define SE_R1 6.6
#define SE_FAR 28.0
#define SE_MIN_T 0.015
/** Arm twist: one turn per 8 world units along z. */
#define SE_TWIST (SE_TAU / 8.0)

float gCloud;
float gFil;

/**
 * Value noise whose lattice repeats every `wz` cells along z, so a field
 * sampled at a z period of 16 world units survives the eye's rebase exactly.
 * z cells are wrapped with floor rather than %, which GLSL ES leaves undefined
 * for negative operands.
 */
float seNoise(vec3 p, int wz) {
    vec3 f = floor(p);
    ivec3 i = ivec3(f);
    vec3 t = p - f;
    vec3 w = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    int z0 = i.z - wz * int(floor(f.z / float(wz)));
    int z1 = z0 + 1 == wz ? 0 : z0 + 1;

    float c000 = hashCell(ivec3(i.x, i.y, z0));
    float c100 = hashCell(ivec3(i.x + 1, i.y, z0));
    float c010 = hashCell(ivec3(i.x, i.y + 1, z0));
    float c110 = hashCell(ivec3(i.x + 1, i.y + 1, z0));
    float c001 = hashCell(ivec3(i.x, i.y, z1));
    float c101 = hashCell(ivec3(i.x + 1, i.y, z1));
    float c011 = hashCell(ivec3(i.x, i.y + 1, z1));
    float c111 = hashCell(ivec3(i.x + 1, i.y + 1, z1));

    float x00 = mix(c000, c100, w.x);
    float x10 = mix(c010, c110, w.x);
    float x01 = mix(c001, c101, w.x);
    float x11 = mix(c011, c111, w.x);
    return mix(mix(x00, x10, w.y), mix(x01, x11, w.y), w.z);
}

/**
 * lib_scene_motion's fluidWarp3, with the second fold at exactly twice the
 * first's scale instead of 2.07: at TAU/8 and TAU/4 both abcFlow passes tile
 * along z with periods dividing 16, so the rebase stays invisible (2.07 would
 * not tile and the cloud would jump every 16 units of travel).
 */
vec3 seWarp(vec3 p, float amount) {
    float s = SE_TAU / 8.0;
    vec3 drift = vec3(uMoveDir * (uFlowPhase * 0.30), uFlowPhase * 0.18);
    vec3 q = p + amount * abcFlow(p * s + drift);
    return q + amount * 0.55 * abcFlow(q * (2.0 * s) + drift * 1.4 + 3.1);
}

/** Gated arm mask for `n` arms (an integer, so the seam at atan's +-pi is closed). */
float seArm(float n, float a, float lr, float tw, float pitch, float sharp) {
    float phase = n * (a + pitch * lr - tw) + uTime * 0.48;
    float c = 0.5 + 0.5 * cos(phase);
    return smoothstep(0.5 - 0.42 * sharp, 0.5 + 0.42 * sharp, c);
}

/**
 * Dye density at p (z already rebased). Sets gCloud (the raw dye, for colour)
 * and gFil (the filaments, the dye's steep edges).
 */
float dens(vec3 p, float armsA, float armsB, float armBlend, float pitch, float sharp,
           float flowAmt, float swell, float filSharp, float r0) {
    float r = length(p.xy);
    float a = atan(p.y, p.x);
    float lr = log(max(r, 1e-3));
    float tw = p.z * SE_TWIST;

    float shell = smoothstep(r0, r0 + 1.1, r)
                * (1.0 - smoothstep(SE_R1 - 2.4 - 0.8 * swell, SE_R1, r));
    float arm = mix(seArm(armsA, a, lr, tw, pitch, sharp), seArm(armsB, a, lr, tw, pitch, sharp), armBlend);

    // The cloud turns with the arms (the rotation's z period is 8) and is
    // folded by the flow; then it is read stretched 2:1 along z, the streak.
    vec2 xy = rot2(tw + uTime * 0.12) * p.xy;
    vec3 q = seWarp(vec3(xy, p.z), flowAmt);
    float n1 = seNoise(q * vec3(0.9, 0.9, 0.5), 8);
    float n2 = seNoise(q * vec3(2.1, 2.1, 1.0) + vec3(5.1, 2.7, 0.0), 16);
    float cloud = 0.68 * n1 + 0.32 * n2;
    gCloud = cloud;

    float body = smoothstep(0.30, 0.72, cloud * (0.62 + 0.55 * arm));
    float lo = 0.50 + 0.04 * filSharp;
    float fil = smoothstep(lo, lo + 0.20, cloud) * (1.0 - smoothstep(lo + 0.20, lo + 0.34, cloud));
    gFil = fil * arm * shell;
    return shell * body * (0.12 + 0.88 * arm);
}

/** dens()'s per-frame arguments, set once in main, so the field is a function of p alone. */
float gArmsA;
float gArmsB;
float gArmBlend;
float gPitch;
float gSharp;
float gFlowAmt;
float gSwellD;
float gFilSharp;
float gR0;

/** The dye's density as a field of p alone. */
float seField(vec3 p) {
    return dens(p, gArmsA, gArmsB, gArmBlend, gPitch, gSharp, gFlowAmt, gSwellD, gFilSharp, gR0);
}

/** The field the march reads: seField, through lib_shell's hook (the identity until the shell drives it). */
float seDensity(vec3 p) {
    return SHELL_FIELD3(p, seField);
}

/** Larger root of |o + t d| = R in the xy plane, or -1 when the ray never meets it. */
float cylExit(vec2 o, vec2 d, float R) {
    float A = dot(d, d);
    if (A < 1e-6) return -1.0;
    float B = dot(o, d);
    float C = dot(o, o) - R * R;
    float disc = B * B - A * C;
    if (disc < 0.0) return -1.0;
    return (-B + sqrt(disc)) / A;
}

/** The mandala at infinity down +z: rings cut by spokes, iridescent, white-hot middle. */
vec3 seEye(vec3 rd, float swell, float halo) {
    if (rd.z <= 0.05) return vec3(0.0);
    // Where the ray lands on a plane one unit down the axis; the pupil leans
    // toward the travel direction, which glides, so a spike re-aims the gaze.
    vec2 e = rd.xy / rd.z - uMoveDir * 0.025;
    float r = length(e);
    float a = atan(e.y, e.x);
    vec3 warm = pal(0.10);
    float cr = r * 7.0;
    float rings = 0.5 + 0.5 * sin(cr * 5.0 - uFlowPhase * 3.0);
    float spokesA = 8.0 + 4.0 * floor(uFormPhase * 3.0);
    float spokes = 0.5 + 0.5 * cos(a * spokesA + uTime * 0.6);
    float coreMask = exp(-cr * cr * 0.30);
    vec3 iris = pal(fract(0.15 + cr * 0.10 + uFormPhase * 0.2));
    vec3 col = iris * coreMask * (0.35 + 0.65 * rings * spokes) * 1.4;
    col += vec3(1.0, 0.97, 0.9) * exp(-r * r * 700.0) * 2.0;
    col += warm * exp(-r * 11.0) * 0.35 * halo;
    // The galaxy's far glow, so the end of the throat is lit, not a hole.
    col += pal(0.55) * exp(-r * 2.2) * (0.06 + 0.06 * swell);
    return col;
}

void main() {
    vec2 uv = view();

    // ---- the shell: this style's shape as normalized inputs ---------------
    //
    // Every tunable of the shape is a 0..1 input here, set from the params and
    // the six knobs and 0.5 at their defaults, and mapped to its range in this
    // one place: lib_shell's shellMap(n, lo, rest, hi) puts 0.5 exactly on the
    // value the style was tuned at, so the default look is unchanged, and the
    // shell's hooks can later fractal out, move and manipulate any of them.
    float nDensity = clamp(uKnob[0], 0.0, 1.0);
    float nFil = clamp(uKnob[1], 0.0, 1.0);
    float nStir = clamp(uKnob[2], 0.0, 1.0);
    float nPitch = clamp(uKnob[3], 0.0, 1.0);
    float nHalo = clamp(uKnob[4], 0.0, 1.0);
    float nChannel = clamp(uKnob[5], 0.0, 1.0);
    float nArmEdge = 0.5 + 0.5 * clamp(uMorph, 0.0, 1.5) / 1.5;
    float nTurb = 0.5 + 0.5 * clamp(uTurbulence, 0.0, 1.5) / 1.5;
    // The cloud's density, the filaments' sharpness, the stir's depth, the
    // arms' pitch and their edge, the eye's halo, the clear channel's radius.
    float densityK = shellMap(nDensity, 0.6, 1.0, 1.4);
    float filSharp = shellMap(nFil, -0.5, 0.0, 0.5);
    float stirK = shellMap(nStir, 0.6, 1.0, 1.4);
    float stirBase = shellMap(nTurb, 0.18, 0.18, 0.51);
    float pitchAdd = shellMap(nPitch, -0.8, 0.0, 0.8);
    float sharp = shellMap(nArmEdge, 1.0, 1.0, 0.325);
    float halo = shellMap(nHalo, 0.4, 1.0, 1.6);
    float r0 = shellMap(nChannel, SE_R0 * 0.85, SE_R0, SE_R0 * 1.15);
    float hue = shellMap(nPitch, -0.06, 0.0, 0.06);

    float bass = clamp(sndShaped(SND_LOW), 0.0, 1.5);
    float treb = clamp(SND_TREBLE_PRESENCE, 0.0, 1.5);
    float swell = clamp(0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE), 0.0, 1.5);

    // A spike steps the arm count between 2, 3 and 4 and holds it; crossfaded
    // across the middle of each step so the count never jumps in one frame.
    float fp = clamp(uFormPhase, 0.0, 1.0) * 3.0;
    float na = min(floor(fp), 2.0);
    float armBlend = na >= 2.0 ? 0.0 : smoothstep(0.35, 0.65, fp - na);
    float armsA = 2.0 + na;
    float armsB = 2.0 + min(na + 1.0, 2.0);

    // Bass tightens the wind; Warp leans it further. pitch multiplies log r,
    // never z, so it is free to move continuously.
    float pitch = max(1.35 + 0.35 * bass + 0.4 * clamp(uWarp, 0.0, 2.0) + pitchAdd, 0.2);
    float flowAmt = stirBase * stirK * (0.8 + 0.3 * swell);
    float density = (1.1 + 1.0 * swell) * densityK;
    gArmsA = armsA;
    gArmsB = armsB;
    gArmBlend = armBlend;
    gPitch = pitch;
    gSharp = sharp;
    gFlowAmt = flowAmt;
    gSwellD = swell;
    gFilSharp = filSharp;
    gR0 = r0;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 16 (see the header), so this
    // rebase is exact and invisible.
    ro.z = mod(ro.z, SE_ZWRAP);

    vec3 warm = pal(0.10 + hue);
    vec3 cool = pal(0.55 + hue);
    vec3 filCol = mix(vec3(1.0), warm, 0.35);

    float nSteps = clamp(uSteps * 0.75, 16.0, float(SE_MAX_STEPS));
    float base = SE_FAR / nSteps;
    // A fixed per-pixel jitter on the start hides the step banding.
    float jit = hashCell(ivec3(ivec2(gl_FragCoord.xy), 7));

    float t = SE_MIN_T + base * 0.35 * jit;
    float T = 1.0;
    vec3 acc = vec3(0.0);
    for (int i = 0; i < SE_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= nSteps) break;
        if (t > SE_FAR || T < 0.015) break;
        vec3 p = ro + rd * t;
        float r = length(p.xy);
        // Exact skips: across the clear channel, and out past the shell.
        if (r < r0 - 0.05) {
            float tx = cylExit(ro.xy, rd.xy, r0);
            if (tx < 0.0) {
                t = SE_FAR + 1.0;
                break;
            }
            t = max(t + 0.01, tx) + base * 0.35 * jit;
            continue;
        }
        if (r > SE_R1 && dot(p.xy, rd.xy) > 0.0) break;

        // Short steps near the eye, longer far off.
        float dt = base * (0.45 + 1.1 * t / SE_FAR);
        float d = seDensity(p) * density;
        if (d > 0.002 || gFil > 0.002) {
            float edge = r - r0;
            // The inner rim catches the eye's light; the far side sinks to cool.
            vec3 c = mix(cool, warm, smoothstep(0.35, 0.85, gCloud));
            float lit = 0.35 + 0.9 * exp(-edge * 0.55);
            vec3 emit = c * d * lit * (0.55 + 0.75 * swell)
                      + filCol * gFil * lit * (0.30 + 0.9 * treb) * 1.4;
            float fog = exp(-t * 0.055);
            float a = 1.0 - exp(-d * 0.9 * dt);
            acc += T * emit * dt * fog;
            T *= 1.0 - a;
        }
        t += dt;
    }

    vec3 bg = vec3(0.004, 0.003, 0.008) + seEye(rd, swell, halo);
    vec3 col = acc + T * bg;

    if (!touchIdle()) {
        col += pal(0.5 + 0.15 * sin(uTime * 0.05)) * min(touchWake(uv), 3.0) * 0.06;
    }
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
