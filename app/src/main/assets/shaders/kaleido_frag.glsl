#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uSoundTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Morph, Warp and Turbulence are spent on the mirror tube itself (below), not
// on the rays, so view() must not bend the picture with them a second time.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_camera
//#include lib_shell
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3

// Kaleidoscope: a flight down the inside of a real kaleidoscope. The tube is a
// regular N-gon prism of mirror glass around the z axis, and the space inside
// it is folded into N angular wedges, so everything drawn in one wedge is
// mirrored into every other one: the old flat fold, now with depth. Gems (an
// octahedron melting into a cube) hang in each wedge, one per 4-unit cell along
// z, and the flowing sin x cos pattern of the flat style glows on the mirror
// faces, with the spectrum rings now running down the tube's length. The far
// end of the tube is the light, as in the toy.
//
// The camera is the shared rig (camFly): the eye rides the rig's path down +z,
// and the rig's wander off the axis is what shows the folds in perspective.
// The whole tube turns once per 64 world units of z (a twist), and every z
// term below has a period dividing 64 (the gems' cells 4, the wall ripple 8,
// the warp wave and the spectrum rings 16, the sparkle cells 0.5), so the eye
// is rebased by 64, which divides the rig's 8192 travel wrap: the wrap and the
// rebase are both invisible.
//
// ---- audio: what reacts to what ---------------------------------------------
//
// Section (SND_CUT_COUNT)   re-picks the fold count (4 to 9 wedges, hashed),
//                           crossfaded from the old count over the section
//                           envelope (SND_HELD_HARD_CUT), then held: the two
//                           tubes' fields are blended, so nothing pops.
// Kick (SND_KICK_ENV)       the gems swell and glow (k0 PULSE scales it).
// Hats (SND_HAT_ENV)        sparkles: a sparse hashed scatter on the mirror
//                           faces, re-rolled on every onset (SND_ONSET_COUNT),
//                           mirrored into every wedge by the fold.
// Bass (SND_LOW)            lifts the mirror pattern and the light at the end,
//                           as the flat style's bass lifted its pattern.
// Spectrum (sndEq)          rings of the analyser bands flow down the tube.
// Drop (SND_HELD_SURGE)     the tube opens wider and the palette swings to its
//                           far side, on a hump so it glides in and out.
// Pitch (SND_HELD_NOTE_CHANGE) slides the hue, as a hump.
// SND_INTENSITY             overall level.
//
// Knobs (offsets from 0.5): k0 PULSE kick depth, k1 TEXTURE pattern density,
// k2 FLOW pattern speed (a crossfade of two fixed rates, so it never jumps),
// k3 TONE where the gems hang (a high melody draws them in off the mirrors
// toward the flight axis, so the eye threads between them; a low one presses
// them back against the glass; plus a small hue offset), k4 BUILD gem glow,
// k5 SPACE tube width.
// Params: Morph melts the gems from octahedra to cubes, Warp waves the folds'
// angle along the tube, Turbulence ripples the mirror faces, Symmetry and the
// Kaleidoscope switch still fold the screen in view() on top.
// Every one of these, and the gems' size (BUILD), the sparkles' density
// (TEXTURE) and the fog (SPACE), is one normalized input (lib_shell) set in one
// block at the top of main and mapped there to its range; at rest each is the
// constant the tube was tuned with. The tube and its gems are the field the
// grow hooks act on (its shell wrapper): Spawn slides a second set of gems and
// mirrors half a cell down the tube, Fractal grows gems a third the size on
// the glass, the layers open the near and far tube.
//
// ---- why the distance estimate is safe --------------------------------------
//
// The angular fold is a piecewise isometry and the fold count is an integer,
// so the folded field is exact. The tube wall in a folded wedge is a plane,
// exact. The twist and the warp wave rotate xy by an angle that depends on z,
// whose Lipschitz bound is sqrt(1 + (r * dtheta/dz)^2) with r up to the wall;
// that bound (gLip) is computed per frame and divides the field. The wall
// ripple's gradient is bounded by 2.8 x its amplitude and divides the wall
// term. Each cell measures only its own gem, clamped to the cell boundary plus
// the gap no gem can cross. The section crossfade mixes two 1-Lipschitz fields,
// which stays 1-Lipschitz. A sphere around the eye is carved out of
// everything, so the eye is never inside a gem or a wall.

