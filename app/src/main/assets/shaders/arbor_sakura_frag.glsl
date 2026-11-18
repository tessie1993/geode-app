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
//#include lib_arbor

// Arbor Sakura: a cherry tree on a grassy knoll under a pale spring sky, a
// hazy snow-capped mountain behind it, blossoms opening along its twigs and
// petals drifting down through the air around it. Raymarched and orbited with
// camAround, but lit softly, in pastel washes with a little grain, so it still
// reads as the painted sakura it was in 2D.
//
// The tree is a 3D arborTree with a mirror fork (two arms) and a strong
// twist, so the forks turn through every plane, and positive bend, so the
// outer limbs sag into the cherry's wide sideways reach; the whole tree leans
// a little. Blossoms are spheres smooth-unioned onto every outermost tip
// through the hit's tip, and WHICH tips bloom is decided per branch from the hit's path,
// so every flower is a particular twig's and holds still while the tree
// sways. The mountain and sky are a backdrop by ray direction (no geometry),
// the knoll a gentle bump on the ground plane.
//
// ---- camera ---------------------------------------------------------------------
//
// camAround: the eye circles the tree at SK_ORBIT (Space and the dolly pull
// it in) and turns one revolution per 1024 world units of travel
// (CAM_SPIN_UNIT), so the rig's 8192 wrap lands on a whole number of turns
// and is invisible. Nothing in the scene reads world z or the travel itself,
// so there is no other seam to fit.
//
// ---- audio ------------------------------------------------------------------
//
// Onsets OPEN BLOSSOMS. Each twig tip has a hashed place on a cycle and the
// onset count (floor(SND_HIT_TIME)) slides a "flowering" window along it, so
// every onset opens a few new flowers and lets a few of the oldest fall: the
// tree is always about half in bloom, but WHERE it blooms keeps moving with
// the rhythm. The onset envelope (SND_ONSET_ENV) is the opening, new flowers
// growing from nothing while the departing ones shrink away, so nothing pops.
// Air (sndLevel(SND_TREBLE_ATT)) thickens the drift of falling petals. Swell
// (SND_INTENSITY) only warms the sky and the sun a little.
//
// ---- macros and knobs -------------------------------------------------------
//
// Speed: uTime drives the sway and the petal fall. Complexity: uMorph adds a
// fork level and so twice the tips (arbLevels, which also follows Detail).
// Chaos: uTurbulence and uWarp add a breeze flutter through the limbs and
// sway; uTwist turns the forks further. Colour: sky, mountain, bark,
// blossoms and petals from pal(). Energy: uIntensity scales the air petals.
// Space: camAround's orbit and dolly. Knobs (offsets from 0.5, so off is the
// tuned look): k0 PULSE the blossom size, k1 TEXTURE the paper grain, k2 FLOW
// the petal fall speed, k3 TONE the blossom hue, k4 BUILD the sun's glow, k5
// SPACE the orbit radius. The mids bow the boughs (arbMidBow).
//
// ---- why the estimate is safe ---------------------------------------------------
//
// The fold is 1-Lipschitz times its scale (see lib_arbor). Lean and sway are
// rigid rotations. The breeze flutter shifts xz by a sine of y with slope at
// most SK_FLUTTER_K times its amplitude per axis, so the tree's distance is
// divided by 1 + 1.5 * SK_FLUTTER_K * amp, a bound on that warp's stretch.
// Blossom radii differ per branch, and a point only sees its own branch's
// tip, so a flower straddling a fold plane could be missed from the far side;
// the march steps at 0.75 of the estimate to hide that rare overshoot, as
// every marched Arbor does. The knoll's slope is under 0.1, and the ground is
// scaled by 0.9 for it.

#define SK_MAX_STEPS 112
#define SK_FAR 22.0
#define SK_BASE -1.75
#define SK_ORBIT 6.2
#define SK_CYCLE 40.0
#define SK_WINDOW 0.55
#define SK_FLUTTER_K 2.7
#define SK_TAU 6.2831853

ArborForm gF;
float gGrow;
float gLip;
float gFlutter;
float gT;
mat2 gLean;
float gOnset;
float gN;
float gBloomScale;

float groundH(vec2 xz) {
    return SK_BASE - 0.08 + 0.26 * exp(-dot(xz, xz) * 0.12);
}

