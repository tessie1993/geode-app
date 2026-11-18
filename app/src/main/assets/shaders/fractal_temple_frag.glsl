#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Morph, Warp and Turbulence are spent on the temple itself (below), so view()
// must not bend the screen with them a second time.
#define VIEW_OWNS_SHAPE

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_touch
//#include lib_shell

// Fractal Temple: a walk down a stepped causeway through an endless temple.
// The 2D style drew the same four things flat (a triangulated dome closing
// overhead, a portal of neon lamp rings, the causeway running to the horizon,
// filigree spires either side); now they are built and lit:
//
//   the causeway   separate stone treads, one per world unit, with a lit
//                  nosing on each and a lit edge down both sides;
//   the spires     one column each side, one spire per 4 units: a tapered cone
//                  of stacked, scalloped tiers (the lace towers), its courses
//                  and ribs drawn as filigree light, a star at the tip;
//   the vault      a domed bay every 32 units: the open hall is the union of
//                  one big sphere per bay, so neighbouring domes meet in arches,
//                  and the shell is pierced by a near-equilateral triangle
//                  lattice, so it is lace with the glowing sky behind it;
//   the portal     three concentric lamp rings hanging under every crown,
//                  staggered along the way, which the walk passes through.
//                  They light the hall (a point light at the nearest portal)
//                  and throw a halo into the air.
//
// ---- camera ------------------------------------------------------------------
//
// camFly down +z. The rings' inner radius (3.8) is wider than the glide path's
// wander, the hall is 20 wide at eye level, and a sphere around the eye is
// carved out of everything, so orbit and hover paths stay clear too. The eye's
// z is rebased by TF_ZWRAP = 64; every z term has a period dividing it: treads
// 1, spire cells 4 (hashed by cell index mod 16), bays and portals 32, the
// Warp bend 64. The lattice's slanted families advance 0.875 per unit of z, so
// a 64 shift moves them 56 / s periods, a whole number for each of the three
// tessellations s = 4, 8/3, 2. 64 divides the rig's 8192 travel wrap, so
// neither the rebase nor the wrap ever shows.
//
// ---- audio -------------------------------------------------------------------
//
// SND_INTENSITY            opens the dome (the bays grow, the lace thins) and
//                          thins the fog, so the causeway reaches further.
// Low (sndShaped(SND_LOW)) lights the portal: its lamps, its halo and the light
//                          it throws on the hall.
// Treble presence          picks out the filigree on the spires and the lit
//                          facets of the dome.
// Kick (SND_KICK_ENV)      the spire stars swell and the rings breathe out a
//                          little; an envelope, so they swell, never strobe.
// Section (SND_CUT_COUNT)  re-seeds the skyline (spire heights, places, hues),
//                          steps the dome between its three tessellations and
//                          re-picks the lit facets, all crossfaded over the
//                          section envelope (SND_HELD_HARD_CUT). This is what
//                          the old spike re-seed and form phase did.
// Drop (SND_HELD_SURGE)    swings the palette to its far side, as a hump.
// SND_ENERGY_TREND         overall level.
//
// Params: Morph deepens the scallops between the spire tiers, Warp bends the
// whole nave from side to side, Turbulence sways the spires, Intensity and the
// palette reach through lib_scene_grade and pal(). Knobs (offsets from 0.5,
// so off is the tuned look): k0 PULSE the kick's swell, k1 TEXTURE the lace
// and filigree, k2 FLOW where the lamps' chase stands (an offset on its
// clock, never a rate times it, so a moving knob never jumps it), k3 TONE the portals' span and a
// small hue offset (a high melody throws the rings wide like open arches, a
// low one draws them in tight round the causeway, still clear of it), k4 BUILD
// the sky and halo, k5 SPACE the dome's size.
//
// ---- why the distance estimate is safe ----------------------------------------
//
// Treads: one box per unit cell, half-length 0.44 inside the 0.5 half-cell, so
// the repeat is exact. Ground: a plane. Rings: exact tori. Vault: the hall is
// the union of the two nearest bay spheres (farther ones are always further
// away), its complement max(R - |p - c|) is exact, the shell is that offset
// both ways, and the lace bars are distances to lines in the horizontal plane
// divided by each family's normal length: a horizontal projection is
// 1-Lipschitz, the min and a convex mix of two lattices stay 1-Lipschitz, and
// max() with the shell keeps the bound. Spires: the radial term is divided by
// TF_SPIRE_LIP, which bounds the profile's slope (taper 1.3 / 2.4 at the
// shortest spire plus the scallop 1.3 * 0.36 * 0.5 * TAU * 0.9 at full Morph,
// ~1.86; sqrt(1 + 1.86^2) < 2.2). Each cell
// measures only its own spire, which stays within 1.85 of the cell centre
// (0.25 jitter, 1.3 radius, 0.3 sway), so the result is clamped to the z cell
// edge plus 0.15, and to the mirror plane plus 3.1 for the other side's
// column. The Warp bend shifts x by 1.2 * sin(TAU z / 64), slope < 0.12, which
// the 0.8 step scale absorbs along with the sway's 0.03 shear. A sphere around
// the eye is carved out of everything, so the eye is never inside the temple.

