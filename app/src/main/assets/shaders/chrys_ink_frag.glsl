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
//#include lib_chrys

// Chrysanthemum Ink: a sumi-e spider chrysanthemum, now a flower you can walk
// round. The head is a real 3D bloom, raymarched: three rings of long quill
// petals (tubes along circular arcs, uneven in length, their tips hooked
// round the axis by chrysHook, as the 2D strokes were), a small domed heart,
// a bowed stem and two grey-wash leaves, all standing on the warm paper. It
// is still painted, not lit: every surface is drawn as ink density, wet and
// dark at a quill's root and breaking into dry-brush streaks toward its tip,
// pooled darker along its silhouette, and thinned toward the paper with
// distance, the way a far stroke is a paler wash. The paper and the red seal
// stay flat on the sheet in screen space; only the flower is in depth. The
// palette appears only twice, as a pale wash in the heart and in the seal.
//
// ---- camera -----------------------------------------------------------------
//
// camAround: the rig orbits the flower at 4 CAM_SPIN_UNITs (whole turns per
// 1024 of travel, so the 8192 wrap never shows) and the dolly pulls in. The
// head is tilted toward the orbit's starting side, so the orbit sweeps from a
// face-on bloom round to its hooked profile and back. Nothing in the scene
// depends on travel, so there is no z period to fit.
//
// ---- audio ------------------------------------------------------------------
//
// The low-mid band (sndSpectrum(0.41)) is how WET the ink is, as before: it
// widens the bleed past every silhouette, a soft wash measured from each ray's
// closest approach to the flower, with a ragged paper-fibre fringe, so a warm,
// full passage soaks the flower and a thin one leaves it crisp. Spectral flux
// (sndLevel(SND_FLUX)) flicks splatter drops off the brush: real ink beads
// hanging in a shell round the head, their places re-seeded by
// SND_ONSET_COUNT so every onset throws a new pattern. The flower turns
// slowly on uDrift. Brightness never follows the music: this is paper, and
// paper is always the same white.
//
// ---- macros and knobs -------------------------------------------------------
//
// Speed: uTime drives the slow sway. Complexity: uMorph adds a fourth ring of
// short quills curled over the heart (dropped on the low-quality path).
// Chaos: uTurbulence + uWarp give every quill its own length and lift, a
// shaggier head. uTwist hooks every quill harder. Energy: uIntensity scales
// the bleed and the splatter. Colour: pal() tints the ink, the heart wash and
// the seal. Space: uZoom via view(). Knobs (offsets from 0.5, so off is the
// tuned look): k0 PULSE the splatter, k1 TEXTURE the dry-brush break, k2 FLOW
// the bleed, k3 TONE the wash's hue, k4 BUILD how far the outer ring opens,
// k5 SPACE the orbit distance.
//
// Budget: a 110-step ceiling broken on uSteps (Detail), and only inside one
// bounding sphere round the flower and its stem; rays that miss it never
// march. Below uSteps 72 the Morph ring and the splatter are dropped.
//
// ---- why the distance estimate is safe ---------------------------------------
//
// Each quill is measured exactly in its own vertical half-plane: the arc is a
// circle, so the in-plane gap is | |x - c| - R |, the gap across is
// rho * sin(angle), and the tube is their length minus its radius (the tip
// end is a sphere at the arc's end point). The hook rotates the plane by
// hook * e^2 (e the radius past the ring's root), which stretches space by at
// most sqrt(1 + (2 hook e rho)^2): each quill's distance is divided by that
// at the point. The ring is folded into one sector, and a neighbour quill may
// be longer than this one, so the ring's step is clamped to the distance to
// the neighbour's plane (one sector over) less the thickest quill radius,
// unless the point is farther than any quill of the ring can reach (its root
// plus the longest length). The quill radius is capped inside the sector, so
// no quill leaves its own plane's neighbourhood. The clamp is for stepping
// only: hits, normals and the bleed read the unclamped distance (gRaw), so
// a clamp never draws. The stem's curve has slope under 1.2 and is
// divided by its gradient; a leaf's 2D profile is a bound and is scaled by
// 0.55 inside a bounding sphere, which stands in for it outside. Splatter
// beads sit in their own 0.4 cells with jitter plus radius under the half
// cell, clamped to the cell walls, and only inside their shell.

