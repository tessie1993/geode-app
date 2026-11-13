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
//#include lib_shell

// Fractal Dive: a fly-through corridor bored through a space-filling fold
// fractal (box fold by fract() then sphere inversion, the Apollonian /
// pseudo-Kleinian branch of the mandelbox family). The first fold tiles the
// world with period 4 along z (2 / FD_WSCALE), a power of two, so the rig's
// 8192 travel wrap is invisible. The corridor is a cylinder subtracted around the
// z axis, wide enough for the rig's +-2 wander. Flown with camFly.
//
// ---- audio ------------------------------------------------------------------
//
// Kick (SND_KICK_ENV) pulses the inversion scale a little: the walls breathe. Drop
// (SND_HELD_SURGE) twists every fold, as a bump on the envelope so it glides in and
// out. The fold variation (inversion scale, inner fold size) follows uOrbit,
// which the CPU re-targets on a section boundary and eases over 3 s: there is
// no section counter to hash, so that is the held step. The section envelope
// (SND_HELD_HARD_CUT) swells the inner folds on the same bump shape while it lands. Brightness follows SND_ENERGY_TREND only.
//
// ---- knobs ------------------------------------------------------------------
//
// Flame's, Opal's and Hollow's roles, each read as its offset from 0.5 so off
// is the tuned look: k0 PULSE the emission, k1 TEXTURE the width of the
// filigree's trap glow, k2 FLOW a bend of the whole world along the way (a
// corkscrew of period 4, so the wrap holds; its slope divides the estimate),
// k3 TONE the palette position and the inversion's scale (a high melody
// spins the walls out into finer filigree, a low one thickens them into heavy
// arches), k4 BUILD the light threaded past the walls,
// k5 SPACE the reach (the far plane and the fog).
//
// ---- shape inputs (lib_shell) ----------------------------------------------
//
// The fractal's tunables as normalized inputs, mapped once at the top of main;
// 0.5 is the look as tuned (the knobs rest there exactly while Note & key is
// off), and none reads the sound:
//
//   sIters      k1 TEXTURE       fold iterations 4 .. 7 .. 10 (whole)
//   gBendA      k2 FLOW          the corkscrew's radius -0.25 .. 0 .. 0.25
//   sInv        k3 TONE          the inversion's resting scale 1.16 .. 1.22 .. 1.28
//   sBox        k4 BUILD         the inner folds' resting size 0.85 .. 1 .. 1.15
//   sHole       k5 SPACE         the corridor's radius 2.8 .. 3.4 .. 4.2
//   reach       k5 SPACE         far plane and fog 0.8 .. 1 .. 1.2
//
// The world fold (period 4) stays: it is the tile the rebase relies on. The
// fractal goes through its shell wrapper, so the shell's hooks reach it.

#define FD_MAX_STEPS 96
/** The most fold iterations TEXTURE can ask for (7 at rest, sIters). */
#define FD_ITERS 10
/** World to fractal space. The first fract() has period 2 in fractal space, so 4 in the world. */
#define FD_WSCALE 0.5
#define FD_HOLE 3.4
#define FD_FAR 22.0
#define FD_FOG 0.10

float gS;
float gBox;
mat2 gTwA;
mat2 gTwB;
float gTrap;
/** k2 FLOW: the corkscrew's radius, world units (0 at rest), and the estimate's divisor for its slope. */
float gBendA = 0.0;
float gBendL = 1.0;
/** Shape inputs (see the header). */
float sIters;
float sHole;

float mapField(vec3 w) {
    // FLOW's corkscrew: period 4 along z, the world tile, so the rebase stays exact.
    w.xy += gBendA * vec2(sin(w.z * 1.5707963), cos(w.z * 1.5707963));
    vec3 p = w * FD_WSCALE;
    float scale = 1.0;
    float trap = 1e9;
    for (int i = 0; i < FD_ITERS; i++) {
        if (float(i) >= sIters) break;
        // Box fold, then sphere inversion. The first fold is the world tile,
        // so it stays size 1 and untwisted (the twists come after the
        // inversion) and the z period holds; the inner folds take gBox.
        float b = i == 0 ? 1.0 : gBox;
        p = b * (-1.0 + 2.0 * fract(0.5 * p / b + 0.5));
        float r2 = dot(p, p);
        trap = min(trap, r2);
        float k = gS / max(r2, 1e-4);
        p *= k;
        scale *= k;
        p.xy = gTwA * p.xy;
        p.yz = gTwB * p.yz;
    }
    gTrap = trap;
    float de = 0.25 * abs(p.y) / scale / FD_WSCALE;
    // Bore the corridor. smax overestimates inside the cavity a little, which
    // the 0.75 step scale below absorbs.
    return smax(de, sHole - length(w.xy), 0.35) / gBendL;
}

/** mapField through lib_shell's hooks: exactly mapField(p) at rest, and mapField read once, in a loop the compiler cannot unroll. */
float mapFieldGrown(vec3 p) {
    shellStart3(p, SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK);
    float r = 0.0;
    for (int i = SHELL_FROM; i < 7; i++) {
        if (shellPass3(i)) r = shellKeep3(i, mapField(shellAt3(i)));
    }
    return r;
}