#define TF_MAX_STEPS 100
#define TF_TAU 6.2831853
/** Rebase period of the eye's z; divides 8192. */
#define TF_ZWRAP 64.0
/** Tread tops. */
#define TF_FLOOR -2.2
/** The ground the spires and domes stand on, under the causeway. */
#define TF_GROUND -3.4
/** Half-width of the causeway. */
#define TF_WIDTH 2.4
/** Height of the portal rings' centre. */
#define TF_RING_Y 0.6
/** Bay length: one dome and one portal per 32 units. */
#define TF_BAY 32.0
/** Half-thickness of the dome shell. */
#define TF_SHELL 0.35
/** Lipschitz bound of the spire's radial profile. */
#define TF_SPIRE_LIP 2.2
/** The slanted lattice families' z rate: 64 * 0.875 = 56, whole for every tessellation. */
#define TF_SLANT 0.875
/** |(0.5, 0.875)|, the slanted families' normal length. */
#define TF_SLANT_LEN 1.0077822
#define TF_FAR 64.0
#define TF_BUBBLE 0.7

/** Warp's sideways bend of the nave, world units. */
float gBend;
/** Dome radius. */
float gRD;
/** The old and new tessellation sizes and bar half-widths, crossfaded by gSw. */
float gS0;
float gS1;
float gW0;
float gW1;
/** Section crossfade: 0 at the cut (old look), 1 once it has landed (new look). */
float gSw;
uint gSeed0;
uint gSeed1;
/** Turbulence's spire sway (shear per unit height) and Morph's scallop depth. */
float gLean;
float gScal;
float gRingR;
float gTube;
vec3 gEye;

/** Set by map(): material, nearest ring distance, and the spire cell's shape for shading. */
float gMat;
float gRingD;
vec3 gSQ;
float gSH;
float gSHue;

/** x offset of the nave at world z: Warp's bend, period 64. */
float bendAt(float z) {
    return gBend * sin(z * (TF_TAU / 64.0));
}

/** Distance into the lace bars of one tessellation of size s (negative inside a bar). */
float laceBars(vec2 xz, float s, float w) {
    float d1 = abs(fract(xz.x / s) - 0.5) * s;
    float d2 = abs(fract((0.5 * xz.x + TF_SLANT * xz.y) / s) - 0.5) * (s / TF_SLANT_LEN);
    float d3 = abs(fract((-0.5 * xz.x + TF_SLANT * xz.y) / s) - 0.5) * (s / TF_SLANT_LEN);
    return min(d1, min(d2, d3)) - w;
}

vec3 spireHash(uint h) {
    return vec3(uhashUnit(h), uhashUnit(uhash(h)), uhashUnit(uhash(h ^ 0x9e37u)));
}