#define IK_MAX_STEPS 110
/** The flower head's heart, world space. The rig orbits the origin. */
const vec3 IK_HEAD = vec3(0.15, 0.35, 0.0);
/** The head's axis, tilted 40 degrees toward the orbit's start (-z); IK_SIDE completes the frame. */
const vec3 IK_AXIS = vec3(0.0, 0.76604444, -0.64278761);
const vec3 IK_SIDE = vec3(0.0, 0.64278761, 0.76604444);
/** The march's bounding sphere: the head, its widest chaos, the splatter shell and the stem's visible run. */
const vec3 IK_BOUND_C = vec3(0.15, -0.45, 0.0);
#define IK_BOUND_R 3.3
#define IK_CELL 0.4
#define IK_SPLAT_R 2.8

// Per ring, outer to inner: quill count, root radius, length, elevation
// (radians above the head's plane), arc curvature (negative droops, positive
// curls over the heart), hook (the 2D style's, before the 3D scale), root
// and tip tube radii.
const float IK_COUNT[4] = float[4](22.0, 16.0, 11.0, 9.0);
const float IK_R0[4] = float[4](0.14, 0.12, 0.09, 0.06);
const float IK_LEN[4] = float[4](1.15, 0.85, 0.55, 0.35);
const float IK_ELEV[4] = float[4](0.35, 0.75, 1.2, 1.4);
const float IK_BEND[4] = float[4](-0.75, -0.25, 1.4, 2.2);
const float IK_HOOK[4] = float[4](2.6, -3.5, 0.0, 0.0);
const float IK_ROOT[4] = float[4](0.032, 0.03, 0.026, 0.022);
const float IK_TIP[4] = float[4](0.012, 0.012, 0.011, 0.01);

float gChaos;
float gRings;
float gSpin;
float gHookScale;
float gOpen;
float gSplat;
float gOnset;
bool gLowQ;
// The winner of the last map() call, by its unclamped distance.
float gRaw;
float gMat;
float gU;
float gV;
float gId;
float gEdge;
// Leaf frames, built once per pixel in main: base, across, normal, along.
vec3 gLO[2];
vec3 gLS[2];
vec3 gLN[2];
vec3 gLF[2];

float stemX(float sy) {
    return IK_HEAD.x - 0.14 * sy * sy - 0.05 * sy;
}

vec3 toFlower(vec3 p) {
    vec3 d = p - IK_HEAD;
    vec3 q = vec3(d.x, dot(d, IK_AXIS), dot(d, IK_SIDE));
    q.xz = chrysTurn(gSpin) * q.xz;
    return q;
}

// One ring of quills in the flower frame (axis +y). Returns (distance, u
// along the quill, v across it, quill id) and leaves the sector-edge distance
// in gEdge. The distance is already divided by the hook's stretch.
vec4 quillRing(vec3 q, int ring) {
    float k = float(ring);
    float count = IK_COUNT[ring];
    float r0 = IK_R0[ring];
    float hook = IK_HOOK[ring] * gHookScale;
    vec2 xz = chrysTurn(chrysRingOffset(k, count)) * q.xz;
    float rho = length(xz);
    float e = max(rho - r0, 0.0);
    xz = chrysTurn(hook * e * e) * xz;
    float st = 2.0 * hook * e * rho;
    float lip = sqrt(1.0 + st * st);
    float a = rho > 1e-5 ? atan(xz.y, xz.x) : 0.0;  // atan(0, 0) is undefined in GLSL ES
    float sector = CHRYS_TAU / count;
    float id = floor(a / sector + 0.5);
    float off = a - id * sector;
    float side = rho * sin(off);
    // The nearer neighbour quill lies in the plane one sector over and is no
    // thicker than this ring's root: a lower bound on its distance.
    gEdge = (rho * sin(min(sector - abs(off), 1.5)) - IK_ROOT[ring]) / lip;

    float pid = mod(id, count);
    float l = IK_LEN[ring] * (0.62 + 0.55 * chrysHash1(pid + 13.0 * k) * (1.0 + 0.6 * gChaos));
    float el = IK_ELEV[ring] + 0.25 * gChaos * (chrysHash1(pid * 7.0 + k + 1.0) - 0.5);
    float kb = IK_BEND[ring];
    if (ring == 0) {
        el -= 0.3 * gOpen;
        kb -= 0.35 * gOpen;
    }
    // The centreline: a circle of radius 1/kb leaving (r0, 0) at angle el.
    kb = abs(kb) < 0.05 ? (kb < 0.0 ? -0.05 : 0.05) : kb;
    float rad = 1.0 / kb;
    vec2 x = vec2(rho * cos(off), q.y);
    vec2 p0 = vec2(r0, 0.0);
    vec2 c = p0 + rad * vec2(-sin(el), cos(el));
    vec2 v0 = p0 - c;
    vec2 vx = x - c;
    float th = atan(v0.x * vx.y - v0.y * vx.x, dot(v0, vx));
    float u = (th / kb) / l;
    // Capped inside the sector, so no quill crosses its edge plane.
    float cap = 0.45 * sector * max(rho, 0.02);
    float r = min(mix(IK_ROOT[ring], IK_TIP[ring], clamp(u, 0.0, 1.0)), cap);
    float d;
    if (u > 1.0) {
        vec2 p1 = c + chrysTurn(kb * l) * v0;
        d = length(vec3(x - p1, side)) - min(IK_TIP[ring], cap);
    } else {
        float dn = abs(length(vx) - abs(rad));
        d = max(length(vec2(dn, side)) - r, -u * l);
    }
    return vec4(d / lip, u, side / max(r, 1e-4), pid);
}

