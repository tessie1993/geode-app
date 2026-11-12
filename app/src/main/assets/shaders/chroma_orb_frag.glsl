#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Morph, Warp and Turbulence are spent on the orbs themselves, not on the rays.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_touch

// Chroma Orb: a flight through a string of glass orbs threaded on the travel
// axis. Each orb is skinned with the old four-fold kaleidoscope of neon dot
// lattices and has the old ringed tunnel bored straight through it, so the
// camera flies into the dark diamond at its centre, down a bore lined with
// rings of dots, out the far side, across a gap of soft green bokeh and into
// the next orb. Every dot is still drawn three times at slightly different
// scales, so it carries a green core with blue and orange fringes.
//
// The skin is the old picture put back on the sphere: the surface direction
// is projected stereographically about the bore axis (conformal, so the dots
// stay round), folded into the kaleidoscope's arms with their dark seams, and
// read by the same three woven lattices. Looking down the axis at an orb you
// see the 2D style; from any other angle you see that it was a sphere.
//
// The camera is the shared rig (camFly). Orbs sit at z = 16 + 32k, so the
// rig's orbit path, which circles the origin inside a radius of 5.4, circles
// in the gap between two orbs instead of inside one. Every z term has a period
// dividing 256 (orb spacing 32, eight orbs per hue cycle, bokeh slices every
// 2), and 256 divides 8192, so rebasing the eye by 256 is exact and the rig's
// travel wrap is invisible. The twist and the bore's ring drift are measured
// from each orb's own centre, so they never see the wrap at all.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (SND_BASS_ATT)        breathes the lattice: every dot swells a little.
// Kick (SND_KICK_ENV)        the orb just ahead of the eye swells; geometry,
//                            not brightness. PULSE scales it.
// Snare (SND_SNARE_ENV)      widens the chromatic fringes, as touch does.
// Pitch (SND_HELD_NOTE_CHANGE) slides the palette, as a hump, so it glides.
// Band presences             light the bokeh between the orbs, as before.
// SND_ENERGY_TREND           overall level.
// uFlowPhase                 drifts the bore rings: loudness changes the RATE.
//                            The kaleidoscope turns on time alone (owner
//                            2026-11-12, no style spins to the music).
//
// Morph turns each sphere into a cut gem (an octahedron, its vertices on the
// bore axis so the diamond hole still lines up). Warp twists the bore and the
// skin about the axis, more toward the ends. Turbulence ripples the glass.
// Intensity drives the dots' emission. Knobs: PULSE kick swell, TEXTURE
// lattice density, TONE the orbs' figure and a small hue
// offset (a high melody draws each orb out along the flight into a long egg,
// a low one flattens it into a wide lens; the scale only ever shrinks a
// coordinate, so the field stays a lower bound, and the longest orb still
// ends before the gap), BUILD fringe width, SPACE fog. FLOW no longer twists
// the bore (a knob is the notes', and no sound turns a style).
//
// ---- why the estimate is safe ----------------------------------------------
//
// The solid is max(orb, -bore). The orb is a mix of two exact distances
// (sphere, octahedron), so it is 1-Lipschitz; the glass ripple adds at most
// A * f * sqrt(3) to the gradient and the orb term is divided by 1 plus that.
// The bore is a regular polygon read from the inside (apothem minus the
// largest face projection: exact inside, a lower bound outside). Its twist
// rotates by k * dz, which stretches the gradient by sqrt(1 + (k r)^2); the
// bore term is divided by that at the largest radius an orb reaches. The orbs
// are repeated every 32 with radius under 11, so the nearest cell is the only
// one that matters and the field is continuous across cell walls (the orb
// term dominates there). A sphere around the eye is carved out of everything,
// so a rig path that brushes the bore wall never puts the eye inside glass.

#define CO_MAX_STEPS 110
#define CO_TAU 6.2831853
#define CO_FAR 90.0
/** Orb spacing along z, and the offset that keeps the origin in a gap. */
#define CO_SPACING 32.0
#define CO_OFFSET 16.0
/** z period of everything: 8 orbs; divides 8192. */
#define CO_ZWRAP 256.0
#define CO_RADIUS 8.5
/** Bore apothem: the rig's glide wanders up to ~3 off the axis. */
#define CO_BORE 3.5
#define CO_KICK 0.9
#define CO_BUBBLE 0.8
/** Largest radius the bore's twist acts on (gem vertex plus kick). */
#define CO_RMAX 11.0

