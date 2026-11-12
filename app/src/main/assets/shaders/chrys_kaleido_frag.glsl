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

// Chrysanthemum Kaleido: a flight down the inside of a kaleidoscope full of
// flowers. The tube is a regular N-gon prism of mirror glass around the z
// axis (N = Symmetry, 6 by default), and the space inside it is folded into
// N angular wedges, so one wedge's bouquet is mirrored into every other one:
// the old flat rosette, now with depth. Each 4-unit cell along z may hold one
// whole flower - its own petal count, cup, tip and colour, two offset rings
// of thin cupped petals around a domed heart - and, as in the flat style,
// the flowers stream outward: each one is born small on the tube's axis,
// opens as it drifts toward the mirrors and shrinks away before its life
// wraps, so none ever pops. The faces lean toward the eye, so the flight
// meets them face-on and the far end of the tube, the toy's light, glows
// through their backs. The mirror faces are dark silvered glass that reflect
// the flowers once (one bounded bounce), and thin bright seams mark the
// tube's corners, where two mirrors meet, as on the real toy.
//
// The camera is the shared rig (camFly): the eye rides the rig's path down
// +z, and the rig's wander off the axis is what shows the folds in
// perspective. The mirrors turn with time (an isometry), never with z, so the
// only z terms are the flower cells (period 4, hashed by index mod 16) and
// the seam glint, which is placed relative to the eye; the eye is rebased by
// 64, which divides the rig's 8192 travel wrap, so both are invisible.
//
// ---- audio ------------------------------------------------------------------
//
// The beat (SND_KICK_ENV) SWINGS THE MIRRORS: the fold twists away and back
// across each beat's envelope (chrysBump, from rest), more toward the walls
// than at the axis, so on a steady groove the whole bouquet rocks in time
// while the flowers keep streaming (k0 PULSE scales the swing). The bar
// oscillator (uBarOsc, phase-locked and confidence-gated) breathes the
// kaleidoscope's magnification over each bar: it moves the lens's focal
// length, not the world. The high-mid band (sndLevel(SND_MID_HIGH_ATT))
// slides the seams' glint down the mirrors. The stream rides uFlowPhase
// (loudness sets its speed, never its direction). Brightness follows
// SND_INTENSITY only.
//
// ---- macros and knobs -------------------------------------------------------
//
// Complexity: uMorph fills more of the cells with flowers (five in nine at 0,
// every cell at 1, as the flat style went from five flowers to nine). Chaos:
// uTurbulence + uWarp give the petals uneven lengths and ragged edges;
// uTwist curls every flower. Energy: uIntensity scales the mirror swing and
// the bar breathing. Colour: pal() for every flower, the glass and the light.
// Space: uZoom via view(); Symmetry is the mirror count. Knobs (offsets from
// 0.5): k0 PULSE swing depth, k1 TEXTURE petal width, k2 FLOW petal flutter,
// k3 TONE hue, k4 BUILD the hearts' glow, k5 SPACE tube width.
//
// ---- why the distance estimate is safe --------------------------------------
//
// The angular fold is a piecewise isometry and the mirror count an integer,
// so the folded tube wall (a plane at the apothem) is exact. The time spin is
// an isometry; the beat swing turns xy by an angle that grows with radius,
// whose Lipschitz bound is sqrt(1 + (r * dangle/dr)^2) <= sqrt(1 + (1.42 *
// swing)^2) inside the tube (r up to the corner, apothem * sqrt 2 for N >= 4),
// computed per frame as gLip. A flower's petals are lib_chrys's 2D ring bound
// extruded into a thin slab whose height bends with rho^2 (the cup); the cup's
// slope, the curl's shear and the ragged edge are each bounded and their
// product divides the flower term (gFLip). Each cell measures only its own
// flower, clamped to the cell boundary plus the gap no flower can cross. A
// flower that straddles a mirror line near the axis is measured only in its
// own wedge, so its image across the line can be nearer than the estimate;
// the 0.8 step factor and the small size of flowers near the axis cover it.
// A sphere around the eye is carved out of everything, so the eye is never
// inside a petal or a wall.

#define CK_MAX_STEPS 96
#define CK_REFL_STEPS 40
#define CK_FAR 34.0
#define CK_REFL_FAR 16.0
/** The eye's z rebase: a multiple of the cell, dividing 8192. */
#define CK_ZWRAP 64.0
/** Flower cell length along z (divides CK_ZWRAP). */
#define CK_CELL 4.0
/** The largest flower's reach from its cell centre along z, jitter included. */
#define CK_REACH 1.7
/** Base apothem of the mirror tube. */
#define CK_R 4.4
#define CK_BUBBLE 0.6