float map(vec3 p) {
    vec3 q = toFlower(p);
    // The heart: a low dome of florets.
    float d = (length(vec3(q.x, q.y * 1.8, q.z)) - 0.15) / 1.8;
    gRaw = d;
    gMat = 0.0;
    gU = 0.0;
    gV = 0.0;
    gId = 0.0;
    float rho = length(q.xz);
    for (int i = 0; i < 4; i++) {
        float k = float(i);
        if (k >= gRings) break;
        float lmax = IK_LEN[i] * (0.62 + 0.55 * (1.0 + 0.6 * gChaos));
        // No quill of this ring reaches farther than its root plus the longest length.
        float envD = length(vec2(rho - IK_R0[i], q.y)) - lmax - IK_ROOT[i];
        if (envD >= gRaw) continue;
        vec4 pr = quillRing(q, i);
        d = min(d, min(pr.x, max(gEdge, envD)));
        if (pr.x < gRaw) {
            gRaw = pr.x;
            gMat = 1.0 + k;
            gU = pr.y;
            gV = pr.z;
            gId = pr.w;
        }
    }

    // The stem: a bowed tube from the heart down off the sheet.
    float sy = IK_HEAD.y - p.y;
    float ds;
    if (sy > 0.0) {
        float slope = 0.28 * sy + 0.05;
        float w = 0.03 + 0.006 * sy;
        ds = (length(vec2(p.x - stemX(sy), p.z - IK_HEAD.z)) - w) / sqrt(1.0 + slope * slope);
    } else {
        ds = length(p - IK_HEAD) - 0.03;
    }
    d = min(d, ds);
    if (ds < gRaw) {
        gRaw = ds;
        gMat = 5.0;
        gU = clamp(sy * 0.3, 0.0, 1.0);
        gV = 0.0;
    }

    // Two grey-wash leaves off the stem, each a thin drooping blade.
    for (int i = 0; i < 2; i++) {
        float len = 0.8 - 0.12 * float(i);
        vec3 lc = gLO[i] + gLF[i] * (0.5 * len);
        float bs = length(p - lc) - (0.55 * len + 0.15);
        if (bs > 0.0) {
            d = min(d, bs);
            continue;
        }
        vec3 o = p - gLO[i];
        vec3 l = vec3(dot(o, gLS[i]), dot(o, gLN[i]), dot(o, gLF[i]));
        l.y += 0.3 * l.z * l.z;
        vec4 leaf = chrysLeaf(l.xz, len, 0.5, 0.15, 0.8);
        vec2 w2 = vec2(leaf.x, abs(l.y) - 0.008);
        float dl = (length(max(w2, 0.0)) + min(max(w2.x, w2.y), 0.0)) * 0.55;
        d = min(d, dl);
        if (dl < gRaw) {
            gRaw = dl;
            gMat = 6.0;
            gU = leaf.y;
            gV = leaf.z;
            gId = float(i);
        }
    }

    // Splatter: ink beads in a shell round the head, re-seeded on every onset.
    if (gSplat > 0.02) {
        vec3 hp = p - IK_HEAD;
        float rq = length(hp);
        if (rq < IK_SPLAT_R) {
            vec3 sp = hp / IK_CELL;
            vec3 cc = floor(sp);
            float sr = length(cc + 0.5) * IK_CELL;
            float seed = chrysHash(cc.xy + cc.z * vec2(7.13, 3.71) + gOnset * vec2(0.37, 0.71));
            vec3 jit = (vec3(seed, fract(seed * 13.7), fract(seed * 71.3)) - 0.5) * 0.3;
            float rr = min(0.16 * gSplat * step(0.72, seed) * smoothstep(2.4, 1.6, sr) * smoothstep(1.0, 1.4, sr), 0.3);
            vec3 f = abs(fract(sp) - 0.5);
            float toEdge = (0.5 - max(max(f.x, f.y), f.z)) * IK_CELL;
            float raw = rr > 0.001 ? (length(sp - (cc + 0.5 + jit)) - rr) * IK_CELL : 1e9;
            d = min(d, min(raw, toEdge + 0.02));
            if (raw < gRaw) {
                gRaw = raw;
                gMat = 7.0;
                gU = 0.0;
                gV = 0.0;
            }
        } else {
            d = min(d, rq - IK_SPLAT_R + 0.01);
        }
    }
    return d;
}