/** One spire per 4-unit cell each side; b is the bent point. */
float spireDist(vec3 b) {
    uint side = b.x < 0.0 ? 1u : 0u;
    float ax = abs(b.x);
    float cell = floor(b.z * 0.25);
    float cz = cell * 4.0 + 2.0;
    uint ci = uint(int(mod(cell, 16.0)));
    vec3 h = mix(spireHash(uhash3(uvec3(ci, side, gSeed0))),
                 spireHash(uhash3(uvec3(ci, side, gSeed1))), gSw);
    float xc = TF_WIDTH + 2.6 + 1.4 * h.x;
    float zc = cz + (h.z - 0.5) * 0.5;
    // Lower under the arches between bays, tallest under each crown.
    float arch = 0.5 + 0.5 * cos(cz * (TF_TAU / TF_BAY));
    float H = (4.0 + 6.0 * h.y) * (1.0 - 0.4 * arch);
    float R = 0.8 + 0.5 * h.x;
    vec3 q = vec3(ax - xc, b.y - TF_GROUND, b.z - zc);
    q.x -= gLean * sin(uTime * 0.4 + h.y * TF_TAU) * q.y;
    float y = clamp(q.y, 0.0, H);
    // Taper to a point, scalloped into tiers at a fixed 0.9 per unit of height
    // so a re-seed that changes H never jumps the tiers.
    float rad = R * (1.0 - y / H) * (1.0 - gScal * (0.5 + 0.5 * cos(TF_TAU * 0.9 * q.y)));
    float d = max((length(q.xz) - rad) / TF_SPIRE_LIP, max(-q.y, q.y - H));
    d = min(d, 2.15 - abs(b.z - cz));
    d = min(d, ax + 3.1);
    gSQ = q;
    gSH = H;
    gSHue = h.y;
    return d;
}

float ftField(vec3 p) {
    vec3 b = p;
    b.x -= bendAt(p.z);

    // The causeway's treads.
    float d = sdBox(vec3(b.x, b.y - (TF_FLOOR - 0.3), fract(b.z) - 0.5), vec3(TF_WIDTH, 0.3, 0.44));
    gMat = 0.0;
    float dg = p.y - TF_GROUND;
    if (dg < d) { d = dg; gMat = 1.0; }

    float ds = spireDist(b);
    if (ds < d) { d = ds; gMat = 2.0; }

    // The vault: lace shell around the union of the two nearest bay spheres.
    float z0 = floor(b.z / TF_BAY) * TF_BAY + TF_BAY * 0.5;
    float z1 = z0 + (b.z > z0 ? TF_BAY : -TF_BAY);
    float dc = max(gRD - length(b - vec3(0.0, TF_GROUND, z0)), gRD - length(b - vec3(0.0, TF_GROUND, z1)));
    float shell = abs(dc - TF_SHELL) - TF_SHELL;
    float lace = laceBars(b.xz, gS1, gW1);
    if (gSw < 0.999) lace = mix(laceBars(b.xz, gS0, gW0), lace, gSw);
    float dd = max(shell, lace);
    if (dd < d) { d = dd; gMat = 3.0; }

    // The portal: three staggered rings under the crown.
    float rl = length(vec2(b.x, b.y - TF_RING_Y));
    float zr = b.z - z0;
    float dr = 1e9;
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        vec2 tq = vec2(rl - gRingR * (1.0 + 0.25 * fi), zr - (fi - 1.0) * 0.9);
        dr = min(dr, length(tq) - gTube);
    }
    gRingD = dr;
    if (dr < d) { d = dr; gMat = 4.0; }

    return max(d, TF_BUBBLE - length(p - gEye));
}

/** ftField through lib_shell's hooks: exactly ftField(p) at rest, and ftField read once, in a loop the compiler cannot unroll. */
float ftFieldGrown(vec3 p) {
    shellStart3(p, SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK);
    float r = 0.0;
    for (int i = SHELL_FROM; i < 7; i++) {
        if (shellPass3(i)) r = shellKeep3(i, ftField(shellAt3(i)));
    }
    return r;
}

