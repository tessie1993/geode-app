#version 300 es
#define VIEW_OWNS_SHAPE
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
//#include lib_lucid

// Lucid Hive: a flight down the inside of a honeycomb. The bore is lined with
// hexagonal wax pockets, 24 around and endless along the travel, and the back
// of every pocket is a window onto its own little world: the cell's own
// simplex nest, seeded by the cell's hash, so each one is a different marbled
// dream behind the same glass. As in the flat style the channel order is per
// cell, so the comb is a quilt of one field in three colour permutations,
// held apart by dark wax walls that glow softly from the world inside them.
// The pocket floors are shallow dishes, deeper at the heart; each cell's world
// turns at its own rate.
//
// ---- camera -----------------------------------------------------------------
//
// camFly down +z. The comb is the hex lattice (2 / sqrt 3, 2) unrolled onto a
// cylinder whose circumference is exactly 24 lattice columns, so it closes
// around the bore without a seam. Its z period is 2, the cell hash wraps at
// 1024 cells and the Twist shear has a 64 period: all powers of two dividing
// 8192, so the rig's travel wrap is invisible, and the eye is rebased into
// 0..1024 in main. The pockets form a shell (bore radius to an outer skin), so
// from the rig's wide glide or orbit paths, where the eye can leave the bore,
// the hive reads as a lit tube from outside, never a wall of black.
//
// ---- audio ------------------------------------------------------------------
//
// The SNARE (SND_SNARE_ENV) turns every cell inside out, now in depth as well
// as in colour: each world's frame is blended toward its circle inversion
// (r -> R^2 / r, the rim swaps with the heart), and the pocket's floor flips
// from a dish to a dome, its heart rising toward the mouth. Both ride
// lucidBump of the snare, zero on the hit, swelling as it decays and relaxing
// back, so the inversion breathes rather than snapping. The worlds turn on
// time alone (owner 2026-11-12, no style spins to the music: uHarmony and k2
// FLOW no longer set their rate). Brightness follows SND_LEVEL gently, and
// SND_ENERGY_TREND lights the far end of the bore.
//
// ---- macros and knobs -------------------------------------------------------
//
// Speed is in uTime and the rig's flight. Complexity: uMorph thins the wax
// walls, zooms each world out, and past 0.5 adds a nest level (lucidDetail).
// Chaos: uTurbulence/uWarp raise the nest's warp strength and jitter each world
// (lucidChaos, lucidJitter), and Turbulence also lets each pocket's floor bob.
// uTwist shears the comb into a slow helix. Color: every cell's tint is pal()
// at its own hash. Energy: uIntensity scales the snare inversion. Space:
// uZoom, uRotation and the kaleidoscope through view(). Knobs (offsets from
// 0.5, so off is the tuned look): k0 PULSE the inversion, k1 TEXTURE the nest
// warp, k3 TONE the hue, k4 BUILD the worlds'
// glow, k5 SPACE the pocket depth.
//
// ---- why the distance estimate is safe --------------------------------------
//
// The solid is the shell (bore radius LH_R to the outer skin) minus the
// pockets, so its distance is at least the max of a lower bound to the shell
// and one to the outside of the pockets. The shell's radial terms are exact.
// A pocket lies inside its own hex cell (its radius gRin is below the cell's
// apothem), so the nearest wax from inside a pocket is that pocket's own side
// or floor. The side distance is measured in unrolled units, where one unit is
// R / r of a real one: below the bore radius that overstates the distance, so
// it is scaled by r / R there; above it the unrolled distance understates the
// real one and is safe as it is. The floor's dish slopes at most
// 2 * LH_DISH / gRin against the hex radius, so that term is divided by
// LH_FLOOR_LIP. The Twist shear adds at most C * gTwist / 64 per unit of z, and
// the step shrinks by it. A sphere around the eye is carved out of everything.

#define LH_MAX_STEPS 96
#define LH_FAR 40.0
#define LH_TAU 6.2831853
/** Hex lattice in world units: x spacing 2 / sqrt(3), z spacing 2 (the z period). */
#define LH_RX 1.1547005
#define LH_HR vec2(LH_RX, 2.0)
/** Cell apothem: half the x spacing. */
#define LH_APO 0.5773503
/** Lattice columns around the bore. */
#define LH_N 24
/** The circumference, LH_N * LH_RX, and the bore radius it gives. */
#define LH_C 27.712812
#define LH_R 4.4106
/** Cells along z before the hash repeats; divides 8192. */
#define LH_ZWRAP 1024.0
/** Pocket depth at rest, world units. */
#define LH_DEPTH 0.9
/** Height of the floor's dish (or, inverted, its dome). */
#define LH_DISH 0.28
/** Gradient bound of the floor term: sqrt(1 + (2 * dish / rIn)^2) with margin. */
#define LH_FLOOR_LIP 1.7
#define LH_BUBBLE 0.7

