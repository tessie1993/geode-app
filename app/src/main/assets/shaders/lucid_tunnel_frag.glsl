#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP; uSoundTex is R32F.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_lucid

// Lucid Tunnel: a real tube, flown through, painted inside with a sine lattice
// folded through itself four times and lit like a black-light poster. The old
// style drew the wall in (angle, 1/radius) screen coordinates; now the wall is
// a solid shell around the world's z axis whose surface is pushed in and out
// by the lattice's first level, so the paint sits on real ridges and troughs,
// a dim headlight on the eye picks out their relief, and the far end of the
// bore sinks into violet UV haze.
//
// Every term of the wall field still uses only WHOLE multiples of the angle,
// so the lattice wraps the tube with no seam, even after the nest has pushed
// it around by its own warp (a field periodic in the angle, displaced by
// offsets that are themselves periodic in the angle, stays periodic). The
// shell is |wall - r| < thickness rather than a solid outside a hole, so from
// the rig's orbit and wide glide paths it reads as a lit pipe from outside.
//
// Black-light recolour, as before: the wall is near-black and only the zero
// crossings of the folded field glow, in two saturated palette colours that
// trade places down the tube, so it reads as day-glo paint under UV.
//
// ---- camera ------------------------------------------------------------------
//
// camFly: the flight down the bore is the Camera tab's fly speed, and the
// rig's dolly (kick punches, drop pushes) is already in uCamPos. The eye's z
// is rebased by LT_ZWRAP = 32. Depth enters the field only as
// u = z * TAU / 16 times whole numbers (the lattice and every nest level), the
// standing twist turns once per 32 units, the Warp bend has period 32 and the
// colour bands period 32, so every z term divides 32, 32 divides the rig's
// 8192 travel wrap, and neither the rebase nor the wrap ever shows. The drop's
// corkscrew is measured from the eye, so the rebase shifts both sides alike.
//
// ---- audio -------------------------------------------------------------------
//
// Drop (SND_HELD_SURGE)  corkscrews the tunnel, as before: winds a twist into
//                        the wall that grows with distance from the eye and
//                        spins the bore round, on lucidBump so it eases in as
//                        the drop's envelope falls and unwinds after it.
// Kick (SND_KICK_ENV)    a UV front: a band of brighter paint that leaves the
//                        eye on the hit and runs away down the tube as the
//                        envelope falls. Light only, never the geometry.
// sndShaped(SND_LEVEL)   widens the glowing lines a little.
// SND_ENERGY_TREND       the far end's haze and the overall level, gently.
//
// ---- macros and knobs ----------------------------------------------------------
//
// Speed: in uTime and uFlowTime (the lattice crawls along the wall on
// uFlowTime) and in the rig's fly speed. Complexity: uMorph past 0.5 adds a
// nest level (lucidDetail) and uMorph deepens the wall's relief. Chaos:
// uTurbulence/uWarp raise how far each nest level pushes the next
// (lucidChaos); Warp also bends the bore. Color: both glow colours and the
// wall's undertone are pal(), saturated by dayglo(). Energy: uIntensity scales
// the drop corkscrew, the line widening and the kick front. Space: uZoom,
// uRotation, uTwist and the kaleidoscope through view().
// Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE the kick
// front, k1 TEXTURE the line width, k2 FLOW the paint's glow, k3 TONE the hue,
// k4 BUILD the far haze, k5 SPACE the bore radius.
//
// ---- why the distance estimate is safe ----------------------------------------
//
// The geometry reads only the lattice's FIRST level (wallField, |f| <= 1),
// never the nest: the nest's gradient multiplies level by level and has no
// useful bound, so it only paints the hit point. wallField's slope is at most
// ~7.2 per radian of angle and ~3.4 per unit of u (0.39 per world unit, so
// ~1.35 per world unit along z). The relief is at most LT_RELIEF_MAX = 0.35,
// so around a wall of radius >= ~3.4 it slopes ~0.75, along z ~0.47. The
// standing twist (TAU/32 per unit) and the drop's corkscrew (<= 0.18 rad per
// unit) move the angle along z, adding <= 0.28 * 7.2 * 0.35 ~ 0.7; the Warp
// bend adds ~0.14. sqrt(1 + 0.75^2 + 1.31^2) < 1.85, so dividing the radial
// gap by LT_LIP = 2.0 keeps every step short of the wall. Near the axis the
// angular slope per world unit grows as 1/r, but there the gap itself is
// > 2.5 units and the march starts outside the inner cylinder no part of the
// shell can reach (the bore minus every inward term at its current size), so
// those steps never land. A sphere around the eye is carved out of the shell,
// so the eye is never inside it.