/** How open the blossom on branch `path` is right now, 0..1 (see the audio note). */
float bloomOpen(float path) {
    float hb = arbBranchHash(path, 0.0, 3.0);
    float phase = fract(hb - gN / SK_CYCLE);
    float step1 = 1.0 / SK_CYCLE;
    float open = step(phase, SK_WINDOW);
    // Newest flowers (just inside the window) open as the onset envelope
    // falls; the ones that just left it shrink away on the same envelope.
    open *= mix(1.0, 1.0 - gOnset, step(SK_WINDOW - step1, phase));
    open += gOnset * step(1.0 - step1, phase);
    return open;
}

/** The world: the tree in blossom and the lawn. mat.x is the blossom share (0 bark, 1 blossom), mat.y 1 on the ground. */
ArborHit arborMap(vec3 p) {
    vec3 q = p - vec3(0.0, SK_BASE, 0.0);
    q.xy = gLean * q.xy;
    q.xz += gFlutter * vec2(sin(q.y * SK_FLUTTER_K + gT * 1.3), cos(q.y * (SK_FLUTTER_K * 0.85) + gT * 1.1));
    ArborHit h = arborTree(q, gF, gGrow);
    float tree = h.d / gLip;
    float path = h.path;
    float tip = h.tip / gLip;

    float r = gBloomScale * bloomOpen(path) * (0.8 + 0.4 * hash11(path * 0.31 + 2.0));
    float bloom = mix(1e3, tip - r, step(0.002, r));
    float foliage = smin(tree, bloom, 0.035);

    float ground = (p.y - groundH(p.xz)) * 0.9;
    h.mat.x = smoothstep(-0.02, 0.02, tree - bloom);
    h.mat.y = step(ground, foliage);
    h.d = min(foliage, ground);
    return h;
}

//#include lib_arbor_field

/** The pale sky, the sun and the hazy mountain, all by ray direction. */
vec3 skyColour(vec3 rd, float hue, float swell, float glow) {
    // Clamped at the horizon, so a ray below it that misses the knoll meets
    // the same colour the fog fades the ground into: no seam.
    float up = clamp(rd.y, 0.0, 1.0);
    vec3 skyLo = mix(vec3(0.98, 0.90, 0.84), pal(hue) * 0.4 + 0.55, 0.4);
    vec3 skyHi = mix(vec3(0.70, 0.80, 0.94), pal(hue + 0.55) * 0.35 + 0.5, 0.4);
    vec3 col = mix(skyLo, skyHi, smoothstep(-0.05, 0.75, up)) * (0.96 + 0.05 * swell);
    vec3 sunDir = normalize(vec3(-0.55, 0.42, 0.6));
    float s = max(dot(rd, sunDir), 0.0);
    col += vec3(1.0, 0.88, 0.74) * (0.10 * pow(s, 6.0) + (0.25 + 0.2 * swell) * glow * pow(s, 90.0));

    // The mountain: a profile over azimuth (whole-number frequencies, so it
    // closes round the circle), one tall peak and low ridges.
    float az = atan(rd.z, rd.x);
    float peak = pow(max(cos(az - 1.1), 0.0), 18.0);
    float h = 0.03 + 0.20 * peak + 0.025 * sin(3.0 * az + 0.7) + 0.015 * sin(7.0 * az + 2.1);
    float edge = 0.004;
    float mtn = 1.0 - smoothstep(h - edge, h + edge, up);
    vec3 mtnCol = mix(col, mix(vec3(0.62, 0.66, 0.78), pal(hue + 0.6) * 0.6, 0.4), 0.55);
    float snow = smoothstep(0.10, 0.16, up) * smoothstep(h - 0.07, h - 0.04, up) * peak;
    mtnCol = mix(mtnCol, vec3(0.97), 0.6 * snow);
    col = mix(col, mtnCol, mtn);
    return col;
}

/**
 * Falling petals on one sheet: a cylinder of radius `rad` about the trunk,
 * at the ray's point `p`. The sheet's own (angle, height) coordinates are
 * world-fixed, so orbiting shows real parallax between the sheets. The
 * angular cells are a whole number round the circle, so the seam is a cell
 * wall. Returns the petal's coverage.
 */