vec3 gEye;
float gRin;
float gDepth;
float gFlip;
float gTurb;
float gTwist;
float gRout;


float hexDist(vec2 p) {
    p = abs(p);
    return max(dot(p, vec2(0.5, 0.8660254)), p.x);
}

/** The style's world at p: its distance, and in `found` what is there (a.x the hex distance, a.y and a.z the cell's hashes, a.w the pocket floor's radius, b.xy the point in the cell, b.zw the cell id, mat the radius off the axis). */
float mapAt(vec3 p, inout LucidHit found) {
    float r = length(p.xy);
    float ang = r > 1e-4 ? atan(p.y, p.x) : 0.0;  // atan(0, 0) is undefined in GLSL ES
    ang += gTwist * sin(p.z * (LH_TAU / 64.0));
    // Unrolled onto the bore: u runs 0..C around, one unit is one world unit at LH_R.
    vec2 q = vec2(fract(ang / LH_TAU) * LH_C, p.z);

    vec2 h = LH_HR * 0.5;
    vec2 a = mod(q, LH_HR) - h;
    vec2 b = mod(q - h, LH_HR) - h;
    vec2 gv = dot(a, a) < dot(b, b) ? a : b;
    vec2 cc = q - gv;
    // Centres sit on half-columns and whole z; both ids wrap with the comb.
    int ix = int(mod(floor(cc.x / (LH_RX * 0.5) + 0.5), float(2 * LH_N)));
    int iz = int(mod(floor(cc.y + 0.5), LH_ZWRAP));
    uint u = uhash3(uvec3(ivec3(ix, 11, iz)));
    float hc = uhashUnit(u);
    float h2 = uhashUnit(uhash(u));

    float hd = hexDist(gv);
    float x = clamp(hd / gRin, 0.0, 1.0);
    float floorR = LH_R + gDepth * (0.8 + 0.4 * h2) + LH_DISH * (1.0 - x * x) * gFlip
                 + gTurb * 0.12 * sin(uTime * (0.7 + h2) + hc * 40.0);

    float dShell = max(LH_R - r, r - gRout);
    float side = (gRin - hd) * min(r / LH_R, 1.0);
    float dPocket = min(side, (floorR - r) / LH_FLOOR_LIP);
    float d = max(dShell, dPocket);

    found.b.xy = gv;
    found.b.zw = vec2(float(ix), float(iz));
    found.a.x = hd;
    found.a.y = hc;
    found.a.z = h2;
    found.a.w = floorR;
    found.mat = r;
    return max(d, LH_BUBBLE - length(p - gEye));
}

/** The style's world at p as a hit, for lib_lucid_field. */
LucidHit lucidMap(vec3 p) {
    LucidHit h = lucidHit(0.0);
    float d = mapAt(p, h);
    h.d = d;
    return h;
}

//#include lib_lucid_field

/** The flat style's cell world, at the pocket-local point l (0.5 at the edge midpoints). */
vec3 cellWorld(vec2 l, float hc, float h2, float inv, float morph, float nestK, float hue0) {
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)
    int depth = lucidDetail(3, 2);
    int oct = lucidDetail(3, 2);

    // Inside-out: blend toward the inversion in the cell's inscribed circle.
    vec2 li = l * (0.2 / max(dot(l, l), 0.035));
    vec2 lc = mix(l, li, inv);

    // The rate a mid tonality set before (calm 0.5): no sound sets it now.
    float spin = t * (0.15 + 0.35 * hc) * 0.7 + hc * LUCID_TAU;
    vec2 q = lucidRot(spin) * lc * (2.6 + 1.2 * morph) + vec2(hc, h2) * 37.0;
    q += lucidJitter(q, t);
    vec4 n = lucidNest(q, depth, 1.4 * lucidChaos() * nestK, t * 0.3, LUCID_SIMPLEX, oct);

    vec3 raw = 0.5 + 0.5 * vec3(n.z, n.x, n.y);
    vec3 col = raw * mix(vec3(1.0), pal(hc + hue0 + 0.3 * n.z), 0.55);
    col = lucidChannelTurn(col, floor(hc * 3.0) + 0.3 * n.w);
    col = lucidCubic(col, 0.9);
    col = lucidCube(col * 1.35);
    // The soft glow just inside the wax, as the flat cells had.
    float e = lucidHexEdge(l);
    col += pal(hc + hue0 + 0.5) * smoothstep(0.3, 0.45, e) * 0.18;
    return col;
}

