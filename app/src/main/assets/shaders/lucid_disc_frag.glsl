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
//#include lib_lucid

// Lucid Disc: the gilded hyperbolic tiling, now a lacquered medallion with
// depth. The flat style painted the (5, 4, 2) reflection group's tiling of the
// Poincare disc; here the disc is a real object: a low dome of lacquer tiles,
// orbited from above, its edge bound in a gilt torus. The fold is the flat
// style's own: the angle mirrored into a wedge of pi/5, then any point inside
// the wedge's third side (a circle orthogonal to the rim, a hyperbolic line)
// inverted through it, repeated until the point lands in the fundamental
// triangle. That third side is each tile's edge, so the tiles are the {5,4}
// pentagons, four at a corner, and they shrink without end toward the rim.
//
// Each pentagon is a raised lacquer tile, pillowed a little, with a bevel that
// falls into a gold-leaf groove where it meets its neighbours. Alternate tiles
// (odd numbers of inversions) take the lacquer's second tone and sit lower, so
// the two-colouring is now also a two-height relief. Inside each tile the
// family's cellular nest still craquelures the lacquer, and the cracks are
// laid in gold, as before; that part is shading on the marched surface. A
// Mobius translation still glides the whole tiling across the dish (the
// hyperbolic plane's own panning), so tiles swim out of the rim, cross the
// dome and sink back into it, while the dish itself stays put.
//
// ---- camera -------------------------------------------------------------------
//
// The rig, object-centred like camAround, with the eye raised LD_ELEV above
// the dish's plane, so the orbit circles the medallion from above (its
// identity is a disc seen face-on) and the relief shows as the view swings.
// The orbit rate is an integer multiple of CAM_SPIN_UNIT, so the 8192 travel
// wrap is whole turns and never shows; nothing in the field depends on world
// z, so there is no period to fit. The dolly pulls the eye in, the rig's look
// tilts it. There is no endless zoom: the tiling already recedes forever
// toward the rim, and a 3D style dives by moving the eye, never the UV.
//
// ---- audio --------------------------------------------------------------------
//
// Section (SND_CUT_COUNT, eased by SND_HELD_HARD_CUT) re-seeds the piece, as in
// the flat style: the glide's heading, the lacquer's pair of hues and where the
// craquelure samples, all reached by sliding (lucidSeed), never cut to.
// Kick (SND_KICK_ENV)    the raised tiles lift, as a lucidBump: a breath that
//                        rises after the hit and settles, never a jump.
// Snare (SND_SNARE_ENV)  the sunken tiles lift the same way, so the two
//                        colourings bounce against each other.
// Drop (SND_HELD_SURGE)  the dome swells, on a hump so it glides.
// SND_TREBLE_PRESENCE    sharpens and brightens the gold's sheen.
// SND_INTENSITY          brightness, gently. Nothing flashes on a hit: hits
//                        move tiles, and the light follows only the swell.
//
// ---- macros and knobs ----------------------------------------------------------
//
// Speed: uTime (glide, craquelure drift, orbit hue). Complexity: uMorph past
// 0.5 adds a nest level and raises the craquelure density (lucidDetail).
// Chaos: uTurbulence/uWarp raise the nest's warp and jitter its cells
// (lucidChaos, lucidJitter); uTwist twists the view through view(). Colour:
// both lacquer tones are pal(), the leaf is gold toned by pal(). Energy:
// uIntensity scales how far a section seed throws the glide, the treble sheen
// and the tile bounce. Space: uZoom/uRotation through view() and the rig.
// Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE the tile
// bounce, k1 TEXTURE the craquelure, k2 FLOW how far the glide reaches across
// the disc, k3 TONE the lacquer hue, k4 BUILD the gold's sheen and the rim,
// k5 SPACE the orbit radius.
//
// ---- budget -------------------------------------------------------------------
//
// One bounding-sphere test first, so rays that miss the medallion never
// march. 96-step ceiling broken on uSteps (Detail). Far from the dish the field
// is a cheap envelope (the dome plus the tallest tile, no fold at all); only
// within LD_NEAR of it does the fold run, LD_FOLDS times at most (LD_FOLDS_LEAN
// below Detail 80): eight inversions already cross four tiles of hyperbolic
// distance, which is past r = 0.99 for the {5,4} pentagon. The nest is read
// once per pixel, at the hit, and skipped where the craquelure is finer than
// a pixel. The shadow march (10 steps) is skipped below Detail 48.
//
// ---- why the distance estimate is safe ----------------------------------------
//
// The dish's top is a height field over the disc coordinate z = xz / LD_R:
//   top = D (1 - |z|^2) + H (1 - |z|^2) P(m),
// where m is z after the glide and the folds, and P = s(e) * pillow * parity
// is the tile profile (e the distance to the tile's edge circle in m, s a
// smoothstep over LD_BEVEL). The glide and every fold are isometries of the
// hyperbolic plane, and for any such map |dm/dz| = (1 - |m|^2) / (1 - |z|^2).
// So the (1 - |z|^2) the relief is scaled by cancels the fold's stretch:
//   |grad_z h| <= 2 H |z| P + H (1 - |m|^2) |grad_m P| <= H (2 + G),
// with G = 1.5 / LD_BEVEL + 3 * 0.4 (the smoothstep, then the pillow, whose
// |m| is under the pentagon's circumradius 0.40) = 22.6. With H <= LD_HMAX =
// 0.17 and the dome D <= 0.42, the world slope is under
// (0.17 * 24.6 + 2 * 0.42) / LD_R = 2.51, so the field divides the height gap
// by sqrt(1 + 2.51^2) < LD_LIP = 2.8. That holds however many folds ran; where
// the folds run out unconverged (only beside the rim) the profile reads 0, a
// step of at most H (1 - |z|^2), which is tiny there, and the 0.8 step factor
// absorbs it. The envelope is the same dome at D + H with P = 1, slope under
// 0.59, divided by LD_ENV_LIP = 1.2; the real surface lies below it, so a
// point above it is at least that far from the dish. The rim is an exact torus.