/** The fractal as the shell sees it: spawn, fractal, front and back route through here (identity at rest). */
float map(vec3 p) {
    return mapFieldGrown(p);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

void main() {
    // ---- shape inputs: each normalized input mapped to its range, here only (see the header).
    sIters = floor(shellMap(shellKnob(1), 4.0, 7.0, 10.0) + 0.5);
    gBendA = shellMap(shellKnob(2), -0.25, 0.0, 0.25);
    float sInv = shellMap(shellKnob(3), 1.16, 1.22, 1.28);
    float sBox = shellMap(shellKnob(4), 0.85, 1.0, 1.15);
    sHole = shellMap(shellKnob(5), 2.8, FD_HOLE, 4.2);
    float reach = shellMap(shellKnob(5), 0.8, 1.0, 1.2);

    vec2 uv = view();
    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    // The offset's slope along z is gBendA * pi / 2.
    gBendL = sqrt(1.0 + gBendA * gBendA * 2.4674011);

    float swell = clamp(SND_ENERGY_TREND * uSoundDrive, 0.0, 1.5);
    float kick = clamp(SND_KICK_ENV * uSoundDrive, 0.0, 1.0);
    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float sect = clamp(SND_HELD_HARD_CUT, 0.0, 1.0);
    // 0 at the instant of the event and at its end, 1 in between: an envelope
    // with an instant rise, reshaped so the geometry never snaps.
    float dropBump = 4.0 * drop * (1.0 - drop);
    float sectBump = sin(3.14159 * sect);

    gS = sInv + 0.10 * uOrbit.x + 0.03 * sin(uTime * 0.11) + 0.035 * kick;
    gBox = sBox + 0.10 * uOrbit.y + 0.04 * sin(uTime * 0.07 + 0.8) + 0.10 * sectBump;
    // Untwisted at rest: any standing rotation compounds over the folds into
    // noise, so only the drop turns them.
    float twA = 0.30 * dropBump;
    float twB = 0.14 * dropBump;
    gTwA = rot2(twA);
    gTwB = rot2(twB);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // The world tiles every 4 along z, so this shift is invisible and exact
    // (power of two). Without it, far down the 8192 travel a highp z has only
    // ~0.001 of fraction left, which quantises every sample and the normals.
    ro.z = mod(ro.z, 4.0);
    // The grow hooks (lib_shell) open round this eye, in the rebased frame the world is read in.
    shellSetup3(ro, 3.0, 2.0, false);

    float t = 0.02;
    float hitT = -1.0;
    float trap = 1.0;
    float glow = 0.0;
    // The glow's depth, weighted like the glow, to colour it by how far off it hangs.
    float glowT = 0.0;
    float used = 0.0;
    for (int i = 0; i < FD_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        if (t > FD_FAR * reach) break;
        used = float(i);
        float d = map(ro + rd * t);
        float eps = 0.0009 * t + 0.0004;
        if (d < eps) {
            hitT = t;
            trap = gTrap;
            break;
        }
        float g = exp(-d * 18.0) * 0.012;
        glow += g;
        glowT += g * t;
        t += max(d * 0.75, eps);
    }

    float hue = 0.62 + 0.04 * sin(uTime * 0.03) + 0.06 * uOrbit.y + 0.6 * kTone;
    // The fog glows toward the far end of the corridor, so the dive has a
    // destination rather than a black hole.
    float ahead = pow(max(rd.z, 0.0), 12.0);
    vec3 fogCol = pal(hue) * (0.05 + 0.22 * ahead) + vec3(0.004, 0.004, 0.008);
    vec3 col = fogCol;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, 0.0015 * hitT + 0.0006);
        float band = log(max(trap, 1e-4)) * 0.16;
        // Depth, as the Hollows read it: the walls' colour runs on round the
        // palette with distance, the headlamp falls off, and every edge that
        // turns away from the eye catches a rim light, so the nested arches
        // separate into layers instead of one flat screen of filigree.
        float depth = hitT / (FD_FAR * reach);
        vec3 base = pal(hue + 0.25 + band + 0.35 * depth);
        // A headlamp at the eye, plus a cool fill from the corridor axis.
        float diff = max(dot(n, -rd), 0.0) / (1.0 + 0.03 * hitT * hitT);
        vec3 axisDir = normalize(vec3(-p.xy, 0.0) + 1e-4);
        float fill = 0.5 + 0.5 * dot(n, axisDir);
        float spec = pow(max(dot(reflect(rd, n), -rd), 0.0), 24.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.2, 0.25, 1.0);
        float rim = pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 3.0);
        col = base * (0.3 + 0.9 * diff) * ao + pal(hue + 0.5) * fill * 0.12 * ao + vec3(spec) * 0.35 * ao
            + pal(hue + 0.55 + 0.35 * depth) * rim * 0.3 * ao;
        // Trap glow on the thinnest shells: the filigree lights up.
        col += pal(hue + 0.1) * (1.0 - smoothstep(0.0, 0.06 * (1.0 + kTexture), trap)) * 0.25;
        col = mix(col, fogCol, 1.0 - exp(-hitT * FD_FOG / reach));
    }
    // Light threaded past the walls, coloured off the corridor's hue.
    float glowFar = glow > 1e-5 ? glowT / (glow * FD_FAR * reach) : 0.0;
    col += pal(hue + 0.15 + 0.3 * glowFar) * glow * (0.6 + 0.3 * swell) * (1.0 + kBuild);

    col *= 0.85 + 0.2 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col * (1.0 + 0.5 * kPulse)), 1.0);
}
