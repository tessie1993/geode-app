#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Morph, Warp and Turbulence are spent on the scaffold itself, not on the rays.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3

// Scaffold Drift: a flight through an endless scaffold. Space is cut into
// cubic cells; in every cell three square beams run along x, y and z, so the
// beams of neighbouring cells meet at nodes and the whole of space is one
// open frame. Each beam swells toward its nodes and thins at mid-span, like a
// bone, and a finer grid of round bores (along x and along y) perforates the
// steel near the eye, so up close the beams read as grating and far away as
// solid girders fading into fog.
//
// Provenance: written for Geode from a functional description of a generic
// technique (a domain-repeated box and tube lattice raymarched from inside),
// with textbook SDF maths only: domain repetition, a 2D box distance, a
// cylinder distance and min/max booleans. No existing implementation was read.
//
// ---- wrap safety ------------------------------------------------------------
//
// The cell is LS_CELL = 2 world units and the bores repeat every LS_CELL / 8,
// both of which divide LS_ZWRAP = 16. The helical drift below turns once per
// 8 units of z. So rebasing the eye by 16 along z moves the world by whole
// periods of everything in it, and the rig's 8192 travel wrap
// (CameraRig::kTravelWrap, itself a multiple of 16) never shows. The lattice is
// periodic on x and y too, so the orbit and hover paths, which leave the z
// axis, see the same scaffold in every direction.
//
// ---- the shared controls ----------------------------------------------------
//
// Morph (0..1)        beam thickness and how hard the beams swell toward the nodes.
// Warp (0..1)         the helical drift: each z slice of the lattice is offset
//                     round a small circle, so the z beams become a slow helix
//                     and the whole scaffold twists along the flight.
// Turbulence (0..1.5) the bores' radius: how deep the grating cuts.
// Detail              the march budget (uSteps), under the constant cap.
// Morph and Warp never sit still (MotionField breathes them), so both only
// move a width or an amplitude, never a count or a period.
//
// ---- the six knobs (offset from 0.5; nothing changes at rest) --------------
//
// k0 PULSE    how far the kick swells the beams.
// k1 TEXTURE  the bores' radius on top of Turbulence.
// k2 FLOW     the helical drift's amplitude on top of Warp.
// k3 TONE     the palette phase.
// k4 BUILD    the fog's density and the bores' glow.
// k5 SPACE    the beams' thickness (wider or airier cells).
//
// ---- audio: one sound, one property -----------------------------------------
//
// Kick (SND_KICK_ENV)          the beams swell, and the key light pulses with them.
// Onset (SND_ONSET_ENV)        the bores' rims glow in a contrasting hue; an
//                              envelope, so it decays and never strobes.
// Key (uKeyHue, uKeyStrength)  and the chord hue slide the palette phase.
// Build-up (SND_BUILD_UP)      the fog tightens, closing the far lattice in.
// Hard cut (SND_CUT_COUNT)     the key light moves to a new direction,
//                              crossfaded over SND_HELD_HARD_CUT.
// Drop (SND_HELD_SURGE)        the light swings head-on and the fog opens, on a
//                              hump, so it glides there and back.
// Bass (SND_BASS_ATT)          the far haze brightens a little.
// SND_INTENSITY                overall level, as everywhere.

#define LS_MAX_STEPS 96
#define LS_CELL 2.0
#define LS_HALF 1.0
#define LS_BORE (LS_CELL / 8.0)
#define LS_ZWRAP 16.0
/** One helical turn per 8 units of z: 2 * pi / 8. */
#define LS_HELIX_W 0.7853982
#define LS_FAR 28.0
#define LS_BUBBLE 0.45
/** Bores are cut only this close to the eye; past it they would alias. */
#define LS_BORE_NEAR 3.5
#define LS_BORE_FAR 9.0

float gW;      // beam half-width at mid-span
float gSwell;  // extra half-width at the nodes, as a fraction of gW
float gBore;   // bore radius
float gHelix;  // helical drift amplitude
vec3 gEye;
/** Set by map(): the lattice-space point (after the drift), for shading. */
vec3 gL;

/** Lattice space: the world with each z slice offset round a circle. */
vec3 latticeSpace(vec3 p) {
    float a = p.z * LS_HELIX_W;
    return vec3(p.xy + gHelix * vec2(cos(a), sin(a)), p.z);
}