void main() {
    LucidHit fld = lucidHit(0.0);
    vec2 uv = view();

    // The normalization shell (lib_lucid): every variable from its driver, mapped to its range at its one site.
    LucidIn N = lucidIn();

    float snare = clamp(SND_SNARE_ENV * uSoundDrive, 0.0, 1.0);
    float swell = clamp(SND_LEVEL * uSoundDrive, 0.0, 1.5);
    float trend = clamp(SND_ENERGY_TREND * uSoundDrive, 0.0, 1.5);

    float morph = lucidSpan(N.depth, 0.0, 0.0, 1.0);

    float inv = clamp(lucidBump(snare) * 0.85 * uIntensity * lucidSpan(N.emit, 1.0, 0.5, 1.5), 0.0, 0.95);
    gFlip = 1.0 - 2.0 * inv;
    gRin = LH_APO * (0.84 + 0.1 * morph);
    gDepth = LH_DEPTH * lucidSpan(N.reach, 1.0, 0.7, 1.3);
    gTurb = lucidSpan(N.turb, 0.0, 0.0, 0.75);
    gTwist = lucidSpan(N.twist, 0.0, -0.5, 0.5);
    gRout = LH_R + gDepth * 1.2 + LH_DISH + gTurb * 0.12 + 0.3;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 1024, so this shift is
    // invisible and exact (power of two). Without it, far down the 8192
    // travel a highp z has ~0.001 of fraction left and the cells speckle.
    ro.z = mod(ro.z, LH_ZWRAP);
    gEye = ro;

    // Skip the empty bore: nothing of the comb comes nearer the axis than LH_R.
    float t = 0.03;
    float rin = LH_R - 0.05;
    float a2 = dot(rd.xy, rd.xy);
    float c2 = dot(ro.xy, ro.xy) - rin * rin;
    if (c2 < 0.0) {
        if (a2 < 1e-6) {
            t = LH_FAR;
        } else {
            float b2 = dot(ro.xy, rd.xy);
            t = max(t, (-b2 + sqrt(max(b2 * b2 - a2 * c2, 0.0))) / a2);
        }
    }

    // The shear's extra gradient along z, at the comb's own scale.
    float stepScale = 0.85 / (1.0 + abs(gTwist) * LH_C / 64.0);
    float hitT = -1.0;
    for (int i = 0; i < LH_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (t > LH_FAR) break;
        fld = lucidField(ro + rd * t);
        float d = fld.d;
        float eps = 0.0015 + 0.0008 * t;
        if (d < eps) {
            hitT = t;
            break;
        }
        t += max(d * stepScale, eps);
    }

    float hue0 = lucidSpan(N.tone, 0.0, -0.06, 0.06);
    float ahead = pow(max(dot(rd, normalize(uCamBasis[2])), 0.0), 10.0);
    vec3 fogCol = pal(hue0 + 0.5) * (0.02 + 0.2 * ahead * (0.5 + 0.5 * trend)) + vec3(0.004, 0.003, 0.002);
    vec3 col = fogCol;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = lucidNormal(p, max(0.0015, 0.001 * hitT));
        // The hit at p again for the shading, after the normal taps.
        fld = lucidField(p);

        // Headlight on the eye.
        float dif = max(dot(n, -rd), 0.0);
        float spec = pow(max(dot(reflect(rd, n), -rd), 0.0), 40.0);
        float fall = 1.0 / (1.0 + 0.04 * hitT * hitT);
        vec3 wax = pal(fld.a.y + hue0 + 0.5);
        bool inPocket = fld.a.x < gRin + 0.02;

        if (fld.mat > gRout - 0.05) {
            // The outer skin, seen from a glide outside the bore.
            col = wax * (0.03 + 0.08 * dif) * fall + vec3(0.6) * spec * 0.15 * fall;
        } else if (inPocket && fld.mat > fld.a.w - 0.03) {
            // The world behind the glass: emissive, with a glassy sheen.
            vec3 world = cellWorld(fld.b.xy * (0.5 / LH_APO), fld.a.y, fld.a.z, inv, morph, lucidSpan(N.scale, 1.0, 0.6, 1.4), hue0);
            col = world * lucidSpan(N.glow, 1.0, 0.55, 1.45) + mix(vec3(1.0), wax, 0.4) * spec * 0.35 * fall;
        } else if (inPocket && fld.mat > LH_R + 0.03) {
            // The pocket's wax sides: lit from the world below, darker toward the mouth.
            float deep = clamp((fld.mat - LH_R) / max(fld.a.w - LH_R, 0.05), 0.0, 1.0);
            col = wax * (0.02 + 0.06 * dif * fall + 0.3 * deep * deep * lucidSpan(N.glow, 1.0, 0.7, 1.3))
                + mix(vec3(1.0), wax, 0.5) * spec * 0.2 * fall;
        } else {
            // The comb's face: dark wax, a faint glow on each pocket's lip.
            float lip = 1.0 - smoothstep(gRin, LH_APO - 0.02, fld.a.x);
            col = wax * (0.025 + 0.07 * dif * fall + 0.12 * lip)
                + mix(vec3(1.0), wax, 0.5) * spec * 0.3 * fall;
        }
        col = mix(col, fogCol, 1.0 - exp(-hitT * 0.07));
    }

    col *= 0.85 + 0.12 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