/**
 * The field every march, normal and occlusion tap reads: ftField, the style's
 * own, through lib_shell's hook (the identity until the shell drives it).
 */
float map(vec3 p) {
    return ftFieldGrown(p);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** A thin bright line where x crosses 0, w wide, widened by the pixel footprint. */
float wire(float x, float w, float px) {
    return 1.0 - smoothstep(w, w + px + 0.01, abs(x));
}

/** The tessellation for section n: s = 4, 8/3 or 2 (56 / s is whole for each). */
float tessFor(float n) {
    uint pick = uhash(uint(int(n)) + 0x51fu) % 3u;
    return pick == 0u ? 4.0 : (pick == 1u ? 8.0 / 3.0 : 2.0);
}

vec3 skyColour(vec3 rd, float hue, float build) {
    float up = clamp(rd.y, -1.0, 1.0);
    vec3 col = mix(pal(hue + 0.60) * 0.07, pal(hue + 0.50) * 0.025, smoothstep(-0.1, 0.8, up));
    // The glow at the end of the way, straight down the travel axis.
    float ahead = max(rd.z, 0.0);
    col += pal(hue + 0.92) * (0.22 * pow(ahead, 14.0) + 0.05 * ahead * ahead) * (0.8 + 1.2 * build);
    vec3 sp = rd * 150.0;
    float s = hashCell(ivec3(floor(sp)));
    float star = (1.0 - smoothstep(0.0, 0.3, length(fract(sp) - 0.5))) * step(0.996, s);
    col += vec3(0.75) * star * smoothstep(0.1, 0.4, up) * (0.6 + 0.4 * sin(uTime * 2.0 + s * 400.0));
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
    float nSwell = clamp(uKnob[0], 0.0, 1.0);
    float nLace = clamp(uKnob[1], 0.0, 1.0);
    float nChase = clamp(uKnob[2], 0.0, 1.0);
    float nSpan = clamp(uKnob[3], 0.0, 1.0);
    float nSky = clamp(uKnob[4], 0.0, 1.0);
    float nDome = clamp(uKnob[5], 0.0, 1.0);
    float nBend = 0.5 + 0.5 * clamp(uWarp, 0.0, 1.0);
    float nSway = 0.5 + 0.5 * clamp(uTurbulence, 0.0, 1.0);
    float nScallop = 0.5 + 0.5 * clamp(uMorph, 0.0, 1.0);
    // The kick's swell, the lace bars and the filigree, the lamps' chase, the
    // portals' span, the sky and halo, the dome, the nave's bend, the spires'
    // sway and their scallops.
    float swellK = shellMap(nSwell, 0.2, 1.0, 1.8);
    float barBase = shellMap(nLace, 0.08, 0.11, 0.14);
    float filigreeK = shellMap(nLace, 0.0, 0.5, 1.0);
    float chase = shellMap(nChase, -2.0, 0.0, 2.0);
    float spanK = shellMap(nSpan, 0.8, 1.0, 1.2);
    float domeR = shellMap(nDome, 17.0, 20.0, 23.0);

    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float bass = clamp(sndShaped(SND_LOW), 0.0, 1.5);
    float treb = clamp(SND_TREBLE_PRESENCE, 0.0, 1.5);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0) * swellK;
    float trend = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float dropBump = 4.0 * drop * (1.0 - drop);
    float build = nSky;

    // Section crossfade: the old look the frame the count steps, the new one
    // as the section envelope falls.
    float sw = 1.0 - clamp(SND_HELD_HARD_CUT, 0.0, 1.0);
    gSw = sw * sw * (3.0 - 2.0 * sw);
    float secNew = mod(SND_CUT_COUNT, 4096.0);
    float secOld = mod(SND_CUT_COUNT - 1.0, 4096.0);
    gSeed1 = uint(int(secNew));
    gSeed0 = uint(int(secOld));
    float bar = clamp(barBase - 0.03 * swell, 0.05, 0.2);
    gS0 = tessFor(secOld);
    gS1 = tessFor(secNew);
    gW0 = gS0 * bar;
    gW1 = gS1 * bar;

    gBend = shellMap(nBend, 0.0, 0.0, 1.2);
    gLean = shellMap(nSway, 0.0, 0.0, 0.03);
    gScal = shellMap(nScallop, 0.12, 0.12, 0.36);
    gRD = domeR + 3.0 * swell;
    gRingR = 3.8 * (1.0 + 0.05 * clamp(kick, 0.0, 1.6)) * spanK;
    gTube = 0.13;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 64 (see the header), so this
    // rebase is exact and invisible, and z keeps its precision far down the
    // 8192 travel.
    ro.z = mod(ro.z, TF_ZWRAP);
    gEye = ro;
    // The grow hooks (lib_shell) open round this eye, in the rebased frame the world is read in.
    shellSetup3(ro, 3.0, 0.5 * TF_BAY, false);

    float t = 0.05;
    float hitT = -1.0;
    float used = 0.0;
    float halo = 0.0;
    for (int i = 0; i < TF_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (t > TF_FAR) break;
        used = float(i);
        float d = map(ro + rd * t);
        float eps = 0.0008 * t + 0.0008;
        if (d < eps) {
            hitT = t;
            break;
        }
        float stepLen = max(d * 0.8, eps);
        // The light the portal throws into the air, weighted by the step so
        // it does not depend on Detail.
        halo += exp(-gRingD * 2.2) * min(stepLen, 1.0);
        t += stepLen;
    }

    float hue = shellMap(nSpan, -0.15, 0.0, 0.15) + 0.5 * dropBump + 0.02 * sin(uTime * 0.03);
    vec3 sky = skyColour(rd, hue, build);
    vec3 col = sky;
    float fogK = 0.045 * (1.0 - 0.35 * clamp(swell, 0.0, 1.0));
    float flow = uFlowTime + chase;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.0012 * hitT));
        // Restore the hit's globals after the normal taps.
        map(p);
        float mat = gMat;
        vec3 b = vec3(p.x - bendAt(p.z), p.y, p.z);
        float z0 = floor(b.z / TF_BAY) * TF_BAY + TF_BAY * 0.5;
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 0.9, 0.3, 1.0);
        float px = hitT * 2.0 / (max(uCamFocal, 0.1) * uResolution.y);
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);

        // The nearest portal lights the hall; a dim cool fill from above.
        vec3 lp = vec3(bendAt(z0), TF_RING_Y, z0);
        vec3 lv = lp - p;
        float ld = length(lv);
        lv /= max(ld, 1e-3);
        float lamp = (0.55 + 0.55 * bass) * 9.0 / (9.0 + ld * ld * 0.12);
        vec3 lampCol = pal(hue + 0.92);
        float dif = max(dot(n, lv), 0.0);
        float spec = pow(max(dot(reflect(rd, n), lv), 0.0), 32.0);
        float fill = 0.5 + 0.5 * n.y;
        vec3 light = lampCol * (dif * lamp) + pal(hue + 0.55) * 0.12 * fill;

        if (mat < 0.5) {
            // Tread: stone, a lit nosing on each step's front, the lit edges.
            vec3 stone = mix(pal(hue + 0.60), pal(hue + 0.46), clamp(hitT * 0.02, 0.0, 1.0));
            col = stone * (0.05 + 0.6 * light) * ao + lampCol * spec * 0.25 * lamp;
            float top = smoothstep(0.6, 0.9, n.y);
            float nose = wire(fract(b.z) - 0.08, 0.03, px) * top;
            col += stone * nose * (0.35 + 0.4 * lamp);
            float edge = wire(abs(b.x) - TF_WIDTH + 0.06, 0.04, px);
            col += pal(hue + 0.42) * edge * (0.4 + 0.5 * treb);
        } else if (mat < 1.5) {
            // Ground: dark water, mirroring the sky and the portal glow at grazing angles.
            col = pal(hue + 0.55) * 0.02 * ao + skyColour(reflect(rd, n), hue, build) * (0.15 + 0.6 * fres)
                + lampCol * spec * 0.4 * lamp;
        } else if (mat < 2.5) {
            // Spire: dark body, filigree courses and ribs, a star at the tip.
            vec3 tint = mix(pal(hue + 0.94), pal(hue + 0.86), gSHue);
            float course = wire(fract(gSQ.y * 0.9) - 0.5, 0.06, px * 0.9);
            float ang = atan(gSQ.z, gSQ.x);
            float rib = wire(fract(ang * (12.0 / TF_TAU)) - 0.5, 0.07, px * 3.0);
            float filigree = max(course, rib) * filigreeK;
            col = tint * (0.05 + 0.5 * light) * ao
                + tint * filigree * (0.18 + 0.85 * treb) * ao
                + mix(vec3(1.0), tint, 0.4) * spec * 0.3 * lamp;
            float tip = smoothstep(gSH - 1.1, gSH, gSQ.y);
            col += mix(vec3(1.0), tint, 0.3) * tip * tip * (0.5 + 1.4 * clamp(kick, 0.0, 1.6));
        } else if (mat < 3.5) {
            // Dome: lace bars lit by the portal, a sparse set of lit facets
            // re-chosen each section. The facet id is wrapped by 56 / s, the
            // lattice's shift under the rebase, so it never re-hashes there.
            float h = clamp((b.y - TF_GROUND) / gRD, 0.0, 1.0);
            vec3 domeCol = mix(pal(hue + 0.58), pal(hue + 0.50), h);
            col = domeCol * (0.06 + 0.7 * light) * ao + domeCol * fres * 0.15;
            float s = gSw > 0.5 ? gS1 : gS0;
            uint seed = gSw > 0.5 ? gSeed1 : gSeed0;
            float wrapN = floor(56.0 / s + 0.5);
            float u2 = floor((0.5 * b.x + TF_SLANT * b.z) / s + 0.5);
            float u3 = floor((-0.5 * b.x + TF_SLANT * b.z) / s + 0.5);
            ivec3 cellId = ivec3(int(mod(u2, wrapN)), int(mod(u3, wrapN)), int(seed));
            float on = step(0.86, hashCell(cellId));
            vec3 facet = mix(pal(hue + 0.44), pal(hue + 0.90), hashCell(cellId + ivec3(7, 3, 0)));
            col += facet * on * (0.3 + 0.8 * treb) * (0.6 + 0.4 * ao);
        } else {
            // Ring: a chase of lamps round each hoop.
            float rl = length(vec2(b.x, b.y - TF_RING_Y));
            float which = clamp(floor((rl / gRingR - 1.0) / 0.25 + 0.5), 0.0, 2.0);
            float pa = atan(b.y - TF_RING_Y, b.x);
            float lamps = 22.0 + which * 10.0;
            float bead = 0.5 + 0.5 * cos(pa * lamps + flow * (1.4 + which * 0.4));
            vec3 ringCol = mix(pal(hue + 0.92), pal(hue + 0.04), which / 3.0);
            col = ringCol * (0.3 + 0.9 * bead * bead) * (0.55 + 0.55 * bass) * 1.4
                + vec3(1.0) * spec * 0.2;
        }

        col = mix(col, sky, 1.0 - exp(-hitT * fogK));
    }

    // The portal's halo in the air, brightest on the bass and with BUILD.
    col += pal(hue + 0.92) * halo * 0.05 * (0.35 + 0.9 * bass) * (0.6 + 0.8 * build);

    if (!touchIdle()) {
        col += pal(0.5 + 0.15 * sin(uTime * 0.05)) * min(touchWake(uv), 3.0) * 0.05;
    }
    col *= 0.8 + 0.2 * trend + 0.1 * swell;
    col *= 0.7 + 0.3 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