/** A square beam's distance in its cross-section plane `c`; `s` runs along it. */
float beam(vec2 c, float s) {
    // Thick at the nodes (s = +-LS_HALF), thin at mid-span (s = 0).
    float w = gW * (1.0 + gSwell * (0.5 - 0.5 * cos(6.2831853 * s / LS_CELL)));
    vec2 d = abs(c) - vec2(w);
    // A small rounding keeps the edges from aliasing into a single pixel line.
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - 0.015;
}

/** The three beams of the cell that holds lattice point `l`. */
float frame(vec3 l) {
    vec3 q = mod(l, LS_CELL) - LS_HALF;
    float bx = beam(q.yz, q.x);
    float by = beam(q.xz, q.y);
    float bz = beam(q.xy, q.z);
    return min(bx, min(by, bz));
}

/** The bores: round tubes along x and along y on a grid LS_CELL / 8 apart. */
float bores(vec3 l) {
    vec3 f = mod(l, LS_BORE) - 0.5 * LS_BORE;
    float tx = length(f.yz);
    float ty = length(f.xz);
    return min(tx, ty) - gBore;
}

float map(vec3 w) {
    vec3 l = latticeSpace(w);
    gL = l;
    float d = frame(l);
    // Perforate near the eye only, fading the bore radius to nothing, so the
    // far lattice is solid steel rather than a moire of sub-pixel holes.
    float near = 1.0 - smoothstep(LS_BORE_NEAR, LS_BORE_FAR, length(w - gEye));
    if (near > 0.0 && gBore > 0.0) {
        float b = bores(l) + gBore * (1.0 - near);
        d = max(d, -b);
    }
    // A bubble round the eye, so a beam the path crosses opens rather than
    // filling the screen.
    return smax(d, LS_BUBBLE - length(w - gEye), 0.25);
}

vec3 normalAt(vec3 p, float e) {
    vec3 n = vec3(map(p + vec3(e, 0.0, 0.0)) - map(p - vec3(e, 0.0, 0.0)),
                  map(p + vec3(0.0, e, 0.0)) - map(p - vec3(0.0, e, 0.0)),
                  map(p + vec3(0.0, 0.0, e)) - map(p - vec3(0.0, 0.0, e)));
    return normalize(n + 1e-6);
}

/** The key light for cut `n`: an upper-hemisphere direction from a hash. */
vec3 lightFor(float n) {
    uint h = uhash(uint(n) + 0x51au);
    float az = 6.2831853 * uhashUnit(h);
    float el = 0.35 + 0.5 * uhashUnit(uhash(h));
    return normalize(vec3(cos(az) * cos(el), sin(el), sin(az) * cos(el)));
}

float knob(int i) { return uKnob[i] - 0.5; }

