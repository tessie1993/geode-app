#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uSoundTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
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

// WINTER: a night flight low over a frozen pond. Down the middle runs a lead
// of open black water, ringed by interference ripples from drifting sources;
// either side of it the pond is frozen into ice crazed with voronoi frost
// cracks; past the shoreline snow banks rise, set with snow-capped pines; and
// snow falls through the whole scene in true depth, flakes riding the wind.
// A low moon ahead lights the snow and lays its streak on the water.
//
// The 2D style was the same pond seen from above, with three parallax layers
// of snow. Its idea survives whole: the ripples, the frost glints, the snow on
// the wind, the spectrum along the shoreline. Only now the pond has a far
// shore, the snow has real depth and the camera flies.
//
// ---- the camera --------------------------------------------------------------
//
// The shared rig (camFly). The pond runs straight down +z. Every z term has a
// period dividing 64 (shore wobble 64, lead width 32, bank bumps 16, ripple
// source cells 16, tree cells 2, frost cells 0.5, snow planes 1.6, shore
// spectrum 32), so rebasing the eye by 64 is exact and the rig's 8192 travel
// wrap never shows. The pond is ~11 units wide, wider than any rig path
// wanders (glide |x| <= 3, orbit radius <= 5.4), so no path flies into a bank.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (SND_BASS_ATT)          tightens the rings of the bass ripple sources.
// Mids (SND_MIDDLE)            strengthen the second set of rings and push the wind.
// Treble (SND_TREBLE)          frost cracks and their feature points glint; snow
//                              twinkles per flake (never a full-frame flicker).
// Kick (SND_KICK_ENV)          a gust: the rings swell and the snow thickens.
// Spectrum (sndSpectrum)       shimmers along the shoreline, read along its length.
// Level, SND_ENERGY_TREND      overall brightness, as everywhere.
//
// Morph thaws the pond (a wider lead), Warp lets the ripple sources wander,
// Sway and Turbulence steer the wind. Knobs, each read as an offset from 0.5
// so the style looks as tuned while Note & key reactions are off: k0 PULSE
// ripple height, k1 TEXTURE frost crack density, k2 FLOW wind, k3 TONE how
// high the moon stands (a high melody lifts it up the sky and shortens the
// shadows, a low one sets it on the horizon so the light rakes long across
// the snow and the ice; and a small hue offset), k4 BUILD snowfall, k5 SPACE
// fog distance.
//
// ---- shape inputs (lib_shell) ----------------------------------------------
//
// Every tunable of the pond is a normalized input, mapped to its range in one
// place (the top of main) and read from globals. 0.5 is the look as tuned: the
// knobs rest there exactly while Note & key is off, and a param at its default
// 0 maps to its rest through shellMapS. Music reaches these only through the
// knobs and params (the reaction matrix); none reads the sound.
//
//   input       driver           lo .. rest .. hi
//   sBump       k1 TEXTURE       the banks' bumps 0.15 .. 0.35 .. 0.6
//   sFrost      k1 TEXTURE       frost cells per unit 1.0 .. 1.8 .. 2.6
//   sPines      k1 TEXTURE       the share of cells with a pine 0.3 .. 0.6 .. 0.9
//   sMoonY      k3 TONE          the moon's height 0.045 .. 0.32 .. 0.595
//   sRise       k4 BUILD         the banks' height 1.6 .. 2.6 .. 3.8
//   sBank       k4 BUILD         the banks' run 5.5 .. 4.0 .. 3.0
//   sTreeH      k4 BUILD         the pines' height 0.6 .. 1 .. 1.3
//   sFlake      k4 BUILD         a flake's size 0.7 .. 1 .. 1.5
//   sShore      k5 SPACE         the pond's half-width 5.0 .. 5.6 .. 7.0
//   sFog        k5 SPACE         fog density 0.063 .. 0.045 .. 0.027
//   sThaw       Morph            the lead's widening 0 .. 1.6
//   sWander     Warp             the ripple sources' wander 0.6 .. 2.0
//
// The z periods (64, 32, 16, 2, 0.5, 1.6) stay constants: each divides the 64
// the eye is rebased by. A steeper bank (sRise over sBank) or bigger bumps
// shrink the height field's Lipschitz factor in proportion (gGlip). The banks
// and pines go through its shell wrapper, the lead's rings and the frost through
// its shell wrapper, so the shell's spawn, fractal, front and back hooks reach them.
//
// ---- why the estimate is safe ----------------------------------------------
//
// The pond surface is the plane y = WI_ICE, found analytically, never marched:
// every surface lies on or above it, so a ray that reaches that height
// unhit is on the pond. The march sees only the banks and the trees.
//  - Over the pond (|x| < WI_WMIN, where the bank cannot start) the bank is
//    bounded by its slab: max(WI_WMIN - |x|, y - its highest point).
//  - On the bank the height field's gradient is at most ~1.45 (the slope
//    1.5 / WI_BANK * WI_RISE, plus the bumps), so the vertical gap times
//    WI_GLIP (1 / sqrt(1 + 1.45^2)) is a lower bound.
//  - Each tree cell measures only its own pine (two cones, each a half-plane
//    in the (r, y) plane of revolution, so a lower bound), and is clamped to
//    the cell boundary unless the point is above every pine's possible tip.
//    No pine reaches |x| < WI_TREE_X0, so nearer than that the slab bounds it.
//  - A sphere around the eye is carved out of everything.