float gSeg;
float gR;
float gSpin;
float gSwing;
float gLip;
float gFLip;
float gChaos;
float gCurl;
float gShare;
float gWidth;
float gFlutter;
/** k4 BUILD: the hearts' glow. */
float gBuild;
vec3 gEye;

/** PCG integer hash: exact on every device, unlike a sin() hash. */
uint ckHash(uint h) {
    h = h * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    return (h >> 22u) ^ h;
}

float ckUnit(uint h) { return float(h & 0xffffu) * (1.0 / 65536.0); }

/**
 * Spin, swing and fold `p` into one wedge. x runs along the wedge's centre
 * line (the mirror face's normal), y across it from that line (0 .. apothem *
 * tan(seg / 2) at the corner), z is the untouched depth.
 */
vec3 ckFold(vec3 p) {
    float r = length(p.xy);
    // atan(0, 0) is undefined in GLSL ES; the fold is continuous at r = 0 anyway.
    float a = r > 1e-5 ? atan(p.y, p.x) : 0.0;
    a += gSpin + gSwing * (0.4 + r / gR);
    a = abs(mod(a, gSeg) - 0.5 * gSeg);
    return vec3(r * cos(a), r * sin(a), p.z);
}

/**
 * One ring of petals in the flower's frame `s` (face in xy, facing -z). The
 * slab's mid-surface rises toward -z with rho^2 (`bowl` per unit of size), so
 * the ring is a cup. Returns (distance, u, v, petal id).
 */
vec4 ckRing(vec3 s, float size, float count, float r0, float lenK, float bowl, float lift0, float cup, float tip, float seed) {
    float rho = length(s.xy);
    float sector = CHRYS_TAU / count;
    float a = rho > 1e-5 ? atan(s.y, s.x) : 0.0;
    float pid = floor((a - gCurl * (rho - r0)) / sector + 0.5);
    float len = size * lenK * (1.0 + 0.2 * gChaos * chrysWobble(mod(pid, count) + seed * 11.0, uTime));
    vec4 pet = chrysPetal(s.xy, count, r0, len, gWidth, gCurl, cup, tip);
    float u = clamp(pet.y, 0.0, 1.0);
    // Ragged edge (Chaos) and flutter (k2): both small, both bounded in gFLip.
    pet.x += 0.012 * gChaos * size * sin(pet.y * 23.0 + pid * 1.7);
    float flut = gFlutter * size * 0.06 * sin(uTime * 2.3 + pid * 2.1 + seed * 6.0) * u;
    float mid = s.z + lift0 + bowl * rho * rho / max(size, 0.05) + flut;
    float thick = size * 0.03 * (1.0 - 0.5 * u) + 0.004;
    pet.x = max(pet.x, abs(mid) - thick);
    return pet;
}

/** The style's world at p: its distance, and in `found` what is there (mat 0 glass, 1 outer petals, 2 inner petals, 3 heart; a.x the flower's hue hash, a.y its cup, b the petal frame (d, u, v, id)). */
float mapAt(vec3 p, inout ChrysHit found) {
    vec3 q = ckFold(p);
    float wall = gR - q.x;

    float cz = floor(q.z / CK_CELL);
    float lz = q.z - (cz + 0.5) * CK_CELL;
    uint h = ckHash(uint(mod(cz, CK_ZWRAP / CK_CELL)) + 0x2c1u);
    float h1 = ckUnit(h);
    float h2 = ckUnit(ckHash(h));
    float h3 = ckUnit(ckHash(h ^ 0x9e37u));
    // A neighbour cell's flower is at least the boundary distance plus the gap away.
    float fl = 0.5 * CK_CELL - abs(lz) + (0.5 * CK_CELL - CK_REACH);
    float mat = 0.0;
    vec4 pet = vec4(0.0);
    float cup = h2;

    if (h3 < gShare) {
        // Stream outward from the axis; grow, then shrink away before the wrap.
        float life = fract(h1 + uFlowPhase * 0.04 * (0.6 + 0.6 * h2));
        float size = (0.42 + 0.34 * h1) * sin(CHRYS_PI * life);
        float rad = 0.3 + (gR - 1.2) * life;
        size = min(size, 0.12 + 0.45 * rad);
        if (size > 0.02) {
            float ang = 0.5 * gSeg * (0.15 + 0.7 * h2);
            vec3 s = vec3(q.x - rad * cos(ang), q.y - rad * sin(ang), lz - (h3 / max(gShare, 0.01) - 0.5) * 1.2);
            // The face leans toward the axis, then the flower turns on its own stem.
            s.xz = chrysTurn(0.45) * s.xz;
            s.xy = chrysTurn(uTime * (h1 - 0.5) * 0.6 + h1 * 6.0) * s.xy;
            float count = 5.0 + floor(h1 * 9.0);
            float tip = fract(h1 * 7.3);
            vec4 outer = ckRing(s, size, count, size * 0.2, 0.8, 0.3 * cup, 0.0, cup, tip, h1);
            vec3 si = vec3(chrysTurn(chrysRingOffset(1.0, count)) * s.xy, s.z);
            vec4 inner = ckRing(si, size, count + 3.0, size * 0.12, 0.5, 0.7 * cup + 0.2, -size * 0.04, min(cup + 0.3, 1.0), tip, h1 + 5.0);
            float heart = (length(vec3(s.xy, s.z * 1.5)) - size * 0.2) / 1.5;
            float f = outer.x;
            mat = 1.0;
            pet = outer;
            if (inner.x < f) { f = inner.x; mat = 2.0; pet = inner; }
            if (heart < f) { f = heart; mat = 3.0; }
            fl = min(fl, f / gFLip);
            if (fl < f / gFLip - 1e-6) mat = 0.0;
        }
    }

    float d = wall;
    if (fl < wall) d = fl; else mat = 0.0;
    found.mat = mat;
    found.a.x = h1;
    found.a.y = cup;
    found.b = pet;
    return max(d / gLip, CK_BUBBLE - length(p - gEye));
}

