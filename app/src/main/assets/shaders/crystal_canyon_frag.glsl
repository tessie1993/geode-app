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

// Crystal Canyon: a flight down a canyon whose walls are faceted crystal
// pillars, hexagonal prisms with pointed tips standing on a grid. Short ones
// carpet the floor, and they rise into walls on both sides.
//
// The camera is the shared rig (camFly). The canyon runs straight down +z and
// every z term has a power-of-two period, so the rig's 8192 wrap is invisible.
//
// ---- audio ------------------------------------------------------------------
//
// Kick (SND_KICK_ENV) grows the crystals just ahead of the eye. That is geometry:
// their tips shoot up and they thicken a little, and nothing gets brighter.
// A pitch change (SND_HELD_NOTE_CHANGE) slides the crystal hue. Air (sndSpectrum(0.93)) thickens
// and brightens the sparkle glints on the facets. Overall level rides SND_ENERGY_TREND.
//
// ---- knobs ------------------------------------------------------------------
//
// Flame's, Opal's and Hollow's roles, each read as its offset from 0.5 so off
// is the tuned look: k0 PULSE the emission, k1 TEXTURE the height of the tips'
// glowing band, k2 FLOW the canyon's bend (its width's wobble, 0.4..1.0, so a
// neighbour one cell out stays inside hiN's 1.1), k3 TONE the palette
// position and how high the walls tower (a high melody raises them into tall
// spires, a low one settles them into low mesas; hiN follows it), k4 BUILD
// the tips' glow, k5 SPACE the reach (far plane and haze).
//
// ---- shape inputs (lib_shell) ----------------------------------------------
//
// Every tunable of the canyon is a normalized input, mapped to its range in
// one place (the top of main) and read from globals. 0.5 is the look as tuned:
// the knobs rest there exactly while Note & key is off. Only knobs drive them
// here: this style leaves Morph, Warp and Turbulence to view(), which already
// bends its picture with them. Music reaches these only through the knobs (the
// reaction matrix); none reads the sound.
//
//   input       driver           lo .. rest .. hi
//   sKick       k0 PULSE         a full kick's tip growth 0.8 .. 1.5 .. 2.2
//   sThick      k1 TEXTURE       the pillars' thickness 0.65 .. 1 .. 1.1
//   sTip        k1 TEXTURE       the tips' point (face tilt) 0.6 .. 1.3 .. 2.4
//   gBend       k2 FLOW          the width's wobble 0.4 .. 0.7 .. 1.0
//   gRise       k3 TONE          the walls' height 5.7 .. 9.5 .. 13.3
//   sCarpet     k4 BUILD         the floor crystals' spread of height 0.3 .. 0.7 .. 1.4
//   sBank       k4 BUILD         the run up to the wall tops 4.4 .. 3.4 .. 2.4
//   sWidth      k5 SPACE         the open floor's half-width 3.6 .. 4.4 .. 5.6
//   reach       k5 SPACE         far plane and haze 0.8 .. 1 .. 1.2
//
// The cell (1) and its hash wrap (1024) stay constants: the wrap is the power
// of two the eye's rebase relies on, and 1.1 keeps the widest pillar inside
// its cell. The canyon goes through its shell wrapper, so the shell's spawn,
// fractal, front and back hooks reach it.
//
// ---- why the estimate is safe ----------------------------------------------
//
// Each cell measures only its own pillar. A neighbour can be taller, so the
// distance is clamped to the cell boundary unless the point is above every
// neighbour's possible tip (hiN). A sphere around the eye is carved out of
// everything, so the eye can never sit inside a crystal.

#define CC_MAX_STEPS 96
#define CC_FAR 46.0
#define CC_TAU 6.2831853
#define CC_FLOOR -3.6
/** How far the wall pillars rise above the floor. */
#define CC_RISE 9.5
/** Half-width of the open floor before the walls start to climb. */
#define CC_WIDTH 4.4
/** Horizontal run of the slope from the floor up to the wall tops. */
#define CC_BANK 3.4
/** Tip growth on a full kick, world units. */
#define CC_KICK 1.5
#define CC_BUBBLE 0.9
/** Cells along z before the hash repeats: 1024 world units, which divides 8192. */
#define CC_ZWRAP 1024.0

float gH;
float gH2;
float gWall;
float gTop;
float gMat;
/** The eye, z rebased into 0..1024 (see main). */
vec3 gEye;
/** k2 FLOW: the width's wobble, world units (0.7 at rest). */
float gBend = 0.7;
/** k3 TONE: how far the wall pillars rise (CC_RISE at rest). */
float gRise = CC_RISE;
/** Shape inputs (see the header). */
float sKick;
float sThick;
float sTip;
float sCarpet;
float sBank;
float sWidth;