#define WI_MAX_STEPS 96
#define WI_FAR 48.0
#define WI_TAU 6.2831853
/** The eye's z is rebased into 0..64; every z period divides it. */
#define WI_ZWRAP 64.0
/** The pond's surface. */
#define WI_ICE -2.0
/** Half-width of the pond before the banks start, and the shore's wobble. */
#define WI_SHORE 5.6
#define WI_WOBBLE 0.6
#define WI_WMIN (WI_SHORE - WI_WOBBLE)
#define WI_BANK 4.0
#define WI_RISE 2.6
#define WI_BUMP 0.35
#define WI_GLIP 0.55
/** Pine cells along xz (32 per 64 along z). */
#define WI_TC 2.0
#define WI_TREE_X0 (WI_SHORE + 0.6)
#define WI_TREE_HMAX 3.0
#define WI_BUBBLE 0.6
/** Snow planes along z: 40 per 64. */
#define WI_SNOW_S 1.6
#define WI_SNOW_N 6
/** Flake cell, world units; the fall wraps at 256 cells. */
#define WI_SNOW_C 0.45

/** 0 bank snow, 1 pine. */
float gMat;
vec3 gEye;
/** Shape inputs (see the header). */
float sBump;
float sFrost;
float sPines;
float sMoonY;
float sRise;
float sBank;
float sTreeH;
float sFlake;
float sShore;
float sFog;
float sThaw;
float sWander;
/** The bank's Lipschitz factor: WI_GLIP, shrunk when the inputs steepen the banks. */
float gGlip;
/** The sound the lead's rings hear, set once per pixel for wiRippleField(). */
float gBassAtt;
float gMid;
float gTreble;

/** The pond's half-width where the banks cannot start, and where no pine stands. */
float wiWMin() { return sShore - WI_WOBBLE; }
float wiTreeX0() { return sShore + 0.6; }

float wiShoreW(float z) {
    return sShore + WI_WOBBLE * sin(z * (WI_TAU / 64.0) + 0.7);
}

float wiBankAt(float ax, float z) {
    float w = wiShoreW(z);
    return smoothstep(w, w + sBank, ax);
}

float wiGround(vec2 xz) {
    float bank = wiBankAt(abs(xz.x), xz.y);
    float bumps = sBump * sin(xz.x * 0.9 + 1.3) * sin(xz.y * (WI_TAU / 16.0));
    return WI_ICE + sRise * bank + bank * bumps;
}

/** A lower bound on the distance to a cone of apex height `apex` and slope `tanA` (radius per unit down). */
float wiCone(vec2 f, float y, float apex, float tanA) {
    float cosA = inversesqrt(1.0 + tanA * tanA);
    return (length(f) - (apex - y) * tanA) * cosA;
}