#define LT_MAX_STEPS 96
#define LT_TAU 6.2831853
/** Rebase period of the eye's z; divides 8192. */
#define LT_ZWRAP 32.0
/** World units per unit of u, as an angular rate: u = z * LT_UF, period 16. */
#define LT_UF (6.2831853 / 16.0)
/** Bore radius at rest: wider than the glide path's usual wander. */
#define LT_RADIUS 4.2
/** Half-thickness of the shell. */
#define LT_SHELL 0.3
/** Largest relief the wall can take (see the header). */
#define LT_RELIEF_MAX 0.35
/** How far down the tube (world units) the kick front has run once its envelope is spent. */
#define LT_REACH 16.0
/** Gradient bound of the radial field (see the header). */
#define LT_LIP 2.0
#define LT_FAR 44.0
#define LT_BUBBLE 0.8

vec3 gEye;
float gRadius;
float gRelief;
float gBend;
float gScrewRate;
float gSpin;
float gCrawl;

/** The wall: sine lattice, every angular frequency a whole number and every depth frequency a whole multiple of u. x = angle, y = u. */
float wallField(vec2 q, float level) {
    float a = q.x;
    float u = q.y;
    float s = sin(3.0 * a + 1.7 * sin(u + 2.0 * a + level));
    s *= cos(2.0 * a - 2.0 * u + level * 1.9);
    s += 0.5 * sin(5.0 * a + 3.0 * u - level);
    return s * 0.66;
}

/** Push a palette colour toward full saturation: fluorescent paint, not pastel. */
vec3 dayglo(vec3 c) {
    float lo = min(c.r, min(c.g, c.b));
    float hi = max(c.r, max(c.g, c.b));
    return clamp((c - lo) / max(hi - lo, 1e-3), 0.0, 1.0);
}

/** The bore's centre at depth z: the Warp bend, period 32 on both axes. */
vec2 boreCentre(float z) {
    return gBend * vec2(sin(z * (LT_TAU / 32.0)), cos(z * (LT_TAU / 32.0) + 1.3));
}

float mapAt(vec3 p, inout LucidHit found) {
    vec2 c = p.xy - boreCentre(p.z);
    float r = length(c);
    float ang = r > 1e-4 ? atan(c.y, c.x) : 0.0;
    // Standing twist (one turn per 32 units, wrap-exact), the drop's
    // corkscrew from the eye, and the slow spin of the whole bore.
    ang += p.z * (LT_TAU / 32.0) + gScrewRate * (p.z - gEye.z) + gSpin;
    vec2 q = vec2(ang, p.z * LT_UF + gCrawl);
    found.a.xy = q;
    float wall = gRadius + gRelief * wallField(q, 0.0);
    float d = (abs(wall - r) - LT_SHELL) / LT_LIP;
    return max(d, LT_BUBBLE - length(p - gEye));
}

/** The style's world at p, through mapAt: the distance and what was found there (the slots: see mapAt). */
LucidHit lucidMap(vec3 p) {
    LucidHit h = lucidHit(0.0);
    float d = mapAt(p, h);
    h.d = d;
    return h;
}

//#include lib_lucid_field

