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

// Particle Tunnel: a curving tube of glowing points and streaks, flown down.
//
// There is no surface. The ray walks the unit grid cell by cell (a 3D DDA, so
// each cell it crosses is visited exactly once). A cell in the tube's shell
// may hold one particle, and the particle's glow comes from the ray's closest
// approach to it, worked out exactly, so no step size can skip or alias it.
// Streaks are the same points measured in a space squashed along z.
//
// The camera is the shared rig (camFly). The tube's centre line and the cell
// hash both repeat with power-of-two periods along z, so the 8192 wrap is
// invisible. The shell starts at radius 3.95, outside the rig's wander.
//
// ---- audio ------------------------------------------------------------------
//
// Hats (SND_HAT_ENV) make about half the particles swell and twinkle; the rest
// stay put. An onset (SND_ONSET_ENV) spawns a thin ring ahead that expands out to
// the wall as its envelope falls, coloured by SND_ONSET_COUNT so each one differs.
// A section change (SND_HELD_HARD_CUT) steps the whole tunnel's hue by a third and
// glides it back over the event's 1.5 s. Overall level rides SND_LEVEL.
//
// ---- knobs ------------------------------------------------------------------
//
// Flame's, Opal's and Hollow's roles, each read as its offset from 0.5 so off
// is the tuned look: k0 PULSE the emission, k1 TEXTURE the particles' size, k2
// FLOW the tube's bend, k3 TONE the palette position and the tube's girth (a
// high melody draws the shell in tight round the flight, a low one opens it
// wide; at its tightest the shell's inner face stays clear of the rig's
// wander), k4 BUILD the far end's
// glow and haze, k5 SPACE how far the walk reaches.
//
// ---- shape inputs (lib_shell) ----------------------------------------------
//
// Every tunable of the tube is a normalized input, mapped to its range in one
// place (the top of main) and read from globals. 0.5 is the look as tuned: the
// knobs rest there exactly while Note & key is off. Only knobs drive them here:
// this style leaves Morph, Warp and Turbulence to view(), which already bends
// its picture with them. Music reaches these only through the knobs (the
// reaction matrix); none reads the sound.
//
//   input       driver           lo .. rest .. hi
//   gBend       k2 FLOW          the centre line's bend 0.75 .. 1 .. 1.25
//   sStreak     k2 FLOW          a particle's chance to be a streak, as a cut 0.9 .. 0.72 .. 0.45
//   sStreakL    k2 FLOW          a streak's length 0.6 .. 1 .. 1.5
//   gRadius     k3 TONE          the shell's radius 5.75 .. 5 .. 4.25
//   sShell      k4 BUILD         the shell's half-thickness 0.6 .. 1.05 .. 1.4
//   sEmpty      k4 BUILD         the share of shell cells left empty 0.8 .. 0.6 .. 0.35
//   reach       k5 SPACE         how far the walk reaches 0.8 .. 1 .. 1.2
//
// The cell (1) and its hash wrap (1024) stay constants: the walk is a unit
// DDA and the wrap is the power of two the eye's rebase relies on. Which
// cells hold particles is a 3D field (the shell, ptShell) routed through
// its shell wrapper, so the shell's spawn, fractal, front and back hooks reshape
// the swarm; the walk's early exit holds only while those hooks rest.

/** Cells walked per ray: the compile-time ceiling. uSteps is the runtime budget. */
#define PT_MAX_CELLS 96
#define PT_TAU 6.2831853
#define PT_RADIUS 5.0
/** Half-thickness of the particle shell around PT_RADIUS. */
#define PT_SHELL 1.05
#define PT_FAR 60.0
/** Cells along z before the hash repeats: 1024 world units, which divides 8192. */
#define PT_ZWRAP 1024.0

/** k2 FLOW: the bend of the tube's centre line, 0.75..1.25 (1 at rest), so the shell keeps clear of the rig. */
float gBend = 1.0;
/** k3 TONE: the shell's radius (PT_RADIUS at rest). */
float gRadius = PT_RADIUS;
/** k4 BUILD: the shell's half-thickness (PT_SHELL at rest). */
float sShell = PT_SHELL;