float gFolds;
float gSpin;
float gTwist;
float gTwistLip;
float gRipple;
float gRippleLip;
float gGem;
float gKick;
vec3 gEye;
/** Set by map(): the hit orb's centre z, its index (0..7), and which surface. */
float gCz;
float gIdx;
float gMat;

float knob(int i) { return clamp(uKnob[i], 0.0, 1.0) - 0.5; }

vec2 rot(vec2 p, float a) { return mat2(cos(a), -sin(a), sin(a), cos(a)) * p; }

vec2 fold(vec2 p, float n) {
    float w = CO_TAU / n;
    float a = mod(atan(p.y, p.x), w);
    a = abs(a - 0.5 * w);
    return length(p) * vec2(cos(a), sin(a));
}

/** The user's fold count when the kaleidoscope is on; four arms otherwise, which is the diamond. */
float foldCount() { return (uKaleido > 0.5 && uSymmetry >= 2.0) ? floor(uSymmetry) : 4.0; }

/** The bore's cross-section in its own turned frame: the largest face projection of a regular n-gon. */
float polyR(vec2 q, float n) {
    // At least a triangle: two folds would make the bore a slot through the orb.
    float w = CO_TAU / max(n, 3.0);
    float a = mod(atan(q.y, q.x), w) - 0.5 * w;
    return length(q) * cos(a);
}

/** k3 TONE: the orb's per-axis coordinate scale (1 at rest; never above 1). */
vec3 gShape = vec3(1.0);