float mapField(vec3 p) {
    float ax = abs(p.x);
    float gMax = WI_ICE + sRise + sBump;
    float wMin = wiWMin();
    float dg = ax < wMin ? max(wMin - ax, p.y - gMax) : (p.y - wiGround(p.xz)) * gGlip;

    float hiN = gMax + WI_TREE_HMAX * max(sTreeH, 1.0);
    float dt;
    float treeX0 = wiTreeX0();
    if (ax < treeX0) {
        dt = max(treeX0 - ax, p.y - hiN);
    } else {
        vec2 cell = floor(p.xz / WI_TC);
        vec2 cc = (cell + 0.5) * WI_TC;
        uint u = uhash3(uvec3(ivec3(int(cell.x), 7, int(mod(cell.y, WI_ZWRAP / WI_TC)))));
        float h = uhashUnit(u);
        float h2 = uhashUnit(uhash(u));
        float h3 = uhashUnit(uhash(u ^ 0x5bd1u));
        float dTree = 1e3;
        if (h < sPines && wiBankAt(abs(cc.x), cc.y) > 0.6) {
            // Jitter 0.15 plus the widest base 0.8 stays inside the half-cell 1.0.
            vec2 f = p.xz - cc - (vec2(h, h3) - 0.5) * 0.3;
            float base = wiGround(cc) - 0.6;
            float H = (1.4 + 1.6 * h2) * sTreeH;
            float R = 0.55 + 0.25 * h3;
            float lower = max(wiCone(f, p.y, base + 0.7 * H, R / (0.7 * H)), base - p.y);
            float upper = max(wiCone(f, p.y, base + H, (0.7 * R) / (0.55 * H)), base + 0.4 * H - p.y);
            dTree = min(lower, upper);
        }
        vec2 af = abs(p.xz - cc);
        float toEdge = 0.5 * WI_TC - max(af.x, af.y);
        dt = min(dTree, max(toEdge + 0.02, p.y - hiN));
    }

    gMat = dt < dg ? 1.0 : 0.0;
    float d = min(dg, dt);
    return max(d, WI_BUBBLE - length(p - gEye));
}

/** mapField through lib_shell's hooks: exactly mapField(p) at rest, and mapField read once, in a loop the compiler cannot unroll. */
float mapFieldGrown(vec3 p) {
    shellStart3(p, SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK);
    float r = 0.0;
    for (int i = SHELL_FROM; i < 4; i++) {
        if (shellPass3(i)) r = shellKeep3(i, mapField(shellAt3(i)));
    }
    return r;
}