/** The style's world at p as a hit, for lib_chrys_field. */
ChrysHit chrysMap(vec3 p) {
    ChrysHit h = chrysHit(0.0);
    float d = mapAt(p, h);
    h.d = d;
    return h;
}

//#include lib_chrys_field

/** The light down the tube: the far end glows, the rest is jewel dark. */
vec3 ckFog(vec3 rd, float hue, float swell) {
    float ahead = pow(max(rd.z, 0.0), 6.0);
    return pal(hue + 0.15) * (0.03 + (0.28 + 0.2 * swell) * ahead) + vec3(0.004, 0.003, 0.007);
}

/** A flower's petals or heart at `p` (`hit` the field there), lit by the eye and by the tube's far end. */
vec3 ckFlowerShade(vec3 p, vec3 n, vec3 rd, float hue, float ao, ChrysHit hit) {
    float fh = hue + 0.6 * hit.a.x;
    vec3 nf = dot(n, rd) > 0.0 ? -n : n;
    vec3 endL = vec3(0.0, 0.0, 1.0);
    float diff = max(dot(nf, -rd), 0.0);
    float through = max(dot(-nf, endL), 0.0);
    float spec = pow(max(dot(reflect(rd, nf), -rd), 0.0), 24.0);
    if (hit.mat > 2.5) {
        // A contrasting heart, glowing with k4 BUILD.
        float build = gBuild;
        vec3 hc = pal(fh + 0.5) * 0.8;
        return hc * (0.3 + 0.6 * diff) * ao + hc * 0.35 * build + vec3(spec) * 0.15;
    }
    float u = clamp(hit.b.y, 0.0, 1.0);
    vec3 pc = mix(pal(fh) * 0.45, mix(pal(fh + 0.07), vec3(1.0), 0.2), u);
    if (hit.mat > 1.5) pc = mix(pc, pal(fh + 0.04), 0.3);
    pc *= chrysPetalShade(hit.b, hit.a.y);
    return pc * (0.22 + 0.8 * diff) * ao + pc * through * 0.55 + vec3(spec) * 0.12 * ao;
}