float petalSheet(vec3 p, float rad, float fl, float air, float pxw, float fall) {
    float cs = 0.32 + 0.06 * fl;
    float cells = floor(SK_TAU * rad / cs);
    float a = atan(p.z, p.x) / SK_TAU + 0.5;
    float u = a * cells + gT * (0.20 + 0.06 * fl) + 0.15 * sin(gT * 0.7 + p.y * 2.0);
    float v = (p.y + gT * fall * (0.30 + 0.08 * fl)) / cs;
    vec2 cell = vec2(mod(floor(u), cells), floor(v));
    float h = hash13(vec3(cell, 13.0 + fl));
    vec2 lp = (vec2(fract(u), fract(v)) - 0.5) * cs;
    lp -= (vec2(hash13(vec3(cell, 14.0 + fl)), hash13(vec3(cell, 15.0 + fl))) - 0.5) * cs * 0.5;
    float spin = gT * (1.0 + h * 2.0) + h * 6.0;
    lp = rot2(spin) * lp;
    // A tumbling petal: one axis foreshortens as it turns over.
    lp.x /= max(abs(cos(spin * 0.7 + h * 3.0)), 0.3);
    float e = length(lp * vec2(1.0, 2.2)) - 0.035;
    float show = step(1.0 - (0.20 + 0.45 * air), h);
    // Only in the air: above the knoll and below the top of the crown.
    float band = smoothstep(groundH(p.xz), groundH(p.xz) + 0.15, p.y) * (1.0 - smoothstep(1.4, 2.2, p.y));
    return arbInk(e, pxw) * show * band;
}