void main() {
    vec2 uv = view();

    float form = clamp(uMorph, 0.0, 1.0);
    float flow = clamp(uWarp, 0.0, 1.0);
    float chaos = clamp(uTurbulence / 1.5, 0.0, 1.0);
    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float onset = clamp(SND_ONSET_ENV, 0.0, 1.0);
    float build = clamp(SND_BUILD_UP, 0.0, 1.0);
    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float dropBump = 4.0 * drop * (1.0 - drop);
    float bass = clamp(sndLevel(SND_BASS_ATT), 0.0, 1.5);

    gW = clamp((0.11 + 0.09 * form) * (1.0 + 0.6 * knob(5)) + (0.035 + 0.05 * knob(0)) * kick, 0.05, 0.36);
    gSwell = 0.2 + 0.5 * form;
    gBore = clamp(0.012 + 0.045 * chaos + 0.04 * knob(1), 0.0, 0.085);
    gHelix = clamp(0.06 + 0.32 * flow + 0.25 * knob(2), 0.0, 0.45);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Everything tiles with periods dividing 16 along z, so this is exact.
    ro.z = mod(ro.z, LS_ZWRAP);
    gEye = ro;

    // The drift can shear the field by up to gHelix * LS_HELIX_W per unit,
    // which bounds the gradient by sqrt(1 + that^2); the step is shortened by
    // it on top of the plain under-relaxation.
    float relax = 0.82 / sqrt(1.0 + gHelix * gHelix * LS_HELIX_W * LS_HELIX_W);
    float t = 0.02;
    float hitT = -1.0;
    float used = 0.0;
    for (int i = 0; i < LS_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (t > LS_FAR) break;
        used = float(i);
        float d = map(ro + rd * t);
        float eps = 0.0007 * t + 0.0004;
        if (d < eps) {
            hitT = t;
            break;
        }
        t += max(d * relax, eps);
    }

    // Palette phase: a slow drift, the key (gated by its confidence), the
    // chord, and the TONE knob.
    float hue = 0.58 + 0.02 * sin(uTime * 0.013) + 0.35 * uKeyHue * uKeyStrength
              + 0.08 * clamp(SND_CHORD_HUE, 0.0, 1.0) + 0.4 * knob(3);
    float fogK = clamp((0.06 + 0.09 * build + 0.06 * knob(4)) * (1.0 - 0.45 * dropBump), 0.02, 0.2);
    vec3 fogCol = pal(hue + 0.5) * (0.03 + 0.05 * bass) + vec3(0.004, 0.004, 0.008);
    vec3 col = fogCol;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        map(p);
        vec3 l = gL;
        vec3 n = normalAt(p, max(0.0012, 0.0009 * hitT));

        // The key light: a direction per hard cut, crossfaded, swung head-on
        // (toward the viewer's back) by a drop.
        float sw = 1.0 - clamp(SND_HELD_HARD_CUT, 0.0, 1.0);
        sw = sw * sw * (3.0 - 2.0 * sw);
        vec3 key = normalize(mix(lightFor(mod(SND_CUT_COUNT - 1.0, 4096.0)), lightFor(SND_CUT_COUNT), sw));
        key = normalize(mix(key, -rd + vec3(0.0, 0.3, 0.0), 0.75 * dropBump));

        float diff = max(dot(n, key), 0.0);
        float back = max(dot(n, -key), 0.0);
        vec3 h = normalize(key - rd);
        float spec = pow(max(dot(n, h), 0.0), 36.0);
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 4.0);
        // Occlusion: how much room the normal has, and the march's own cost.
        float ao = clamp(map(p + n * 0.18) / 0.18, 0.0, 1.0);
        ao = 0.35 + 0.65 * ao * clamp(1.0 - used / max(uSteps, 1.0), 0.3, 1.0);

        // Which beam this is: the axis the cell point is farthest along is
        // the one it runs on, and each axis takes its own palette step.
        vec3 q = abs(mod(l, LS_CELL) - LS_HALF);
        float axis = q.x > q.y && q.x > q.z ? 0.0 : (q.y > q.z ? 1.0 : 2.0);
        vec3 base = pal(hue + 0.11 * axis);
        float lightGain = 0.75 + 0.45 * kick;

        // A soft headlamp from the eye, so faces turned from the key light
        // still read as steel instead of falling to black.
        float lamp = ndv * exp(-hitT * 0.12);
        vec3 steel = base * (0.05 + 0.9 * diff * lightGain + 0.12 * back + 0.35 * lamp) * ao
                   + pal(hue + 0.3) * spec * 0.6 * lightGain * ao
                   + pal(hue + 0.5) * fres * 0.25;

        // Bore rims: where the surface sits on a bore wall, it glows on an
        // onset and, a little, with the BUILD knob.
        float near = 1.0 - smoothstep(LS_BORE_NEAR, LS_BORE_FAR, hitT);
        float rim = 1.0 - smoothstep(0.0, 0.02 + 0.004 * hitT, abs(bores(l)));
        float inBore = step(bores(l), 0.003) * near;
        vec3 rimCol = pal(hue + 0.5) * 1.3;
        steel += rimCol * (rim * near + 0.4 * inBore) * (0.06 + 0.9 * onset + 0.15 * max(knob(4), 0.0));

        col = mix(steel, fogCol, 1.0 - exp(-hitT * fogK));
    }

    // A faint haze toward the light, so open space reads as air, not void.
    col += pal(hue + 0.45) * 0.03 * pow(max(dot(rd, normalize(vec3(0.2, 0.6, 0.8))), 0.0), 6.0);

    col *= 0.85 + 0.2 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