#define KA_MAX_STEPS 96
#define KA_FAR 34.0
#define KA_TAU 6.2831853
/** The eye's z rebase: the tube's twist period, which divides 8192. */
#define KA_ZWRAP 64.0
/** One full turn of the tube per KA_ZWRAP units. */
#define KA_TWIST (6.2831853 / 64.0)
/** Gem cell length along z (divides KA_ZWRAP). */
#define KA_CELL 4.0
/** Base apothem of the mirror tube. */
#define KA_R 4.6
/** The largest gem's reach from its centre, with margin. */
#define KA_GEM_MAX 1.15
#define KA_BUBBLE 0.7
/** Sparkle cells per world unit on the mirror faces (z period 0.5 divides KA_ZWRAP). */
#define KA_SPARKLE_CELLS 2.0

float gSegOld;
float gSegNew;
float gBlend;
float gR;
float gGem;
float gMorph;
float gTurb;
float gWarp;
float gGemReach;
float gSpin;
float gLip;
float gMat;
vec3 gEye;

/** PCG integer hash: exact on every device, unlike a sin() hash. */
uint kalHash(uint h) {
    h = h * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    return (h >> 22u) ^ h;
}

/** The fold count for section number `n`: 4..9. */
float kalFolds(float n) {
    return 4.0 + float(kalHash(uint(max(n, 0.0))) % 6u);
}

/**
 * Twist, warp and fold `p` into one wedge of `seg` radians. The result's x is
 * the distance along the wedge's centre line (the tube face's normal), y the
 * distance across the face from that line (0 .. apothem * tan(seg / 2)), z the
 * untouched depth.
 */
vec3 kalFold(vec3 p, float seg) {
    float r = length(p.xy);
    // atan(0, 0) is undefined in GLSL ES; the fold is continuous at r = 0 anyway.
    float a = r > 1e-5 ? atan(p.y, p.x) : 0.0;
    a += p.z * KA_TWIST + gWarp * sin(p.z * (KA_TAU / 16.0) + uTime * 0.8);
    a = abs(mod(a, seg) - 0.5 * seg);
    return vec3(r * cos(a), r * sin(a), p.z);
}

/** The tube of `seg`-radian wedges at `p`; `mat` is 1 on a gem, 0 on the mirror. */
float kalTube(vec3 p, float seg, out float mat) {
    vec3 q = kalFold(p, seg);

    // The mirror face: a plane at the apothem, rippled by Turbulence.
    float rip = gTurb * sin(q.y * 2.6 + uTime * 0.7) * sin(q.z * (KA_TAU / 8.0) + uTime * 0.5);
    float wall = (gR + rip - q.x) / (1.0 + 2.8 * gTurb);

    // One gem per cell, hashed by the cell's index wrapped to the rebase period.
    float cz = floor(q.z / KA_CELL);
    float lz = q.z - (cz + 0.5) * KA_CELL;
    uint h = uhash(uint(mod(cz, KA_ZWRAP / KA_CELL)) + 0x51u);
    float h1 = uhashUnit(h);
    float h2 = uhashUnit(uhash(h));
    float ang = seg * (0.1 + 0.3 * h1);
    float rad = gR * (0.42 + 0.26 * h2) * gGemReach;
    vec3 s = vec3(q.x - rad * cos(ang), q.y - rad * sin(ang), lz);
    float sa = gSpin + h1 * KA_TAU;
    s.xz = mat2(cos(sa), -sin(sa), sin(sa), cos(sa)) * s.xz;
    float sb = gSpin * 0.7 + h2 * KA_TAU;
    s.xy = mat2(cos(sb), -sin(sb), sin(sb), cos(sb)) * s.xy;
    float size = gGem * (0.65 + 0.45 * h2);
    float gem = mix(sdOctahedron(s, size), sdBox(s, vec3(size * 0.56)), gMorph);
    // A neighbour cell's gem is at least the boundary distance plus the gap away.
    gem = min(gem, 0.5 * KA_CELL - abs(lz) + (0.5 * KA_CELL - KA_GEM_MAX));

    mat = gem < wall ? 1.0 : 0.0;
    return min(wall, gem);
}

/** The tube and its gems, both section counts blended: the field the grow hooks act on (map() hands it to its shell wrapper). */
float kalField(vec3 p) {
    float m;
    float d = kalTube(p, gSegNew, m);
    // gBlend is uniform, so this branch is coherent and costs nothing at rest.
    if (gBlend < 0.999) {
        float m0;
        float d0 = kalTube(p, gSegOld, m0);
        d = mix(d0, d, gBlend);
        m = mix(m0, m, gBlend);
    }
    gMat = m;
    return d / gLip;
}