/** The banks and pines as the shell sees them: spawn, fractal, front and back route through here (identity at rest). */
float map(vec3 p) {
    return mapFieldGrown(p);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** Half-width of the open lead; Morph thaws it wider. */
float wiLeadW(float z) {
    return min(2.2 + 0.9 * sin(z * (WI_TAU / 32.0) + 1.0) + sThaw, wiWMin() - 0.8);
}

/**
 * The lead's rings: one source per 16 units of z, the nearest three summed,
 * each tuned to a band as the 2D style's three were (bass, mids, treble).
 */
float wiRipple(vec2 xz, float bassAtt, float mid, float treble) {
    float ci = floor(xz.y / 16.0);
    float sum = 0.0;
    for (int k = -1; k <= 1; k++) {
        float idx = ci + float(k);
        float wi = mod(idx, 4.0);
        uint u = uhash(uint(wi) + 0x9e37u);
        float h1 = uhashUnit(u);
        float h2 = uhashUnit(uhash(u));
        float wander = sWander;
        vec2 src = vec2((h1 - 0.5) * 2.4 + wander * sin(uTime * (0.17 + 0.1 * h2) + h1 * 6.0),
                        (idx + 0.5) * 16.0 + (h2 - 0.5) * 8.0 + 0.8 * cos(uTime * 0.13 + h2 * 5.0));
        float r = length(xz - src);
        float freq;
        float amp;
        float speed;
        if (wi < 1.5) {
            freq = (wi < 0.5 ? 3.2 : 4.1) * (wi < 0.5 ? 1.0 + 0.35 * bassAtt : 1.0);
            amp = wi < 0.5 ? 1.0 : 0.5 + mid * 1.6;
            speed = wi < 0.5 ? 3.2 : 4.1;
        } else {
            freq = wi < 2.5 ? 5.2 : 3.6 * (1.0 + 0.35 * bassAtt);
            amp = wi < 2.5 ? 0.3 + treble * 1.4 : 1.0;
            speed = wi < 2.5 ? 5.0 : 3.5;
        }
        sum += sin(r * freq - uTime * speed) * exp(-r * 0.22) * amp;
    }
    return sum;
}

/** The lead's rings as one 2D pattern, so the shell can route it (its shell wrapper). */
float wiRippleField(vec2 xz) {
    return wiRipple(xz, gBassAtt, gMid, gTreble);
}

vec3 wiSky(vec3 rd, vec3 moon, float hue) {
    float up = clamp(rd.y, -1.0, 1.0);
    vec3 col = mix(pal(hue + 0.62) * 0.07, pal(hue + 0.72) * 0.015, smoothstep(-0.05, 0.6, up));
    float m = max(dot(rd, moon), 0.0);
    col += vec3(0.85, 0.9, 1.0) * smoothstep(0.9994, 0.9997, m);
    col += pal(hue + 0.55) * (0.10 * pow(m, 24.0) + 0.05 * m * m);
    vec3 sp = rd * 150.0;
    float s = hashCell(ivec3(floor(sp)));
    float star = (1.0 - smoothstep(0.0, 0.3, length(fract(sp) - 0.5))) * step(0.997, s);
    col += vec3(0.75) * star * smoothstep(0.05, 0.35, up) * (0.6 + 0.4 * sin(uTime * 2.0 + s * 500.0));
    return col;
}

/** Voronoi on the ice: x = nearest feature, y = border (f2 - f1). Cells wrap at 128 along z. */
vec2 wiFrost(vec2 xz, float scale) {
    vec2 fq = xz * scale;
    vec2 fcell = floor(fq);
    float f1 = 8.0;
    float f2 = 8.0;
    float wrap = WI_ZWRAP * scale;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 g = vec2(float(x), float(y));
            vec2 c = fcell + g;
            uint u = uhash3(uvec3(ivec3(int(c.x), int(mod(c.y, wrap)), 3)));
            vec2 o = vec2(uhashUnit(u), uhashUnit(uhash(u)));
            o = 0.5 + 0.4 * sin(uTime * 0.25 + WI_TAU * o);
            float d = length(g + o - fract(fq));
            if (d < f1) {
                f2 = f1;
                f1 = d;
            } else if (d < f2) {
                f2 = d;
            }
        }
    }
    return vec2(f1, f2 - f1);
}

/** The ice's frost as one 2D pattern (nearest feature, border, 0), so the shell can route it (its shell wrapper). */
vec3 wiFrostField(vec2 xz) {
    return vec3(wiFrost(xz, sFrost), 0.0);
}

/**
 * The snowfall: flakes on planes of constant z, 1.6 apart, the next six ahead
 * of the eye along the ray. The planes are fixed in the world, so a flake
 * stays put as the eye flies through it, and nearer flakes slide past faster:
 * real parallax rather than the 2D layers' faked one.
 */
float wiSnow(vec3 ro, vec3 rd, float sceneT, vec2 wind, float density, float treble, float px1) {
    if (abs(rd.z) < 0.02) return 0.0;
    float sgn = rd.z >= 0.0 ? 1.0 : -1.0;
    float base = sgn > 0.0 ? floor(ro.z / WI_SNOW_S) + 1.0 : ceil(ro.z / WI_SNOW_S) - 1.0;
    float planes = WI_ZWRAP / WI_SNOW_S;
    float snow = 0.0;
    for (int k = 0; k < WI_SNOW_N; k++) {
        float idx = base + sgn * float(k);
        float t = (idx * WI_SNOW_S - ro.z) / rd.z;
        if (t > sceneT) break;
        vec3 p = ro + rd * t;
        float pi = mod(idx, planes);
        float ps = uhashUnit(uhash(uint(pi) + 0x51edu));
        vec2 q = p.xy;
        q.x += wind.x * (1.0 + 0.15 * ps) + 0.4 * sin(uTime * (0.5 + 0.3 * ps) + pi * 1.7) * (0.5 + uSway);
        // The fall wraps at 256 cells, the same period the cell hash wraps at
        // below, so the wrap lands on an identical picture.
        q.y += mod(uTime * (0.45 + 0.35 * ps), WI_SNOW_C * 256.0) + wind.y;
        vec2 cq = q / WI_SNOW_C;
        vec2 cell = floor(cq);
        uint u = uhash3(uvec3(ivec3(int(cell.x), int(mod(cell.y, 256.0)), int(pi))));
        float seed = uhashUnit(u);
        if (seed > density) continue;
        vec2 jitter = vec2(uhashUnit(uhash(u)), uhashUnit(uhash(u ^ 0x2c1bu)));
        vec2 fpos = (fract(cq) - (0.2 + 0.6 * jitter)) * WI_SNOW_C;
        float r = WI_SNOW_C * (0.07 + 0.09 * jitter.x) * sFlake;
        // Never thinner than a pixel; the light a sub-pixel flake would spread
        // over its pixel is kept by the area ratio, so far snow dims, not shimmers.
        float re = max(r, px1 * t * 0.8);
        float flake = smoothstep(re, re * 0.3, length(fpos)) * (r * r) / (re * re);
        float twinkle = 0.65 + 0.35 * sin(uTime * (2.0 + seed * 5.0) + seed * 40.0 + treble * 6.0);
        float fade = smoothstep(0.2, 1.0, t) * exp(-t * 0.12);
        snow += flake * twinkle * fade * (0.55 + 0.45 * step(0.25, jitter.y));
    }
    return snow;
}