void main() {
    LucidHit fld = lucidHit(0.0);
    vec2 uv = view();
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)

    // The normalization shell (lib_lucid): every variable from its driver, mapped to its range at its one site.
    LucidIn N = lucidIn();

    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float energy = clamp(sndShaped(SND_LEVEL), 0.0, 1.5) * uIntensity;
    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);

    int depthLevels = lucidDetail(4, 3);
    float chaos = lucidChaos();

    float screw = clamp(lucidBump(drop) * uIntensity, 0.0, 1.5);
    gScrewRate = 0.12 * screw;
    gSpin = 1.4 * screw + t * 0.04;
    gCrawl = uFlowTime * 0.15;
    gRelief = LT_RELIEF_MAX * lucidSpan(N.depth, 0.6, 0.6, 1.0);
    gBend = lucidSpan(N.bend, 0.0, 0.0, 0.7);
    gRadius = LT_RADIUS + lucidSpan(N.reach, 0.0, -0.55, 0.55);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 32, so this shift is exact
    // and invisible, and z keeps its precision far down the travel.
    ro.z = mod(ro.z, LT_ZWRAP);
    gEye = ro;

    // Skip the empty middle: no part of the shell comes nearer the axis than
    // the bore minus every inward term at its current size.
    float rin = gRadius - (gRelief + gBend + LT_SHELL + 0.05);
    float tm = 0.03;
    float a2 = dot(rd.xy, rd.xy);
    float cc = dot(ro.xy, ro.xy) - rin * rin;
    if (rin > 0.5 && cc < 0.0) {
        if (a2 < 1e-6) {
            tm = LT_FAR;
        } else {
            float b = dot(ro.xy, rd.xy);
            tm = max(tm, (-b + sqrt(max(b * b - a2 * cc, 0.0))) / a2);
        }
    }

    float hitT = -1.0;
    float used = 0.0;
    for (int i = 0; i < LT_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (tm > LT_FAR) break;
        used = float(i);
        fld = lucidField(ro + rd * tm);
        float d = fld.d;
        float eps = 0.0008 * tm + 0.0008;
        if (d < eps) {
            hitT = tm;
            break;
        }
        tm += max(d * 0.9, eps);
    }

    float hue0 = lucidSpan(N.tone, 0.0, -0.06, 0.06);
    float ahead = pow(max(dot(rd, normalize(uCamBasis[2])), 0.0), 10.0);
    // UV haze: the bore's far end glows violet-dark, the old dark centre.
    vec3 fogCol = pal(hue0 + 0.75) * (0.015 + (0.10 + 0.12 * swell) * lucidSpan(N.glow, 1.0, 0.5, 1.5) * ahead)
                + vec3(0.004, 0.002, 0.009);
    vec3 col = fogCol;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = lucidNormal(p, max(0.0015, 0.0012 * hitT));
        // Restore the hit point's globals after the normal taps.
        fld = lucidField(p);
        vec2 q = fld.a.xy;

        // The nest, written out for this field, on the hit point only:
        // whole-number angular offsets and whole multiples of u.
        vec2 push = vec2(0.55, 0.9) * chaos;
        vec2 w = vec2(0.0);
        for (int i = 0; i < LUCID_MAX_DEPTH; i++) {
            if (i >= depthLevels) break;
            float lv = float(i);
            vec2 qi = vec2(q.x, q.y * floor(1.0 + 0.5 * lv)) + push * w;
            w = vec2(wallField(qi, lv * 1.3), wallField(qi + vec2(0.0, 3.1), lv * 2.1 + 0.7));
        }
        float f = wallField(q + push * w, 4.2);

        // Lines: the field's zero crossings, widened with distance so the far
        // wall softens instead of aliasing.
        float width = (0.06 + 0.04 * clamp(energy, 0.0, 1.0)) * lucidSpan(N.scale, 1.0, 0.4, 1.6)
                    + 0.004 * hitT;
        float line = width / (abs(f) + width);
        line = line * line;

        // Colour bands trade places down the tube: period 32 along z.
        float band = 0.5 + 0.5 * sin(p.z * (LT_TAU / 32.0) + w.x * 1.5);
        vec3 glow = mix(dayglo(pal(hue0 + 0.5)), dayglo(pal(hue0 + 0.85)), smoothstep(0.2, 0.8, band));

        // The kick front: brighter paint on a band that runs away from the eye.
        float dF = (p.z - gEye.z) - (1.0 - kick) * LT_REACH;
        float front = exp(-dF * dF * 0.12) * kick * lucidSpan(N.emit, 1.0, 0.5, 1.5)
                    * clamp(uIntensity, 0.0, 1.5);

        // The near-black wall under UV: a dim headlight for the relief, a
        // rim in the undertone, then the emissive paint on top.
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 0.8, 0.35, 1.0);
        float fall = 1.0 / (1.0 + 0.03 * hitT * hitT);
        float spec = pow(ndv, 24.0);
        vec3 wall = pal(hue0 + 0.75) * (0.05 + 0.04 * w.y + 0.12 * ndv * fall) * ao
                  + pal(hue0 + 0.6) * fres * 0.08
                  + vec3(spec) * 0.08 * fall;
        wall += glow * line * (1.1 + 0.9 * front) * lucidSpan(N.flow, 1.0, 0.4, 1.6) * (0.7 + 0.3 * ao);
        wall = lucidCubic(wall, 0.4);

        // UV haze swallows the far wall; the paint carries further than the wall.
        float fogF = 1.0 - exp(-hitT * 0.05);
        col = mix(wall, fogCol, fogF);
    }

    col *= 0.85 + 0.12 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.4, 2.2, length(uv) * uZoom));
    fragColor = vec4(grade(col), 1.0);
}