/** kalField through lib_shell's hooks: exactly kalField(p) at rest, and kalField read once, in a loop the compiler cannot unroll. */
float kalFieldGrown(vec3 p) {
    shellStart3(p, SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK);
    float r = 0.0;
    for (int i = SHELL_FROM; i < 7; i++) {
        if (shellPass3(i)) r = shellKeep3(i, kalField(shellAt3(i)));
    }
    return r;
}

float map(vec3 p) {
    return max(kalFieldGrown(p), KA_BUBBLE - length(p - gEye));
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** The flat style's flowing pattern, on a mirror face (q from kalFold) at clock `t`. */
float kalPattern(vec3 q, float t, float dens) {
    float tri = abs(2.0 * fract(q.z / 16.0 + t * 0.05) - 1.0);
    return sin(q.y * 3.2 * dens + t) * cos(q.z * (KA_TAU / 4.0) - t * 0.8)
        + 0.6 * sndEq(tri);
}

void main() {
    vec2 uv = view();

    // ---- the shape inputs (lib_shell): each 0..1, 0.5 the tuned look --------------------
    float nPulse = shellKnob(0);                                   // PULSE    the kick's depth
    float nTexture = shellKnob(1);                                 // TEXTURE  pattern and sparkle density
    float nFlow = shellKnob(2);                                    // FLOW     the pattern's speed
    float nTone = shellKnob(3);                                    // TONE     where the gems hang, the hue
    float nBuild = shellKnob(4);                                   // BUILD    gem glow and size
    float nSpace = shellKnob(5);                                   // SPACE    tube width, fog
    float nMorph = 0.5 + 0.5 * clamp(uMorph, 0.0, 1.0);            // Morph    octahedra to cubes
    float nWarp = 0.5 + 0.5 * clamp(uWarp, 0.0, 1.0);              // Warp     the folds' wave
    float nTurb = 0.5 + 0.5 * clamp(uTurbulence, 0.0, 1.0);        // Turbulence the mirrors' ripple
    float kickK = shellMap(nPulse, 0.3, 1.0, 1.7);
    float dens = shellMap(nTexture, 0.7, 1.0, 1.3);
    float sparkBar = shellMap(nTexture, 0.96, 0.93, 0.88);
    float flow = shellMap(nFlow, 0.0, 0.5, 1.0);
    float reach = shellMap(nTone, 1.5, 1.0, 0.5);
    float hueTone = shellMap(nTone, -0.15, 0.0, 0.15);
    float build = shellMap(nBuild, 0.0, 0.5, 1.0);
    // The largest gem, (size + 0.45) x 1.1, stays inside KA_GEM_MAX.
    float gemSize = shellMap(nBuild, 0.4, 0.5, 0.55);
    float widen = shellMap(nSpace, -0.8, 0.0, 0.8);
    float fogK = shellMap(nSpace, 0.08, 0.06, 0.04);
    float morph = shellMap(nMorph, 0.0, 0.0, 1.0);
    gWarp = shellMap(nWarp, 0.0, 0.0, 0.25);
    gTurb = shellMap(nTurb, 0.0, 0.0, 0.22);

    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float low = sndShaped(SND_LOW);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0) * kickK;
    float hat = clamp(SND_HAT_ENV, 0.0, 1.0);
    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float pitch = clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0);
    float dropBump = 4.0 * drop * (1.0 - drop);
    float pitchBump = 4.0 * pitch * (1.0 - pitch);

    // The section crossfade, without frame-to-frame state: when a section
    // lands, SND_CUT_COUNT steps and SND_HELD_HARD_CUT jumps to 1 on the same
    // frame, so the weight starts at the OLD count and eases to the new one.
    float w = 1.0 - clamp(SND_HELD_HARD_CUT, 0.0, 1.0);
    gBlend = w * w * (3.0 - 2.0 * w);
    gSegOld = KA_TAU / kalFolds(SND_CUT_COUNT - 1.0);
    gSegNew = KA_TAU / kalFolds(SND_CUT_COUNT);

    gR = max(3.6, KA_R * (1.0 + 0.22 * dropBump) + widen);
    gGem = gemSize + 0.3 * clamp(kick, 0.0, 1.5);
    gMorph = morph * (0.75 + 0.25 * sin(uTime * 0.13));
    gSpin = uTime * 0.35;
    // TONE: how far out from the axis the gems hang (1 at rest).
    gGemReach = clamp(reach, 0.55, 1.3);
    // dtheta/dz of the twist plus the warp wave, times the largest radius met
    // inside the tube (its corner, at most apothem / cos(pi / 4)).
    float dth = KA_TWIST + gWarp * (KA_TAU / 16.0);
    float rMax = (gR + gTurb) * 1.4142136;
    gLip = sqrt(1.0 + rMax * rMax * dth * dth);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term has a period dividing 64, so this rebase is exact.
    ro.z = mod(ro.z, KA_ZWRAP);
    gEye = ro;
    // The layers are measured from the eye; a spawned set slides half a gem cell down the tube.
    shellSetup3(ro, 3.0, 0.5 * KA_CELL, false);

    float t = 0.03;
    float hitT = -1.0;
    float used = 0.0;
    float mat = 0.0;
    for (int i = 0; i < KA_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (t > KA_FAR) break;
        used = float(i);
        float d = map(ro + rd * t);
        float eps = 0.0008 * t + 0.0005;
        if (d < eps) {
            hitT = t;
            mat = gMat;
            break;
        }
        t += max(d * 0.9, eps);
    }

    float hue = 0.03 * sin(uTime * 0.021) + 0.5 * dropBump + 0.22 * pitchBump + hueTone;
    // The far end of the tube is the toy's light.
    float ahead = pow(max(rd.z, 0.0), 6.0);
    vec3 fogCol = pal(hue + 0.15) * (0.03 + (0.3 + 0.5 * low) * ahead) + vec3(0.004, 0.003, 0.007);
    vec3 col = fogCol;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.0012 * hitT));
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.2, 0.25, 1.0);
        float spec = pow(max(dot(reflect(rd, n), -rd), 0.0), 36.0);
        float px = hitT * 2.0 / (max(uCamFocal, 0.1) * uResolution.y);

        // Both tubes' folded points, so the pattern crossfades with the fold.
        vec3 qNew = kalFold(p, gSegNew);
        vec3 qOld = kalFold(p, gSegOld);
        float fNew = mix(kalPattern(qNew, uTime, dens), kalPattern(qNew, uTime * 1.8, dens), flow);
        float fOld = mix(kalPattern(qOld, uTime, dens), kalPattern(qOld, uTime * 1.8, dens), flow);
        float f = mix(fOld, fNew, gBlend);
        vec3 q = mix(qOld, qNew, gBlend);

        vec3 surf;
        if (mat < 0.5) {
            // Mirror glass: dark, with the pattern glowing in it and a headlight sheen.
            vec3 glow = pal(f * 0.3 + q.y * 0.08 + hue) * max(0.3 + f * 0.5 + low * 0.6, 0.0);
            surf = pal(hue + 0.5) * 0.03 + glow * (0.55 + 0.45 * ao)
                 + fogCol * fres * 1.5 + vec3(spec) * 0.25 * ao;

            // The mirror joints: the tube's corners, where two faces meet.
            float seg = mix(gSegOld, gSegNew, gBlend);
            float corner = gR * tan(0.5 * seg) - q.y;
            float joint = 1.0 - smoothstep(0.0, 0.035 + 1.5 * px, abs(corner));
            surf += pal(hue + 0.55) * joint * (0.35 + 0.4 * swell);

            // Hat sparkles on the faces, re-rolled per onset. A cell carries one
            // when its hash clears the bar; the fold mirrors it into every wedge.
            vec2 fc = vec2(q.y, q.z) * KA_SPARKLE_CELLS;
            vec2 cell = floor(fc);
            vec2 local = fract(fc) - 0.5;
            uvec2 c = uvec2(ivec2(int(cell.x), int(mod(cell.y, KA_ZWRAP * KA_SPARKLE_CELLS))));
            float pick = float(kalHash(c.x ^ kalHash(c.y ^ kalHash(uint(max(SND_ONSET_COUNT, 0.0))))) & 0xffffu)
                * (1.0 / 65536.0);
            float sparkle = step(sparkBar, pick) * (1.0 - smoothstep(0.0, 0.3 + px, length(local))) * hat;
            surf += pal(f * 0.3 + 0.5 + hue) * sparkle * 1.2;
        } else {
            // Gems: faceted glass lit from the tube's far end and the eye.
            float diff = clamp(dot(n, normalize(vec3(0.0, 0.3, 1.0))) * 0.5 + 0.5, 0.0, 1.0);
            vec3 body = pal(hue + 0.25 + 0.3 * dot(n, vec3(0.35, 0.5, 0.2)));
            surf = body * (0.08 + 0.5 * diff) * ao
                 + pal(hue + 0.65) * fres * 0.8
                 + vec3(spec) * 0.6
                 + body * (0.1 + 0.7 * clamp(kick, 0.0, 1.5)) * build;
        }
        col = mix(surf, fogCol, 1.0 - exp(-hitT * fogK));
    }

    col *= 0.85 + 0.25 * swell;
    col *= 0.75 + 0.25 * (1.0 - smoothstep(0.5, 2.0, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