vec2 tubeCentre(float z) {
    return gBend * vec2(0.8 * sin(z * (PT_TAU / 128.0)), 0.6 * sin(z * (PT_TAU / 64.0) + 1.3));
}

/** The particle shell as a field: at or below 0 a cell centre may hold a particle. */
float ptShell(vec3 cc) {
    vec2 rel = cc.xy - tubeCentre(cc.z);
    return abs(length(rel) - gRadius) - sShell;
}

/** ptShell through lib_shell's hooks: exactly ptShell(p) at rest, and ptShell read once, in a loop the compiler cannot unroll. */
float ptShellGrown(vec3 p) {
    shellStart3(p, SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK);
    float r = 0.0;
    for (int i = SHELL_FROM; i < 4; i++) {
        if (shellPass3(i)) r = shellKeep3(i, ptShell(shellAt3(i)));
    }
    return r;
}

void main() {
    // ---- shape inputs: each normalized input mapped to its range, here only (see the header).
    gBend = shellMap(shellKnob(2), 0.75, 1.0, 1.25);
    float sStreak = shellMap(shellKnob(2), 0.9, 0.72, 0.45);
    float sStreakL = shellMap(shellKnob(2), 0.6, 1.0, 1.5);
    gRadius = shellMap(shellKnob(3), PT_RADIUS * 1.15, PT_RADIUS, PT_RADIUS * 0.85);
    sShell = shellMap(shellKnob(4), 0.6, PT_SHELL, 1.4);
    float sEmpty = shellMap(shellKnob(4), 0.8, 0.6, 0.35);
    float reach = shellMap(shellKnob(5), 0.8, 1.0, 1.2);
    bool shellAtRest = abs(SHELL_SPAWN) + abs(SHELL_FRACTAL) + abs(SHELL_FRONT) + abs(SHELL_BACK) == 0.0;

    vec2 uv = view();
    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 1024, so this shift is
    // invisible and exact (power of two). Without it, far down the 8192
    // travel a highp z has ~0.001 of fraction left: every sample and the
    // finite-difference normals quantise into speckle.
    ro.z = mod(ro.z, PT_ZWRAP);
    // The grow hooks (lib_shell) open round this eye, in the rebased frame the world is read in.
    shellSetup3(ro, 3.0, 0.5, false);

    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    float energy = clamp(sndShaped(SND_LEVEL), 0.0, 1.5);
    float hats = clamp(SND_HAT_ENV, 0.0, 1.0);
    float hueStep = 0.33 * smoothstep(0.0, 0.3, clamp(SND_HELD_HARD_CUT, 0.0, 1.0)) + 0.6 * kTone;

    // DDA setup. A zero direction component is nudged so its crossing is
    // effectively never reached instead of dividing by zero.
    vec3 cell = floor(ro);
    vec3 pos = step(0.0, rd);
    vec3 dirSign = pos * 2.0 - 1.0;
    vec3 inv = 1.0 / max(abs(rd), vec3(1e-4));
    vec3 tMax = (cell + pos - ro) * dirSign * inv;

    vec3 col = vec3(0.0);
    for (int i = 0; i < PT_MAX_CELLS; i++) {
        if (float(i) >= uSteps) break;
        vec3 c = cell;
        float tExit = min(tMax.x, min(tMax.y, tMax.z));
        vec3 m = step(tMax.xyz, tMax.yzx) * step(tMax.xyz, tMax.zxy);
        tMax += m * inv;
        cell += m * dirSign;
        if (tExit > PT_FAR * reach) break;

        vec3 cc = c + 0.5;
        vec2 rel = cc.xy - tubeCentre(cc.z);
        float rr = length(rel);
        // Outside the shell and heading further out: this ray is done (unless
        // the shell's hooks have spawned or folded copies beyond it).
        if (shellAtRest && rr > gRadius + sShell + 0.9 && dot(rel, rd.xy) > 0.0) break;
        if (ptShellGrown(cc) > 0.0) continue;

        uint u = uhash3(uvec3(ivec3(int(c.x), int(c.y), int(mod(c.z, PT_ZWRAP)))));
        if (uhashUnit(u) < sEmpty) continue;
        uint u2 = uhash(u);
        vec3 jit = vec3(float(u2 & 1023u), float((u2 >> 10u) & 1023u), float((u2 >> 20u) & 1023u)) * (1.0 / 1023.0);
        uint u3 = uhash(u2);
        float s1 = uhashUnit(u3);
        float s2 = uhashUnit(uhash(u3));

        // Kept inside 0.1..0.9 of the cell so the glow is not cut at its edge.
        vec3 pt = c + 0.2 + 0.6 * jit;
        pt += 0.1 * sin(vec3(uTime * 0.7, uTime * 0.53, uTime * 0.37) + jit * 6.0);
        float streak = s1 > sStreak ? 1.0 : 0.0;
        // Streaks sit mid-cell along z so their length has room.
        pt.z = mix(pt.z, c.z + 0.5, streak);
        float stretch = mix(1.0, (2.0 + 1.8 * s2) * sStreakL, streak);

        vec3 o = ro - pt;
        o.z /= stretch;
        vec3 dd = vec3(rd.xy, rd.z / stretch);
        float tc = -dot(o, dd) / dot(dd, dd);
        if (tc < 0.0) continue;
        vec3 cl = o + dd * tc;
        float d2 = dot(cl, cl);

        float spark = step(0.5, s2);
        float tw = 0.5 + 0.5 * sin(uTime * (7.0 + 11.0 * s1) + s2 * 61.0);
        float pop = hats * spark * tw;
        float size = (0.03 + 0.035 * s1) * (1.0 + 1.3 * pop) * (1.0 + kTexture);
        float k = size * size;
        float g = k / (d2 + k);
        float glow = g * g + 0.05 * g;
        // Fade with depth, and out right at the eye so nothing blows up.
        float fade = exp(-tc * 0.05 / reach) * smoothstep(0.2, 1.4, tc);
        float hue = hueStep + 0.25 * jit.x + 0.18 * sin(cc.z * (PT_TAU / 64.0)) + 0.10 * rel.y / max(rr, 1e-3);
        vec3 pc = mix(pal(hue), vec3(1.0), 0.15 * spark * tw + 0.5 * pop);
        col += pc * glow * fade * (0.9 + 1.6 * pop) * mix(1.0, 0.7, streak);
    }

    // The onset ring, in a plane ahead of the eye. It starts near the axis and
    // reaches the wall as the envelope falls.
    float onset = clamp(SND_ONSET_ENV, 0.0, 1.0);
    if (onset > 0.002 && rd.z > 0.05) {
        float seed = fract(SND_ONSET_COUNT * 0.618);
        float zr = ro.z + 6.0 + 8.0 * seed;
        float tr = (zr - ro.z) / rd.z;
        vec3 q = ro + rd * tr;
        float ringR = gRadius * (1.0 - 0.8 * onset);
        float dr = length(q.xy - tubeCentre(zr)) - ringR;
        float w = 0.05 + 0.004 * tr;
        float ring = w * w / (dr * dr + w * w);
        col += mix(pal(seed + hueStep), vec3(1.0), 0.2) * ring * sqrt(onset) * 0.8 * exp(-tr * 0.05);
    }

    // The far end of the tube and a faint haze, so the black has depth.
    vec3 axis = normalize(vec3(tubeCentre(ro.z + 40.0) - ro.xy, 40.0));
    float endGlow = pow(max(dot(rd, axis), 0.0), 18.0);
    col += (pal(0.62 + hueStep) * (0.03 + 0.25 * endGlow) + pal(0.80 + hueStep) * 0.015) * (1.0 + kBuild);

    col *= 0.85 + 0.35 * swell + 0.10 * energy;
    col *= 0.7 + 0.3 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col * (1.0 + 0.5 * kPulse)), 1.0);
}