#define LD_MAX_STEPS 96
#define LD_SHADOW_STEPS 10
#define LD_FOLDS 8
#define LD_FOLDS_LEAN 6
/** The disc's radius, world units. */
#define LD_R 2.0
/** The gilt rim's tube radius. */
#define LD_RIM 0.075
/** How far below the rim the medallion's body goes. */
#define LD_BASE 0.2
/** Tile bevel width in the folded disc coordinate (the pentagon's inradius is 0.304). */
#define LD_BEVEL 0.07
/** The tallest a tile can stand at the disc's centre, world units (see the header). */
#define LD_HMAX 0.17
#define LD_LIP 2.8
#define LD_ENV_LIP 1.2
/** Within this of the envelope the full tiled field is evaluated. */
#define LD_NEAR 0.06
#define LD_BOUND 2.25
/** Eye elevation above the dish's plane, radians. */
#define LD_ELEV 0.92

// The (5, 4, 2) triangle's third side: centre (LD_D, 0), radius LD_RR, for
// pa = pi/5, qa = pi/4: rr = sin(pa) / sqrt(cos(qa)^2 - sin(pa)^2), d = sqrt(1 + rr^2).
#define LD_PA 0.62831853
#define LD_RR 1.4952
#define LD_RR2 2.2357
#define LD_D 1.7989

/** The glide's Mobius translation. */
vec2 gB;
int gFolds;
float gDome;
/** Height scale of the even (raised) and odd (sunken) tiles at the centre. */
float gHa;
float gHb;
float gHMax;
/** Set by map(): 0 dish, 1 rim; on the dish the folded point, inversions, edge distance, |z|^2. */
float gMat;
vec2 gM;
float gFlips;
float gE;
float gR2;