float canyonWidth(float z) {
    return sWidth + gBend * sin(z * (CC_TAU / 64.0));
}

float wallAt(float ax, float z) {
    float w = canyonWidth(z);
    return smoothstep(w, w + sBank, ax);
}

float mapField(vec3 p) {
    vec2 cell = floor(p.xz);
    vec2 cc = cell + 0.5;
    vec2 f = p.xz - cc;
    uint u = uhash3(uvec3(ivec3(int(cell.x), 0, int(mod(cell.y, CC_ZWRAP)))));
    float h = uhashUnit(u);
    float h2 = uhashUnit(uhash(u));

    float ax = abs(cc.x);
    float wall = wallAt(ax, cc.y);
    float kickAll = clamp(SND_KICK_ENV, 0.0, 1.0);
    // Only the crystals just ahead of the eye answer the kick.
    float kick = kickAll * exp(-abs(cc.y - gEye.z - 4.0) * 0.14);
    float top = CC_FLOOR + 0.25 + sCarpet * h + gRise * wall * (0.62 + 0.38 * h2)
              + sKick * kick * (0.3 + 0.7 * wall);
    // Largest apothem 0.341 (0.375 at TEXTURE's thickest) plus the 0.07 jitter stays inside the half-cell.
    float r = (0.23 + 0.08 * h2) * sThick * (1.0 + 0.1 * kick);

    vec2 q = rot2(h2 * CC_TAU) * (f - (vec2(h, h2) - 0.5) * 0.1);
    vec2 aq = abs(q);
    // Hexagonal cross-section (max of face planes: a lower bound at corners).
    float pr = max(aq.x * 0.8660254 + aq.y * 0.5, aq.y);
    float side = pr - r;
    // Pointed tip: the face planes tilted by sTip (1.3 at rest), normalised by
    // sqrt(1 + sTip^2) (1.64 at rest, where the ratio below is exactly 1).
    float cap = (p.y - top + sTip * pr) / (1.64 * sqrt((1.0 + sTip * sTip) / (1.0 + 1.3 * 1.3)));
    float dc = max(side, cap);

    // A neighbour one cell further out (plus the width's z wobble) is the
    // tallest one could be.
    float hiN = CC_FLOOR + 0.25 + sCarpet + gRise * wallAt(ax + 1.1, cc.y) + sKick * kickAll;
    vec2 af = abs(f);
    float toEdge = 0.5 - max(af.x, af.y);
    float d = min(dc, max(toEdge + 0.02, p.y - hiN));

    float df = p.y - CC_FLOOR;
    gMat = df < dc ? 1.0 : 0.0;
    d = min(d, df);
    d = max(d, CC_BUBBLE - length(p - gEye));

    gH = h;
    gH2 = h2;
    gWall = wall;
    gTop = top;
    return d;
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

/** The canyon as the shell sees it: spawn, fractal, front and back route through here (identity at rest). */
float map(vec3 p) {
    return mapFieldGrown(p);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

vec3 skyColour(vec3 rd, float hueBase) {
    float up = clamp(rd.y, -1.0, 1.0);
    vec3 col = mix(pal(hueBase + 0.55) * 0.10, pal(hueBase + 0.70) * 0.03, smoothstep(0.0, 0.7, up));
    // The glow at the end of the canyon, straight down the travel axis.
    float ahead = max(dot(rd, normalize(vec3(0.0, 0.12, 1.0))), 0.0);
    col += pal(hueBase + 0.08) * (0.30 * pow(ahead, 10.0) + 0.08 * ahead * ahead);
    vec3 sp = rd * 140.0;
    float s = hashCell(ivec3(floor(sp)));
    float star = (1.0 - smoothstep(0.0, 0.3, length(fract(sp) - 0.5))) * step(0.996, s);
    col += vec3(0.8) * star * smoothstep(0.05, 0.3, up) * (0.6 + 0.4 * sin(uTime * 3.0 + s * 500.0));
    return col;
}

void main() {
    // ---- shape inputs: each normalized input mapped to its range, here only (see the header).
    sKick = shellMap(shellKnob(0), 0.8, CC_KICK, 2.2);
    sThick = shellMap(shellKnob(1), 0.65, 1.0, 1.1);
    sTip = shellMap(shellKnob(1), 0.6, 1.3, 2.4);
    gBend = shellMap(shellKnob(2), 0.4, 0.7, 1.0);
    gRise = shellMap(shellKnob(3), CC_RISE * 0.6, CC_RISE, CC_RISE * 1.4);
    sCarpet = shellMap(shellKnob(4), 0.3, 0.7, 1.4);
    sBank = shellMap(shellKnob(4), 4.4, CC_BANK, 2.4);
    sWidth = shellMap(shellKnob(5), 3.6, CC_WIDTH, 5.6);
    float reach = shellMap(shellKnob(5), 0.8, 1.0, 1.2);

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
    ro.z = mod(ro.z, CC_ZWRAP);
    gEye = ro;
    // The grow hooks (lib_shell) open round this eye, in the rebased frame the world is read in.
    shellSetup3(ro, 3.0, 0.5, false);

    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float energy = clamp(sndShaped(SND_LEVEL), 0.0, 1.5);
    float air = clamp(sndSpectrum(0.93), 0.0, 1.0);
    float hueBase = 0.22 * clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0) + 0.6 * kTone;

    float t = 0.05;
    float hitT = -1.0;
    for (int i = 0; i < CC_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * t;
        float d = map(p);
        if (d < 0.0015 + 0.0008 * t) {
            hitT = t;
            break;
        }
        t += d * 0.9;
        if (t > CC_FAR * reach) break;
    }

    vec3 sky = skyColour(rd, hueBase);
    vec3 col = sky;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.001 * hitT));
        float occ = 0.0;
        float sc = 1.0;
        for (int k = 0; k < 3; k++) {
            float hk = 0.1 + 0.16 * float(k);
            occ += (hk - map(p + n * hk)) * sc;
            sc *= 0.65;
        }
        float ao = clamp(1.0 - 1.6 * occ, 0.25, 1.0);
        // Restore the hit cell's globals after the normal and AO taps.
        map(p);

        vec3 L = normalize(vec3(-0.35, 0.85, 0.4));
        float dif = max(dot(n, L), 0.0);
        float fres = pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 3.0);
        float spec = pow(max(dot(reflect(rd, n), L), 0.0), 48.0);
        float hue = hueBase + 0.18 * gH + 0.22 * gWall + 0.08 * sin(p.z * (CC_TAU / 32.0));
        // Fake refraction: the bent ray picks a spot on the palette, and the
        // three channels read it at staggered offsets, which is the dispersion.
        vec3 rf = refract(rd, n, 0.67);
        float kp = hue + 0.45 * (0.5 + 0.5 * dot(rf, vec3(0.42, 0.62, 0.66)));
        vec3 prism = vec3(pal(kp).r, pal(kp + 0.07).g, pal(kp + 0.14).b);

        if (gMat > 0.5) {
            col = pal(hue + 0.55) * 0.05 * ao + prism * fres * 0.25;
        } else {
            // Tips glow from inside; the shaft stays darker glass.
            float tipGlow = smoothstep(gTop - 2.4 * (1.0 + kTexture), gTop, p.y);
            vec3 body = pal(hue) * (0.10 + 0.30 * dif) * ao + pal(hue + 0.1) * tipGlow * (0.18 + 0.12 * swell) * (1.0 + kBuild);
            col = body + prism * (0.15 + 0.85 * fres) * (0.35 + 0.4 * ao)
                + mix(vec3(1.0), prism, 0.4) * spec * 0.55;
        }

        // Glints: sparse points on the facets that twinkle; air brings more of
        // them and brightens them. Each is a few pixels, never the frame.
        // z wrapped like the cells (1024 * 7 is whole, so the seam is a cell
        // wall), or the glints would re-hash at every rebase of the eye.
        vec3 gp = vec3(p.xy, mod(p.z, CC_ZWRAP)) * 7.0;
        float g = hashCell(ivec3(floor(gp)));
        float tw = 0.5 + 0.5 * sin(uTime * (9.0 + 13.0 * g) + g * 60.0);
        float pt = (1.0 - smoothstep(0.0, 0.32, length(fract(gp) - 0.5)));
        col += mix(vec3(1.0), prism, 0.3) * step(0.93 - 0.06 * air, g) * pt * tw
             * (0.12 + 1.4 * air) * (0.3 + 0.7 * fres + spec);

        col = mix(col, sky, 1.0 - exp(-hitT * 0.05 / reach));
    }

    col *= 0.78 + 0.30 * swell + 0.10 * energy;
    col *= 0.7 + 0.3 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col * (1.0 + 0.5 * kPulse)), 1.0);
}