void main() {
    ChrysHit fld = chrysHit(0.0);
    vec2 uv = view();
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)
    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float beat = clamp(SND_KICK_ENV, 0.0, 1.0);
    float mirrors = clamp(floor(uSymmetry + 0.5), 3.0, 12.0);

    gSeg = CHRYS_TAU / mirrors;
    // The normalization shell (lib_chrys): every variable from its driver, mapped to its range here, once.
    ChrysIn N = chrysIn();
    gR = max(3.4, chrysSpan(N.reach, CK_R, CK_R - 1.2, CK_R + 1.2));
    gSpin = t * 0.02;
    gSwing = 0.3 * clamp(uIntensity, 0.0, 2.0) * chrysSpan(N.emit, 1.0, 0.3, 1.7) * chrysBump(beat);
    // The corner radius is apothem / cos(pi / N); r / gR there is at most 2 (N = 3).
    float rr = mirrors < 3.5 ? 2.0 : 1.4142136;
    gLip = sqrt(1.0 + rr * rr * gSwing * gSwing);
    gChaos = chrysSpan(N.chaos, 0.0, 0.0, 2.0);
    gCurl = chrysSpan(N.curl, 0.0, -4.0, 4.0);
    gShare = (5.0 + chrysSpan(N.rings, 0.0, 0.0, 4.0)) / 9.0;
    gWidth = chrysSpan(N.width, 0.5, 0.35, 0.65);
    gFlutter = chrysSpan(N.sway, 0.5, 0.0, 1.0);
    gBuild = chrysSpan(N.glow, 0.5, 0.0, 1.0);
    // Cup slope (<= 1.4 * bowl at the inner ring), curl shear (curl * reach),
    // ragged edge and flutter (~0.35 chaos, ~0.1): bounds multiplied.
    gFLip = 1.45 * (1.0 + abs(gCurl) * 0.95) * (1.0 + 0.4 * gChaos) * (1.0 + 0.1 * gFlutter);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // The bar breathes the lens: a longer focal length magnifies, the world stays put.
    float zoom = 1.0 + 0.08 * clamp(uIntensity, 0.0, 2.0) * (uBarOsc - 0.5);
    rd = normalize(uCamBasis * vec3(uv, uCamFocal * zoom));
    // The flower cells hash by index mod 16 (64 units), so this rebase is exact.
    ro.z = mod(ro.z, CK_ZWRAP);
    gEye = ro;

    float hue = 0.3 + t * 0.003 + chrysSpan(N.tone, 0.0, -0.15, 0.15);

    float tt = 0.03;
    float hitT = -1.0;
    float used = 0.0;
    for (int i = 0; i < CK_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (tt > CK_FAR) break;
        used = float(i);
        fld = chrysField(ro + rd * tt);
        float d = fld.d;
        float eps = 0.0008 * tt + 0.0005;
        if (d < eps) {
            hitT = tt;
            break;
        }
        tt += max(d * 0.8, eps);
    }

    float fogK = 0.055;
    vec3 fogCol = ckFog(rd, hue, swell);
    vec3 col = fogCol;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = chrysNormal(p, max(0.0015, 0.0012 * hitT));
        fld = chrysField(p);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.2, 0.25, 1.0);
        vec3 surf;
        if (fld.mat > 0.5) {
            surf = ckFlowerShade(p, n, rd, hue, ao, fld);
        } else {
            vec3 q = ckFold(p);
            float ndv = clamp(dot(n, -rd), 0.0, 1.0);
            float fres = pow(1.0 - ndv, 3.0);
            float halfW = gR * tan(0.5 * gSeg);
            // Ground: a deep jewel gradient across the face, as the flat style's wedge.
            float across = clamp(q.y / max(halfW, 0.01), 0.0, 1.0);
            surf = mix(pal(hue + 0.5) * 0.04, pal(hue + 0.62) * 0.09, across);
            surf *= 0.8 + 0.2 * cos(across * 3.0);
            surf += fogCol * fres * 1.2;

            // One bounce: the silvered glass shows the bouquet again. Skipped on
            // the low Detail path, bounded by its own constant and by Detail.
            if (uSteps >= 56.0) {
                vec3 rr2 = reflect(rd, n);
                vec3 ro2 = p + n * 0.02;
                float t2 = 0.02;
                float hit2 = -1.0;
                float cap = uSteps * 0.4;
                for (int i = 0; i < CK_REFL_STEPS; i++) {
                    if (float(i) >= cap) break;
                    if (t2 > CK_REFL_FAR) break;
                    fld = chrysField(ro2 + rr2 * t2);
                    float d = fld.d;
                    float eps = 0.0015 * t2 + 0.001;
                    if (d < eps) {
                        hit2 = t2;
                        break;
                    }
                    t2 += max(d * 0.8, eps);
                }
                vec3 refl = ckFog(rr2, hue, swell);
                if (hit2 > 0.0) {
                    vec3 p2 = ro2 + rr2 * hit2;
                    fld = chrysField(p2);
                    if (fld.mat > 0.5) {
                        // Cheap: no second normal; the flower is lit as if face-on.
                        refl = mix(ckFlowerShade(p2, -rr2, rr2, hue, 0.8, fld), refl, 1.0 - exp(-(hit2 + hitT) * fogK));
                    } else {
                        refl = mix(pal(hue + 0.55) * 0.05, refl, 1.0 - exp(-(hit2 + hitT) * fogK));
                    }
                }
                surf += refl * (0.45 + 0.4 * fres);
            }

            // Mirror seams: a thin line along each corner, its glint sliding down the tube.
            float px = hitT * 2.0 / (max(uCamFocal, 0.1) * uResolution.y);
            float seam = 1.0 - smoothstep(0.0, 0.03 + 1.5 * px, abs(halfW - q.y));
            float glintAt = fract(t * 0.07 + 0.8 * clamp(sndLevel(SND_MID_HIGH_ATT), 0.0, 1.0));
            float zg = gEye.z + 2.0 + 20.0 * glintAt;
            float glint = exp(-pow((p.z - zg) * 0.6, 2.0));
            surf += pal(hue + 0.15) * seam * (0.12 + 0.9 * glint);
        }
        col = mix(surf, fogCol, 1.0 - exp(-hitT * fogK));
    }

    col *= 0.85 + 0.15 * swell;
    col *= 0.75 + 0.25 * (1.0 - smoothstep(0.5, 2.0, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