vec2 cmul(vec2 a, vec2 b) { return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
vec2 cdiv(vec2 a, vec2 b) { return cmul(a, vec2(b.x, -b.y)) / max(dot(b, b), 1e-6); }

/**
 * The tiled relief at disc point z (|z| < 1): glide, fold, profile. Sets gM,
 * gFlips, gE and returns the profile P (0 on a tile edge, about 1 on top).
 */
float ldTile(vec2 z) {
    vec2 m = cdiv(z - gB, vec2(1.0, 0.0) - cmul(vec2(gB.x, -gB.y), z));
    float flips = 0.0;
    float e = 0.0;
    for (int i = 0; i < LD_FOLDS; i++) {
        if (i >= gFolds) break;
        float l = length(m);
        // atan(0, 0) is undefined in GLSL ES; the origin is already in the wedge.
        float a = l > 1e-6 ? atan(m.y, m.x) : 0.0;
        a = mod(a, 2.0 * LD_PA);
        if (a > LD_PA) a = 2.0 * LD_PA - a;
        m = vec2(cos(a), sin(a)) * l;
        vec2 v = m - vec2(LD_D, 0.0);
        float l2 = dot(v, v);
        if (l2 >= LD_RR2) {
            e = sqrt(l2) - LD_RR;
            break;
        }
        m = vec2(LD_D, 0.0) + v * (LD_RR2 / max(l2, 1e-6));
        flips += 1.0;
    }
    gM = m;
    gFlips = flips;
    gE = e;
    float pillow = 1.0 - 1.5 * min(dot(m, m), 0.16);
    return smoothstep(0.0, LD_BEVEL, e) * pillow;
}

float map(vec3 p) {
    float rw = length(p.xz);
    float dRim = length(vec2(rw - LD_R, p.y + 0.01)) - LD_RIM;
    float r2 = min(rw * rw / (LD_R * LD_R), 1.0);
    float side = max(rw - LD_R, -LD_BASE - p.y);

    // The cheap envelope first: the dome plus the tallest tile, no fold.
    float env = max((p.y - (gDome + gHMax) * (1.0 - r2)) / LD_ENV_LIP, side);
    if (env > LD_NEAR) {
        gMat = dRim < env ? 1.0 : 0.0;
        return min(env, dRim);
    }

    vec2 z = p.xz / LD_R;
    float zl = length(z);
    if (zl > 0.999) z *= 0.999 / zl;
    float prof = ldTile(z);
    gR2 = r2;
    float h = (mod(gFlips, 2.0) < 0.5 ? gHa : gHb) * prof;
    float top = (gDome + h) * (1.0 - r2);
    float d = max((p.y - top) / LD_LIP, side);
    gMat = dRim < d ? 1.0 : 0.0;
    return min(d, dRim);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

void main() {
    vec2 uv = view();
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)
    float flow = uFlowTime;

    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kFlow = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    float kSpace = clamp(uKnob[5], 0.0, 1.0) - 0.5;

    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float treb = clamp(SND_TREBLE_PRESENCE, 0.0, 1.5) * uIntensity;
    float seed = lucidSeed(SND_CUT_COUNT, SND_HELD_HARD_CUT);
    float energy = clamp(uIntensity, 0.0, 1.5) * clamp(1.0 + 1.2 * kPulse, 0.3, 1.6);
    float kickB = lucidBump(SND_KICK_ENV) * energy;
    float snareB = lucidBump(SND_SNARE_ENV) * energy;
    float dropB = lucidBump(SND_HELD_SURGE);

    // The relief. Both tile heights stay under LD_HMAX, the dome under 0.42:
    // the distance bound in the header is written for those caps.
    gHa = min(0.10 * (1.0 + 0.55 * kickB), LD_HMAX);
    gHb = min(0.055 * (1.0 + 1.1 * snareB), LD_HMAX);
    gHMax = max(gHa, gHb);
    gDome = min(0.28 + 0.14 * dropB, 0.42);
    gFolds = uSteps < 80.0 ? LD_FOLDS_LEAN : LD_FOLDS;

    // The Mobius glide, the flat style's: b orbits inside the disc.
    float heading = seed * LUCID_TAU * uIntensity + t * 0.03;
    float reach = 0.42 * clamp(1.0 + 0.6 * kFlow, 0.5, 1.3);
    gB = reach * vec2(cos(heading), sin(heading)) * (0.6 + 0.4 * sin(flow * 0.11));

    // Object-centred orbit from above: camAround's eye, pitched down LD_ELEV.
    float orbit = uCamTravel * (3.0 * CAM_SPIN_UNIT);
    mat3 turn = mat3(cos(orbit), 0.0, -sin(orbit), 0.0, 1.0, 0.0, sin(orbit), 0.0, cos(orbit));
    float ce = cos(LD_ELEV);
    float se = sin(LD_ELEV);
    mat3 pitch = mat3(1.0, 0.0, 0.0, 0.0, ce, se, 0.0, -se, ce);
    mat3 basis = turn * pitch * uCamBasis;
    float radius = 4.6 * (1.0 + 0.4 * kSpace) * (1.0 - 0.35 * uCamDolly);
    vec3 ro = -basis[2] * radius;
    vec3 rd = normalize(basis * vec3(uv, uCamFocal));

    // Outside the disc: near black, a faint warm halo the medallion sits in.
    float sr = length(uv);
    vec3 leaf = mix(vec3(0.86, 0.62, 0.24), pal(0.1) * vec3(1.0, 0.8, 0.45), 0.35);
    vec3 bg = pal(seed + 0.5 + 0.12 * kTone) * 0.025 + leaf * 0.03 * exp(-sr * 1.4);
    vec3 col = bg;
    float buildK = clamp(1.0 + 1.2 * kBuild, 0.2, 2.2);

    vec3 bc = vec3(0.0, 0.1, 0.0);
    vec3 oc = ro - bc;
    float b = dot(oc, rd);
    float disc = b * b - (dot(oc, oc) - LD_BOUND * LD_BOUND);
    if (disc > 0.0) {
        float sq = sqrt(disc);
        float tm = max(-b - sq, 0.01);
        float tEnd = -b + sq;
        float hitT = -1.0;
        float used = 0.0;
        for (int i = 0; i < LD_MAX_STEPS; i++) {
            // uSteps is the user's Detail; this loop's own bound is the constant.
            if (float(i) >= uSteps) break;
            if (tm > tEnd) break;
            used = float(i);
            float d = map(ro + rd * tm);
            float eps = 0.0006 * tm + 0.0004;
            if (d < eps) {
                hitT = tm;
                break;
            }
            tm += max(d * 0.8, eps);
        }

        if (hitT > 0.0) {
            vec3 p = ro + rd * hitT;
            vec3 n = normalAt(p, 0.0008 * hitT + 0.0008);
            vec3 L = normalize(vec3(-0.45, 0.85, 0.3));

            // Soft shadow: raised tiles shade the grooves and the sunken tiles.
            float sh = 1.0;
            if (uSteps >= 48.0) {
                float st = 0.015;
                for (int j = 0; j < LD_SHADOW_STEPS; j++) {
                    float d = map(p + L * st);
                    sh = min(sh, 10.0 * d / st);
                    if (sh < 0.05) break;
                    st += clamp(d, 0.01, 0.1);
                }
                sh = clamp(sh, 0.0, 1.0);
                sh = 0.35 + 0.65 * sh * sh * (3.0 - 2.0 * sh);
            }
            // Restore the hit point's globals after the shading taps.
            map(p);
            float ao = clamp(1.0 - used / max(uSteps, 1.0) * 0.6, 0.5, 1.0);

            float dif = max(dot(n, L), 0.0);
            vec3 hv = normalize(L - rd);
            float ndv = clamp(dot(n, -rd), 0.0, 1.0);
            float fres = pow(1.0 - ndv, 4.0);
            // One world pixel at the hit, for fading detail finer than it.
            float px = hitT * 2.0 / (max(uCamFocal, 0.1) * uResolution.y);

            if (gMat > 0.5) {
                // The gilt rim.
                float spec = pow(max(dot(n, hv), 0.0), 30.0 + 40.0 * clamp(treb, 0.0, 1.0));
                col = leaf * (0.12 + 0.7 * dif * sh) * ao
                    + mix(vec3(1.0, 0.92, 0.7), pal(0.15), 0.25) * spec * (0.6 + 0.5 * clamp(treb, 0.0, 1.0)) * buildK * sh
                    + leaf * fres * 0.25 * buildK;
            } else {
                // How large the folded plane is here, in world units per unit of m:
                // the inverse of the fold's stretch (see the header).
                float mScale = LD_R * max(1.0 - gR2, 1e-4) / max(1.0 - dot(gM, gM), 1e-4);
                // The craquelure: the flat style's nest, in the folded plane.
                float cellDen = 7.0 + 3.0 * clamp(uMorph, 0.0, 1.0);
                // Under a pixel the cracks fade to their average gold cover (0.3),
                // so the rim's tiny tiles never shimmer; TEXTURE sets how much of
                // the craquelure shows at all.
                float vis = smoothstep(1.5, 4.0, mScale / cellDen / max(px, 1e-5))
                          * clamp(1.0 + 1.4 * kTexture, 0.0, 1.0);
                float crack = 0.3;
                float cellZ = 0.5;
                vec2 nxy = vec2(0.0);
                float nz = 0.5;
                if (vis > 0.01) {
                    int depth = lucidDetail(3, 2);
                    vec2 q = gM * cellDen + vec2(seed * 31.0, seed * 17.0);
                    q += lucidJitter(q, t);
                    vec4 nst = lucidNest(q, depth, 0.9 * lucidChaos(), t * 0.15, LUCID_CELLS, 1);
                    vec3 cell = lucidCells(q + 0.9 * nst.xy, 0.85);
                    float cr = 1.0 - smoothstep(0.02, 0.09, cell.y - cell.x);
                    crack = mix(0.3, cr, vis);
                    cellZ = mix(0.5, cell.z, vis);
                    nxy = nst.xy * vis;
                    nz = mix(0.5, nst.z, vis);
                }
                // The tile groove, gilded; faded to its average where under a pixel.
                float grooveW = LD_BEVEL * 0.45;
                float grooveVis = smoothstep(1.0, 3.0, grooveW * mScale / max(px, 1e-5));
                float groove = 1.0 - smoothstep(grooveW * 0.6, grooveW, gE);
                groove = mix(0.35, groove, grooveVis);
                float gold = max(crack, groove);

                float parity = mod(gFlips, 2.0);
                float hue = seed + 0.12 * kTone;
                vec3 lacquerA = pal(hue) * 0.32;
                vec3 lacquerB = pal(hue + 0.5) * 0.18;
                vec3 lacquer = mix(lacquerA, lacquerB, parity) * (0.7 + 0.5 * cellZ);
                lacquer = lucidCubic(lacquer, 0.35);
                // The bevel's foot sits in its own shade.
                float bevelAo = 0.55 + 0.45 * smoothstep(0.0, LD_BEVEL, gE);

                // Lacquer: a dark body under a clear coat.
                float coat = pow(max(dot(n, hv), 0.0), 70.0);
                vec3 env = mix(pal(hue + 0.25) * 0.05, leaf * 0.22, smoothstep(-0.2, 0.8, reflect(rd, n).y));
                vec3 lac = lacquer * (0.25 + 0.95 * dif * sh) * ao * bevelAo
                         + vec3(0.9, 0.85, 0.8) * coat * 0.35 * sh
                         + env * fres * 0.8;

                // Gold leaf: metallic, its sheen roughened by the nest as if it
                // were a surface normal (the flat style's trick, on a real normal).
                vec3 gn = normalize(n + vec3(nxy.x, 0.0, nxy.y) * 0.35);
                float sheen = pow(max(dot(gn, hv), 0.0), 6.0 + 10.0 * clamp(treb, 0.0, 1.0));
                vec3 gc = leaf * (0.55 + 0.45 * nz) * (0.18 + 0.7 * dif * sh) * ao
                        + mix(vec3(1.0, 0.92, 0.7), pal(0.15), 0.25) * sheen * (0.45 + 0.35 * clamp(treb, 0.0, 1.0)) * buildK * sh;
                col = mix(lac, gc, gold);
            }
            col = mix(col, bg, smoothstep(radius, radius + 3.0, hitT) * 0.6);
        }
    }

    col *= 0.85 + 0.12 * swell;
    col *= (1.0 - smoothstep(0.6, 2.2, sr)) * 0.2 + 0.8;
    fragColor = vec4(grade(col), 1.0);
}