/** The ground pattern wiRippleField through lib_shell's hooks: exactly wiRippleField(uv) at rest, and wiRippleField read once, in a loop the compiler cannot unroll. */
float wiRippleFieldGrownW(vec2 uv) {
    shellStart2(SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK);
    float r = float(0.0);
    for (int i = SHELL_FROM; i < 3; i++) {
        if (!shellPass2(i)) continue;
        float v = wiRippleField(shellAt2(uv, i, SHELL_GROUND2));
        r = i == 0 ? v : shellBlend2(r, v, shellWeight2(i));
    }
    return r;
}

/** The ground pattern wiFrostField through lib_shell's hooks: exactly wiFrostField(uv) at rest, and wiFrostField read once, in a loop the compiler cannot unroll. */
vec3 wiFrostFieldGrownW(vec2 uv) {
    shellStart2(SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK);
    vec3 r = vec3(0.0);
    for (int i = SHELL_FROM; i < 3; i++) {
        if (!shellPass2(i)) continue;
        vec3 v = wiFrostField(shellAt2(uv, i, SHELL_GROUND2));
        r = i == 0 ? v : shellBlend2(r, v, shellWeight2(i));
    }
    return r;
}

void main() {
    // ---- shape inputs: each normalized input mapped to its range, here only (see the header).
    sBump = shellMap(shellKnob(1), 0.15, WI_BUMP, 0.6);
    sFrost = shellMap(shellKnob(1), 1.0, 1.8, 2.6);
    sPines = shellMap(shellKnob(1), 0.3, 0.6, 0.9);
    sMoonY = shellMap(shellKnob(3), 0.045, 0.32, 0.595);
    sRise = shellMap(shellKnob(4), 1.6, WI_RISE, 3.8);
    sBank = shellMap(shellKnob(4), 5.5, WI_BANK, 3.0);
    sTreeH = shellMap(shellKnob(4), 0.6, 1.0, 1.3);
    sFlake = shellMap(shellKnob(4), 0.7, 1.0, 1.5);
    sShore = shellMap(shellKnob(5), 5.0, WI_SHORE, 7.0);
    sFog = shellMap(shellKnob(5), 0.063, 0.045, 0.027);
    sThaw = shellMapS(clamp(uMorph, 0.0, 1.0), -1.6, 0.0, 1.6);
    sWander = shellMapS(clamp(uWarp, 0.0, 1.0), -0.8, 0.6, 2.0);
    // A steeper bank or bigger bumps raise the height field's slope by at most this factor (1 at rest).
    float steep = max((sRise / WI_RISE) * (WI_BANK / sBank), sBump / WI_BUMP);
    gGlip = WI_GLIP / max(steep, 1.0);

    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 64, so this rebase is exact
    // (power of two) and keeps z precise for the hashes and the normals.
    ro.z = mod(ro.z, WI_ZWRAP);
    gEye = ro;
    // The grow hooks (lib_shell) open round this eye, in the rebased frame the world is read in.
    shellSetup3(ro, 3.0, 0.5 * WI_TC, false);
    // The ground's grow hooks (lib_shell's its shell wrapper) open round the eye where it stands on it.
    shellSetup2(ro.xz, 5.0);

    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float energy = clamp(sndShaped(SND_LEVEL), 0.0, 1.5);
    float bassAtt = sndLevel(SND_BASS_ATT);
    float mid = sndShaped(SND_MIDDLE);
    float treble = sndLevel(SND_TREBLE);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    gBassAtt = bassAtt;
    gMid = mid;
    gTreble = treble;
    float hue = 0.15 * (uKnob[3] - 0.5);
    float t0 = uTime;

    // Wind: Sway and Turbulence steer it, the mids push it, the FLOW knob leans it.
    vec2 wind = vec2(
        sin(t0 * 0.23) * (0.35 + uSway) + uTurbulence * 0.5 * sin(t0 * 0.61) + 1.2 * (uKnob[2] - 0.5),
        cos(t0 * 0.17) * 0.18 * (0.3 + mid)
    );
    vec3 moon = normalize(vec3(-0.3, sMoonY, 1.0));

    // The pond, analytically: the lowest surface there is.
    float tPlane = rd.y < -1e-4 ? (WI_ICE - ro.y) / rd.y : 1e5;

    float t = 0.03;
    float hitT = -1.0;
    float limit = min(WI_FAR, tPlane);
    for (int i = 0; i < WI_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (t > limit) break;
        float d = map(ro + rd * t);
        if (d < 0.0015 + 0.0008 * t) {
            hitT = t;
            break;
        }
        t += d * 0.9;
    }
    bool pond = false;
    if (hitT < 0.0 && tPlane < WI_FAR) {
        hitT = tPlane;
        pond = true;
    }

    vec3 sky = wiSky(rd, moon, hue);
    vec3 col = sky;
    float sceneT = hitT > 0.0 ? hitT : WI_FAR;
    float fogK = sFog;
    vec3 fogCol = pal(hue + 0.62) * 0.05 + vec3(0.01, 0.012, 0.018);

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        float ax = abs(p.x);
        float shoreW = wiShoreW(p.z);
        // The shoreline's spectrum, read along its length (a triangle, so the
        // 32-unit period meets itself without a seam).
        float along = abs(fract(p.z / 32.0) * 2.0 - 1.0);
        float shore = 1.0 - smoothstep(0.0, 0.7, abs(ax - shoreW));
        vec3 shimmer = pal(hue + 0.35) * shore * sndSpectrum(along) * 0.35;

        if (pond) {
            float lead = wiLeadW(p.z);
            float water = 1.0 - smoothstep(lead - 0.3, lead + 0.3, ax);
            // Rings: height from the lead's sources, faded with distance so
            // they never alias into sparkle near the horizon.
            float amp = 0.05 * (1.0 + 0.8 * kick + 2.0 * (uKnob[0] - 0.5)) * exp(-hitT * 0.05) * water;
            float e = 0.03;
            float w0 = wiRippleFieldGrownW(p.xz);
            float wx = wiRippleFieldGrownW(p.xz + vec2(e, 0.0));
            float wz = wiRippleFieldGrownW(p.xz + vec2(0.0, e));
            vec3 n = normalize(vec3(-(wx - w0) / e * amp, 1.0, -(wz - w0) / e * amp));
            float ndv = clamp(dot(n, -rd), 0.0, 1.0);
            vec3 rf = reflect(rd, n);

            // Open water: dark, the rings' crests shimmering, the sky and the
            // moon's streak in it.
            float crest = pow(clamp(w0 * 0.5 + 0.5, 0.0, 1.0), 3.0);
            float fres = 0.03 + 0.97 * pow(1.0 - ndv, 5.0);
            vec3 waterCol = pal(hue + 0.15 + w0 * 0.25) * (0.03 + 0.12 * crest + energy * 0.08)
                          + wiSky(rf, moon, hue) * fres * 0.9
                          + vec3(0.9, 0.95, 1.0) * pow(max(dot(rf, moon), 0.0), 180.0) * 1.2;

            // Ice: frosted (a blurred, dimmer reflection), crazed with cracks
            // that glint on treble, brightest at the crystals' feature points.
            float density = sFrost;
            vec2 fr = wiFrostFieldGrownW(p.xz).xy;
            float pxw = 2.0 * hitT / (uResolution.y * uCamFocal * max(abs(rd.y), 0.08)) * density;
            float crack = 1.0 - smoothstep(0.0, 0.05 + pxw, fr.y);
            float glint = pow(1.0 - clamp(fr.x, 0.0, 1.0), 8.0);
            vec3 nI = normalize(vec3(0.0, 1.0, 0.0) + vec3(0.06 * (fr.x - 0.5), 0.0, 0.06 * (fr.y - 0.2)));
            vec3 rI = normalize(mix(reflect(rd, nI), vec3(0.0, 1.0, 0.0), 0.35));
            float dif = max(dot(nI, moon), 0.0);
            vec3 iceCol = pal(hue + 0.58) * (0.04 + 0.12 * dif)
                        + wiSky(rI, moon, hue) * (0.25 + 0.5 * pow(1.0 - ndv, 3.0))
                        + pal(hue + 0.55) * crack * (0.10 + treble * 0.9) * exp(-hitT * 0.03)
                        + vec3(0.9, 0.95, 1.0) * glint * treble * 0.5;
            // The thin rim where the ice gives onto the lead.
            float rim = (1.0 - smoothstep(0.0, 0.12, abs(ax - lead))) * 0.25;

            col = mix(iceCol, waterCol, water) + pal(hue + 0.5) * rim + shimmer;
        } else {
            vec3 n = normalAt(p, max(0.0015, 0.001 * hitT));
            float occ = 0.0;
            float sc = 1.0;
            for (int k = 0; k < 3; k++) {
                float hk = 0.1 + 0.16 * float(k);
                occ += (hk - map(p + n * hk)) * sc;
                sc *= 0.65;
            }
            float ao = clamp(1.0 - 1.6 * occ, 0.25, 1.0);
            // Restore the hit's material after the normal and AO taps.
            map(p);

            float dif = max(dot(n, moon), 0.0);
            float amb = 0.06 + 0.10 * clamp(n.y, 0.0, 1.0);
            vec3 snowAlb = mix(vec3(0.85, 0.9, 1.0), pal(hue + 0.6), 0.2);
            vec3 lit = snowAlb * (amb + 0.55 * dif) * ao;
            if (gMat > 0.5) {
                // Pine: dark needles, snow lying on what faces up.
                float cap = smoothstep(0.25, 0.65, n.y + 0.15 * sin(p.y * 9.0 + p.x * 5.0));
                vec3 needles = pal(hue + 0.3) * 0.03 * (0.5 + dif) * ao;
                col = mix(needles, lit, cap);
            } else {
                col = lit + shimmer;
            }
            // Sparkle on the snow: sparse points that glint with treble. z
            // wrapped like everything else (64 * 6 is whole, so the seam is a
            // cell wall), or the sparkle would re-hash at every rebase.
            vec3 gp = vec3(p.xy, mod(p.z, WI_ZWRAP)) * 6.0;
            float g = hashCell(ivec3(floor(gp)));
            float tw = 0.5 + 0.5 * sin(uTime * (7.0 + 11.0 * g) + g * 60.0);
            float pt = 1.0 - smoothstep(0.0, 0.3, length(fract(gp) - 0.5));
            col += vec3(0.9, 0.95, 1.0) * step(0.95 - 0.05 * treble, g) * pt * tw * (0.1 + 0.9 * treble) * (0.3 + dif);
        }
        col = mix(col, fogCol, 1.0 - exp(-hitT * fogK));
    }

    // Snow in front of whatever the ray met; near-white with a hint of the
    // palette so it reads as snow on any colour scheme.
    float density = clamp(0.4 + 0.6 * (uKnob[4] - 0.5) + 0.3 * kick + 0.15 * uTurbulence, 0.1, 0.9);
    float px1 = 2.0 / (uResolution.y * uCamFocal);
    float snow = wiSnow(ro, rd, sceneT, wind, density, treble, px1);
    col += snow * mix(vec3(0.95), pal(hue + 0.6), 0.25) * (0.8 + energy * 0.5);

    col *= 0.8 + 0.25 * swell + 0.12 * energy;
    col *= 0.7 + 0.3 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