float rawAt(vec3 p) {
    map(p);
    return gRaw;
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * rawAt(p + k.xyy * eps) + k.yyx * rawAt(p + k.yyx * eps)
                   + k.yxy * rawAt(p + k.yxy * eps) + k.xxx * rawAt(p + k.xxx * eps));
}

void main() {
    vec2 uv = view();
    // The sheet itself is flat: paper and seal in unwarped screen space.
    vec2 suv = (vUv * 2.0 - 1.0) * vec2(uResolution.x / uResolution.y, 1.0);
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)

    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kFlow = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    float kSpace = clamp(uKnob[5], 0.0, 1.0) - 0.5;

    gLowQ = uSteps < 72.0;
    gChaos = clamp(uTurbulence + uWarp, 0.0, 2.0);
    gRings = gLowQ ? 3.0 : 3.0 + step(0.5, uMorph);
    // The 2D hooks were tuned on strokes half as long; scaled for world units.
    gHookScale = 0.23 * clamp(1.0 + uTwist, 0.0, 3.0);
    gOpen = 2.0 * kBuild;
    float lowmid = clamp(sndSpectrum(0.41) * uIntensity, 0.0, 1.5);
    float flux = clamp(sndLevel(SND_FLUX) * uIntensity, 0.0, 1.5);
    gSplat = gLowQ ? 0.0 : flux * (1.0 + 1.2 * kPulse);
    gOnset = mod(floor(SND_ONSET_COUNT), 64.0);
    gSpin = uDrift * 0.08 + 0.15 * sin(t * 0.03);
    float bleed = (0.004 + 0.05 * lowmid) * (1.0 + 1.2 * kFlow);
    float dryAmt = clamp(1.0 + 1.6 * kTexture, 0.0, 2.0);
    float hue = 0.1 + 0.02 * sin(t * 0.02) + 0.15 * kTone;

    for (int i = 0; i < 2; i++) {
        float sy = 1.1 + 0.65 * float(i);
        gLO[i] = vec3(stemX(sy), IK_HEAD.y - sy, IK_HEAD.z);
        vec3 f = i == 0 ? normalize(vec3(-1.0, 0.45, 0.3)) : normalize(vec3(1.0, 0.35, -0.35));
        vec3 s = normalize(cross(f, vec3(0.0, 1.0, 0.0)));
        gLF[i] = f;
        gLS[i] = s;
        gLN[i] = cross(s, f);
    }

    vec3 ro;
    vec3 rd;
    camAround(uv, 3.8 * (1.0 - 0.3 * kSpace), 4.0 * CAM_SPIN_UNIT, ro, rd);

    // Paper: warm, with fibre and slow blotches.
    vec3 paper = vec3(0.93, 0.89, 0.81);
    paper *= 0.96 + 0.04 * motionFbm(suv * 3.0) + 0.03 * (motionNoise(suv * vec2(180.0, 25.0)) - 0.5);
    paper *= 1.0 - 0.18 * smoothstep(0.8, 2.0, length(suv));

    float ink = 0.0;
    float heart = 0.0;
    vec3 oc = ro - IK_BOUND_C;
    float b = dot(oc, rd);
    float h = b * b - (dot(oc, oc) - IK_BOUND_R * IK_BOUND_R);
    if (h > 0.0) {
        float sq = sqrt(h);
        float tm = max(-b - sq, 0.01);
        float tEnd = -b + sq;
        float hitT = -1.0;
        float used = 0.0;
        float dmin = 1e9;
        float minMat = 0.0;
        for (int i = 0; i < IK_MAX_STEPS; i++) {
            // uSteps is the user's Detail; this loop's own bound is the constant.
            if (float(i) >= uSteps) break;
            if (tm > tEnd) break;
            used = float(i);
            float d = map(ro + rd * tm);
            float eps = 0.0007 * tm + 0.0008;
            if (gRaw < dmin) {
                dmin = gRaw;
                minMat = gMat;
            }
            if (gRaw < eps) {
                hitT = tm;
                break;
            }
            tm += max(d * 0.85, eps * 1.2);
        }

        if (hitT > 0.0) {
            vec3 p = ro + rd * hitT;
            vec3 n = normalAt(p, 0.0012 * hitT + 0.0006);
            // Restore the hit's globals after the normal taps.
            map(p);
            float mat = gMat;
            float u = clamp(gU, 0.0, 1.0);
            vec3 nf = dot(n, rd) > 0.0 ? -n : n;
            vec3 L = normalize(vec3(0.4, 0.8, -0.45));
            float lit = max(dot(nf, L), 0.0);
            // Wet ink pools along the edge a stroke turns away on.
            float rim = pow(1.0 - abs(dot(n, rd)), 2.0);
            float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.1, 0.3, 1.0);
            if (mat < 0.5) {
                vec3 q = toFlower(p);
                float speck = 0.5 + 0.5 * sin(q.x * 60.0) * sin(q.z * 60.0);
                ink = 0.5 * (0.55 + 0.45 * (1.0 - lit)) * (0.8 + 0.3 * speck);
                heart = 1.0;
            } else if (mat < 4.5) {
                float ring = mat - 1.0;
                // Dry brush: the stroke breaks into streaks toward the tip.
                float dry = motionNoise(vec2(gV * 3.5 + gId * 7.0 + ring * 3.0, u * 2.0));
                float brk = smoothstep(0.45, 1.0, u) * min(dryAmt, 1.0);
                float tone = mix(0.95, 0.5, u) * mix(1.0, smoothstep(0.25 * dryAmt, 0.6, dry), brk);
                ink = tone * (0.62 + 0.38 * (1.0 - lit)) + 0.3 * rim * tone;
                heart = step(1.5, ring) * smoothstep(0.35, 0.0, u) * 0.7;
            } else if (mat < 5.5) {
                float dry = motionNoise(vec2(p.x * 40.0, p.y * 2.0));
                ink = 0.8 * mix(1.0, smoothstep(0.2, 0.55, dry), 0.35 * u) * (0.75 + 0.25 * (1.0 - lit)) + 0.2 * rim;
            } else if (mat < 6.5) {
                float vein = smoothstep(0.08, 0.0, abs(gV)) * step(u, 0.95);
                float tone = 0.32 + 0.25 * u + 0.3 * vein;
                ink = tone * (0.7 + 0.3 * (1.0 - lit)) + 0.25 * rim;
            } else {
                ink = 0.9;
            }
            // Strokes deep behind others carry more layers of wash.
            ink = clamp(ink + 0.25 * (1.0 - ao), 0.0, 1.0);
            // A far stroke is a paler wash.
            float fade = exp(-max(hitT - 2.8, 0.0) * 0.3);
            ink *= fade;
            heart *= fade;
        } else {
            // The bleed: wet ink spread past the silhouette the ray passed
            // closest to, with a ragged paper-fibre fringe.
            float w = bleed * (0.6 + 0.8 * motionNoise(suv * 70.0));
            float tone = minMat > 5.5 && minMat < 6.5 ? 0.4 : 0.75;
            ink = tone * exp(-max(dmin, 0.0) / max(w, 1e-4));
        }
    }

    vec3 inkCol = mix(vec3(0.05, 0.055, 0.07), pal(hue + 0.6) * 0.15, 0.2);
    vec3 col = mix(paper, inkCol, ink);

    // The colour: a pale wash in the heart of the flower...
    col = mix(col, col * mix(vec3(1.0), pal(hue) * 1.2, 0.55), heart);
    // ...and the seal, stamped on the sheet.
    vec2 sealC = vec2(-0.55 * uResolution.x / uResolution.y + 0.12, -0.72);
    vec2 sq2 = (suv - sealC) / 0.075;
    float box = max(abs(sq2.x), abs(sq2.y));
    float carve = step(0.45, chrysHash(floor(sq2 * 3.0 + 7.0))) * step(box, 0.78);
    float sealInk = smoothstep(1.0, 0.97, box) * (1.0 - carve * 0.85) * (0.85 + 0.15 * motionNoise(sq2 * 12.0));
    vec3 sealCol = mix(vec3(0.72, 0.12, 0.08), pal(hue + 0.95), 0.25);
    col = mix(col, sealCol, sealInk);

    fragColor = vec4(grade(col), 1.0);
}