void main() {
    vec2 uv = arbView();
    float px = max(length(fwidth(uv)), 1e-4);
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)
    gT = t;
    float energy = uIntensity;
    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float air = clamp(sndLevel(SND_TREBLE_ATT), 0.0, 1.0) * energy;
    gOnset = clamp(SND_ONSET_ENV, 0.0, 1.0);
    gN = floor(SND_HIT_TIME);

    // The normalization shell (lib_arbor): every variable from its driver, mapped to its range here, once.
    ArborIn N = arborIn();
    float chaos = arbSpan(N.chaos, 0.0, 0.0, 2.0);
    float orbit = arbSpan(N.reach, SK_ORBIT, 0.85 * SK_ORBIT, 1.15 * SK_ORBIT);
    float skyGlow = arbSpan(N.glow, 1.0, 0.2, 1.8);
    float fall = arbSpan(N.flow, 1.0, 0.6, 1.4);
    float grainK = arbSpan(N.density, 0.1, 0.025, 0.175);
    float hueShift = arbSpan(N.tone, 0.0, -0.06, 0.06);
    gBloomScale = arbSpan(N.emit, 0.12, 0.084, 0.156);

    float hue = 0.95 + 0.02 * sin(t * 0.03);
    float bloomHue = hue + 0.02 + hueShift;

    // ---- the tree's form -------------------------------------------------------
    float sway = 0.03 * sin(t * 0.6) + chaos * 0.04 * sin(t * 3.1) * sin(t * 1.7);
    gLean = rot2(-(0.14 + sway));
    gFlutter = 0.015 + 0.04 * chaos;
    gLip = 1.0 + 1.5 * SK_FLUTTER_K * gFlutter;
    gGrow = arbLevels(7.0, 1.0);
    gF = arborShape(ArborForm(2.0, 0.62, 0.16 + arbMidBow(), 1.9 + 0.4 * sway, 0.72, 0.82, 0.11, 0.72), N);

    vec3 ro;
    vec3 rd;
    camAround(uv, orbit, CAM_SPIN_UNIT, ro, rd);

    vec3 sky = skyColour(rd, hue, swell, skyGlow);
    vec3 haze = skyColour(normalize(vec3(rd.x, 0.02, rd.z)), hue, swell, 0.0);

    // ---- march -----------------------------------------------------------------
    float tm = 0.05;
    float hitT = -1.0;
    float used = 0.0;
    float isBloom = 0.0;
    float isGround = 0.0;
    float path = 0.0;
    for (int i = 0; i < SK_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (tm > SK_FAR) break;
        used = float(i);
        ArborHit h = arborField(ro + rd * tm);
        float d = h.d;
        if (d < 0.0008 * tm + 0.0006) {
            hitT = tm;
            isBloom = h.mat.x;
            isGround = h.mat.y;
            path = h.path;
            break;
        }
        tm += d * 0.75;
    }

    float grain = vnoise3(vec3(uv * 60.0, 1.0)) * 0.5 + vnoise3(vec3(uv * 9.0, 2.0)) * 0.5;
    vec3 petal = mix(vec3(1.0, 0.80, 0.86), pal(bloomHue) * 0.5 + 0.5, 0.45);

    vec3 col = sky;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = arborNormal(p, 0.0012 * hitT + 0.0006);
        vec3 l = normalize(vec3(-0.55, 0.42, 0.6));
        vec3 skyAmb = mix(vec3(0.70, 0.78, 0.92), pal(hue + 0.55) * 0.3 + 0.55, 0.3);
        float ao = arbStepAO(used, uSteps);
        if (isGround > 0.5) {
            // Grass, with fallen petals scattered thickest under the crown and
            // a dappled shade where the crown hangs over it.
            float r = length(p.xz - vec2(0.6, 0.0));
            vec3 grass = mix(vec3(0.55, 0.68, 0.45), pal(hue + 0.3) * 0.6, 0.3);
            float fallen = smoothstep(0.62, 0.78, vnoise3(vec3(p.xz * 9.0, 4.0))) * smoothstep(3.2, 0.8, r);
            vec3 g = mix(grass * (0.85 + 0.25 * vnoise3(vec3(p.xz * 3.0, 7.0))), petal, fallen * 0.85);
            float dif = clamp(dot(n, l) * 0.6 + 0.4, 0.0, 1.0);
            float shade = 1.0 - 0.35 * smoothstep(2.4, 0.5, r) * (0.6 + 0.4 * vnoise3(vec3(p.xz * 2.5, 9.0)));
            col = g * (0.35 * skyAmb + 0.75 * dif * shade);
        } else {
            vec3 lit = arbLight(n, rd, l, 0.5, 18.0);
            vec3 bark = mix(vec3(0.24, 0.15, 0.16), pal(hue + 0.05) * 0.25, 0.3);
            vec3 barkCol = bark * (0.75 + 0.4 * vnoise3(p * vec3(30.0, 8.0, 30.0)))
                         * (0.30 * skyAmb + 0.85 * lit.x) + bark * lit.z * 0.3;
            // Blossoms: soft, wrapped light that seems to pass through the
            // petals, a rim in the sun and a rosy heart, varied per flower.
            float fh = hash11(path * 0.53 + 7.0);
            vec3 bc = mix(petal, vec3(1.0, 0.95, 0.96), 0.35 * fh);
            float petals = 0.85 + 0.15 * vnoise3(p * 55.0);
            vec3 heart = pal(bloomHue + 0.1) * 0.5 + vec3(0.4, 0.2, 0.2);
            float facing = clamp(dot(n, -rd), 0.0, 1.0);
            vec3 bloomCol = bc * petals * (0.45 * skyAmb + 0.65 * lit.x) + vec3(1.0, 0.9, 0.85) * lit.y * 0.25;
            bloomCol = mix(bloomCol, heart * (0.5 + 0.5 * lit.x), 0.25 * smoothstep(0.85, 1.0, facing) * fh);
            col = mix(barkCol, bloomCol, isBloom);
        }
        col *= mix(1.0, ao, 0.7);
        col = mix(col, haze, 1.0 - exp(-hitT * 0.045));
    }

    // ---- falling petals ------------------------------------------------------------
    // Three sheets about the trunk; each ray meets each twice (behind the tree,
    // then in front). Far crossings first, near last, so the nearer petal wins.
    float limit = hitT > 0.0 ? hitT : SK_FAR;
    vec2 roxz = ro.xz;
    vec2 rdxz = rd.xz;
    float qa = max(dot(rdxz, rdxz), 1e-5);
    float qb = dot(roxz, rdxz);
    float qc0 = dot(roxz, roxz);
    for (int k = 0; k < 6; k++) {
        bool farHit = k < 3;
        float fl = farHit ? float(2 - k) : float(k - 3);
        float rad = 1.1 + 1.0 * fl;
        float disc = qb * qb - qa * (qc0 - rad * rad);
        if (disc <= 0.0) continue;
        float sq = sqrt(disc);
        float th = (farHit ? (-qb + sq) : (-qb - sq)) / qa;
        if (th <= 0.05 || th >= limit) continue;
        vec3 pp = ro + rd * th;
        float cover = petalSheet(pp, rad, fl, air, th * px / uCamFocal, fall);
        vec3 pc = petal * (0.9 + 0.1 * fl) * mix(0.85, 1.05, smoothstep(-0.5, 1.5, pp.y));
        pc = mix(pc, haze, 1.0 - exp(-th * 0.045));
        col = mix(col, pc, cover * (0.9 - 0.15 * fl));
    }

    col *= 0.92 + grainK * grain;
    col *= 0.86 + 0.14 * smoothstep(2.0, 0.3, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