float map(vec3 p) {
    float idx = floor((p.z - CO_OFFSET) / CO_SPACING + 0.5);
    float cz = idx * CO_SPACING + CO_OFFSET;
    vec3 l = vec3(p.xy, p.z - cz);

    // Only the orb just ahead of the eye answers the kick.
    float kick = gKick * exp(-abs(cz - gEye.z - 10.0) * 0.07);
    float r = CO_RADIUS + kick;
    vec3 ls = l * gShape;
    float sph = length(ls) - r;
    float gem = sdOctahedron(ls, r * 1.32);
    float orb = mix(sph, gem, gGem);
    vec3 rp = l * 1.2;
    orb += gRipple * sin(rp.x + uTime * 0.4) * sin(rp.y - uTime * 0.3) * sin(rp.z + uTime * 0.2);
    orb /= gRippleLip;

    vec2 q = rot(l.xy, gSpin + gTwist * l.z);
    float bore = (CO_BORE - polyR(q, gFolds)) / gTwistLip;

    float d = max(orb, bore);
    gMat = orb > bore ? 0.0 : 1.0;
    gCz = cz;
    gIdx = mod(idx, CO_ZWRAP / CO_SPACING);
    return max(d, CO_BUBBLE - length(p - gEye));
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

float disc(vec2 g, float radius, float aa) { return smoothstep(radius, radius - 0.06 - aa, length(g)); }

// Three square lattices of discs, the second offset half a cell and the third
// turned 45 degrees, so the rows read as woven rather than as one grid.
float lattice(vec2 p, float pulse, float aa) {
    float a = disc(fract(p * 3.4) - 0.5, 0.30 + 0.05 * pulse, aa);
    float b = disc(fract(p * 6.2 + 0.5) - 0.5, 0.25 + 0.04 * pulse, aa * 1.8);
    float c = disc(fract(rot(p, 0.7854) * 11.0) - 0.5, 0.17, aa * 3.2);
    return a + 0.75 * b + 0.3 * c;
}

/** The outer skin at stereographic point `q`: folded lattice with the fold seams left dark. */
float skin(vec2 q, float density, float pulse, float aa) {
    vec2 s = fold(q, gFolds);
    float wedge = 0.5 * CO_TAU / gFolds;
    float a = atan(s.y, s.x);
    float seam = smoothstep(0.0, 0.05, a) * smoothstep(0.0, 0.05, wedge - a);
    return lattice(s * density, pulse, aa) * (0.3 + 0.7 * seam);
}

/** The bore wall: rings of dots in (angle, depth), alternate rings staggered half a dot like a honeycomb. */
float boreRings(float ang, float dz, float pulse, float aa) {
    float ring = dz * 1.05 - uFlowPhase * 1.5;
    float stagger = 0.5 * mod(floor(ring), 2.0);
    // 24 around: the angle's wrap lands on a whole cell.
    vec2 cell = vec2(ang / CO_TAU * 24.0 + stagger, ring);
    return disc(fract(cell) - 0.5, 0.33 + 0.05 * pulse, aa);
}

/** Lens fringe order: blue is the biggest copy, orange the smallest. */
vec3 fringe(vec3 rgb, float hue) {
    vec3 green = mix(pal(hue + 0.62), pal(hue + 0.55), 0.3);
    vec3 blue = pal(hue + 0.33);
    vec3 orange = pal(hue + 0.94);
    // Green is the core of every dot; blue and orange are strongest where the
    // green copy has ended, with some of each under the core so the overlaps
    // go cyan and yellow as three offset copies really do.
    float core = clamp(rgb.g, 0.0, 1.0);
    float under = 1.0 - 0.6 * core;
    return min(green * core + blue * rgb.b * under * 0.9 + orange * rgb.r * under * 0.85, vec3(1.15));
}

void main() {
    vec2 uv = view();

    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float bass = sndLevel(SND_BASS_ATT);
    float snare = clamp(SND_SNARE_ENV, 0.0, 1.0);
    float pitch = clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0);
    float pitchBump = 4.0 * pitch * (1.0 - pitch);
    float presence = (SND_BASS_PRESENCE + SND_MID_PRESENCE + SND_TREBLE_PRESENCE) / 3.0;
    float finger = touchFalloff(uv, 0.45);
    float glow = 0.5 + 0.5 * clamp(uIntensity, 0.0, 2.0);
    float hue = 0.2 * pitchBump + 0.25 * knob(3);

    gFolds = foldCount();
    // Time alone, at the rate the loudness clock (uFlowPhase) used to add at a typical level.
    gSpin = uTime * 0.195;
    gTwist = 0.06 * clamp(uWarp, 0.0, 1.0);
    gShape = vec3(vec2(1.0 - 0.58 * max(-knob(3), 0.0)), 1.0 - 0.58 * max(knob(3), 0.0));
    // A lens (TONE low) reaches further out in xy, where the bore's twist moves faster.
    float rMax = CO_RMAX / gShape.x;
    gTwistLip = sqrt(1.0 + gTwist * gTwist * rMax * rMax);
    gRipple = 0.3 * clamp(uTurbulence / 1.5, 0.0, 1.0);
    gRippleLip = 1.0 + gRipple * 1.2 * 1.7320508;
    gGem = clamp(uMorph, 0.0, 1.0);
    gKick = CO_KICK * clamp(SND_KICK_ENV, 0.0, 1.0) * (1.0 + 1.6 * knob(0));
    float density = 4.0 * (1.0 + 0.6 * knob(1));
    float split = 0.035 + 0.03 * finger + 0.03 * snare + 0.04 * knob(4);
    float fogK = 0.03 * (1.0 - 1.4 * knob(5));

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 256, so this rebase is exact
    // and keeps highp z precise far down the 8192 travel.
    ro.z = mod(ro.z, CO_ZWRAP);
    gEye = ro;

    float t = 0.03;
    float hitT = -1.0;
    float used = 0.0;
    float nearest = 1e3;
    for (int i = 0; i < CO_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (t > CO_FAR) break;
        used = float(i);
        float d = map(ro + rd * t);
        nearest = min(nearest, d);
        float eps = 0.0007 * t + 0.0005;
        if (d < eps) {
            hitT = t;
            break;
        }
        t += max(d * 0.9, eps);
    }

    vec3 fogCol = pal(hue + 0.66) * 0.09;
    vec3 col = fogCol;
    float sceneT = hitT > 0.0 ? hitT : CO_FAR;
    // The halo each orb sits in, for rays that pass close to one.
    if (hitT < 0.0) col += pal(hue + 0.62) * 0.12 * exp(-max(nearest, 0.0) * 0.9);

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.001 * hitT));
        // Restore the hit's globals after the normal taps.
        map(p);
        vec3 l = vec3(p.xy, p.z - gCz);
        float orbHue = hue + 0.04 * (gIdx - 3.5) / 3.5;
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.1, 0.3, 1.0);
        vec3 L = normalize(vec3(-0.45, 0.6, -0.66));
        float spec = pow(max(dot(reflect(rd, n), L), 0.0), 28.0);
        // Dot edges widen with distance so far lattices fade instead of shimmering.
        float aa = clamp(hitT * 0.006, 0.0, 0.25);
        vec2 q = rot(l.xy, gSpin + gTwist * l.z);

        vec3 rgb;
        if (gMat < 0.5) {
            // Outer skin: the old 2D picture, stereographically wrapped about the axis.
            vec3 dir = normalize(vec3(q, l.z));
            vec2 st = dir.xy / (1.0 + abs(dir.z));
            rgb = vec3(skin(st * (1.0 + split), density, bass, aa),
                       skin(st, density, bass, aa),
                       skin(st * (1.0 - split), density, bass, aa));
            // A dark ring marks the bore's lip, as a bored hole would.
            float lip = 1.0 - 0.6 * smoothstep(0.6, 0.0, polyR(q, gFolds) - CO_BORE);
            vec3 dots = fringe(rgb, orbHue) * lip * glow;
            float limb = 0.5 + 0.5 * ndv;
            col = pal(orbHue + 0.62) * 0.04 + dots * limb * (0.55 + 0.45 * ao);
            col += vec3(0.25, 0.45, 0.35) * spec * 0.3;
            col += pal(orbHue + 0.55) * fres * 0.3;
        } else {
            // Bore wall: the ringed tunnel, dimmer toward the orb's middle as
            // if lit from the open ends.
            float ang = atan(q.y, q.x);
            rgb = vec3(boreRings(ang * (1.0 + split), l.z * (1.0 + split), bass, aa),
                       boreRings(ang, l.z, bass, aa),
                       boreRings(ang * (1.0 - split), l.z * (1.0 - split), bass, aa));
            float open = 0.35 + 0.65 * smoothstep(0.0, CO_RADIUS, abs(l.z));
            col = pal(orbHue + 0.62) * 0.03 + fringe(rgb, orbHue) * open * glow * (0.5 + 0.5 * ao);
            col += vec3(0.25, 0.45, 0.35) * spec * 0.15;
        }

        col = mix(col, fogCol, 1.0 - exp(-hitT * fogK));
    }

    // Bokeh: soft discs on world z slices every 2 units, one hashed candidate
    // per 2.5-unit cell, wide and out of focus. Slices sit at fixed world z
    // (index wrapped by 256), so they hold still under the rebase and their
    // parallax is true. Brightness follows the band presences, as before.
    if (abs(rd.z) > 0.08) {
        float dir = rd.z > 0.0 ? 1.0 : -1.0;
        float base = rd.z > 0.0 ? floor(ro.z * 0.5) + 1.0 : ceil(ro.z * 0.5) - 1.0;
        float lift = 0.6 + 0.4 * clamp(presence, 0.0, 1.5);
        for (int i = 0; i < 7; i++) {
            float zi = (base + dir * float(i)) * 2.0;
            float ti = (zi - ro.z) / rd.z;
            if (ti < 0.6 || ti > sceneT) continue;
            vec2 xy = ro.xy + rd.xy * ti;
            vec2 cell = floor(xy / 2.5);
            int slice = int(mod(zi, CO_ZWRAP) * 0.5 + 0.5);
            float h = hashCell(ivec3(int(cell.x), int(cell.y), slice));
            if (h < 0.6) continue;
            float h2 = hashCell(ivec3(int(cell.x), int(cell.y), slice + 131));
            vec2 c = (cell + 0.5 + (vec2(h, h2) - 0.5) * 0.6) * 2.5
                   + 0.2 * vec2(sin(uTime * 0.21 + h * 30.0), cos(uTime * 0.17 + h2 * 30.0));
            float blob = smoothstep(0.45, 0.06, length(xy - c)) * (0.25 + 0.75 * h2);
            col += pal(hue + 0.62) * blob * 0.12 * lift * exp(-ti * fogK);
        }
    }

    col *= 0.85 + 0.2 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
