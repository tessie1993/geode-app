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

// Chrysanthemum Tunnel: flying down the inside of a flower turned inside
// out. The tunnel wall is shingled with petal scales like an artichoke heart
// or a pine cone: rows of scales, each row offset half a column from the
// last, rooted at their far end with their tips pointing back toward the
// viewer, the nearer row lying over the roots of the row beyond. At the far
// end of the tunnel a small flower glows at the vanishing point.
//
// The old style drew the scales as a 2D pattern on one closed-form cylinder.
// Now every scale is a real thin plate, marched: it lies flat on the wall at
// its root and rises toward its tip, so the tips stand off the wall, catch
// the headlamp, and leave a dark crevice (the ambient-occlusion taps) under
// themselves where the next row tucks in. The wall is a shell (between the
// bore radius and one unit beyond), so from a wide glide where the eye leaves
// the bore it still reads as a lit pipe.
//
// ---- camera -------------------------------------------------------------------
//
// camFly: travel down the bore is the rig's. The eye's z is rebased by
// TU_ZWRAP (64). Every z term has a period that divides it: the rows repeat
// every 32 (64 rows of 0.5, hashed by mod(row, 64)), the twist winding and the
// bend have periods 32 and 64, and 64 divides the rig's 8192 travel wrap, so
// neither the rebase nor the wrap ever shows. Catalogue's spin rolls the view
// (uRotation), which down a round tube reads as the tunnel turning.
//
// ---- audio --------------------------------------------------------------------
//
// A section change is hidden inside a BLOOM. On SND_HELD_HARD_CUT the flower at
// the vanishing point opens out until its petals fill the whole view, then
// closes again (chrysBump over the 1.5 s envelope, so it starts from rest).
// The tunnel's palette steps to a new hash of SND_CUT_COUNT exactly when the
// envelope crosses its midpoint - the moment the bloom covers the screen - so
// the old tunnel goes in behind the petals and a new one comes out. The bloom
// is drawn at the scene's own light level; it is a change of shape and hue,
// not a flash. The sub band (sndSpectrum(0.10)) lifts the scale tips off the
// wall - geometry now, each scale by its own share - deepening the crevice
// under each one. Brightness follows SND_LEVEL only.
//
// ---- macros and knobs -----------------------------------------------------------
//
// Complexity: uMorph adds columns of scales round the tunnel (14 up to 22).
// Chaos: uTurbulence + uWarp give the scales uneven lengths and torn edges,
// and uWarp also bends the bore; uTwist winds the rows into a wavy helix.
// Energy: uIntensity scales the section bloom and the sub lift. Colour: pal()
// for the scales, the far flower and the fog. Knobs (offsets from 0.5, so off
// is the tuned look): k0 PULSE the sub lift, k1 TEXTURE the torn edges, k2
// FLOW the headlamp, k3 TONE the hue, k4 BUILD the bloom's reach, k5 SPACE the
// bore radius.
//
// ---- why the distance estimate is safe -------------------------------------------
//
// Outside the band of radii the scales can occupy (gHmax deep inside the
// wall), the field is the plain radial gap, exact. Inside it, each sample
// measures the scale of its own column in three rows (the row it is in, the
// row beyond whose tips reach over it, the row before whose root edge borders
// it). A scale is at most 0.42 of its column wide and at most 1.76 rows long,
// so every scale not measured is at least as far as the column edge (taken
// per row) or the row cap (0.12 world units at the least): both are mixed in
// with min(), which keeps the result a lower bound. The plates rise at most
// 1.5 per unit along the row, the twist and bend shear by ~0.35, and the torn
// edge adds up to ~0.5 per unit of Chaos, so the band's field is divided by
// gLip = 1.9 + 0.5 * chaos, above sqrt(1 + 1.5^2 + 0.35^2). Arcs are measured
// at the sample's own radius, so the across distance is true to within the
// chord (cos 0.22, about 1%). A sphere around the eye is carved out of
// everything, so the eye is never inside a scale. The march starts where the
// ray leaves the inner cylinder no part of the band can reach.

#define TU_MAX_STEPS 110
#define TU_RADIUS 4.0
/** World units per row of scales. */
#define TU_ROW 0.5
/** A scale's length at rest, world units (1.6 rows). */
#define TU_LEN 0.8
/** Half-thickness of a scale plate. */
#define TU_THICK 0.025
/** Thickness of the wall shell behind the scales. */
#define TU_SHELL 1.0
/** Rebase period of the eye's z; divides 8192. */
#define TU_ZWRAP 64.0
#define TU_FAR 44.0
#define TU_BUBBLE 0.7

float gT;
float gChaos;
float gR;
float gCols;
float gWidth;
float gLiftW;
float gHmax;
float gTwistAmp;
float gBend;
float gLip;
vec3 gEye;

/** The bore's centre at depth z: the Warp bend, periods 32 and 64. */
vec2 boreCentre(float z) {
    return gBend * vec2(sin(z * (CHRYS_TAU / 32.0)), cos(z * (CHRYS_TAU / 64.0)));
}

/** The style's world at p: its distance, and in `found` what is there (mat 1 on a scale, 0 on the bare wall; a.x the scale's u, a.y its v, a.z its hash). */
float mapAt(vec3 p, inout ChrysHit found) {
    vec2 q = p.xy - boreCentre(p.z);
    float r = length(q);
    float dWall = max(gR - r, r - gR - TU_SHELL);
    found.mat = 0.0;
    float d = dWall;
    if (r > gR - gHmax) {
        float h = gR - r;
        float s = atan(q.y, q.x) / CHRYS_TAU * gCols + gTwistAmp * sin(p.z * (CHRYS_TAU / 32.0));
        float wy = p.z / TU_ROW;
        float row = floor(wy);
        float colArc = CHRYS_TAU * r / gCols;
        // Rows not measured: the ones beyond start 1.24 rows up at the nearest.
        float capped = (row + 1.24 - wy) * TU_ROW;
        float best = 1e9;
        for (int k = -1; k < 2; k++) {
            float rr = row + float(k);
            float off = 0.5 * mod(rr, 2.0);
            float ci = floor(s - off);
            float cx = ci + 0.5 + off;
            float id = mod(rr, 64.0) * 37.0 + mod(ci, gCols);
            float hh = chrysHash1(id);
            float len = TU_LEN * (1.0 + 0.05 * gChaos * chrysWobble(id, gT));
            // Scale frame: root at the far edge of row rr, pointing back at the eye.
            vec2 lq = vec2((s - cx) * colArc, (rr + 1.0 - wy) * TU_ROW);
            vec4 pet = chrysLeaf(lq, len, gWidth, 0.35, 0.6);
            pet.x += chrysFray(lq * 0.5, gChaos) * 0.75;
            float u = clamp(pet.y, 0.0, 1.0);
            // Flat on the wall at the root, rising toward the tip.
            float hy = 0.06 * u + gLiftW * (0.75 + 0.5 * hh) * u * u;
            float dc = max(pet.x, abs(h - hy) - TU_THICK);
            if (dc < best) {
                best = dc;
                found.a.x = u;
                found.a.y = pet.z;
                found.a.z = hh;
            }
            // The rest of this row lies past the column edge.
            capped = min(capped, (0.5 - abs(s - cx)) * colArc * 0.98 + 0.01);
        }
        float dp = min(best, capped) / gLip;
        if (best < dWall) found.mat = 1.0;
        d = min(dWall, dp);
    } else {
        // Clear of every scale: the gap to the band.
        d = min(dWall, gR - gHmax - r + 0.01);
    }
    return max(d, TU_BUBBLE - length(p - gEye));
}

/** The style's world at p as a hit, for lib_chrys_field. */
ChrysHit chrysMap(vec3 p) {
    ChrysHit h = chrysHit(0.0);
    float d = mapAt(p, h);
    h.d = d;
    return h;
}

//#include lib_chrys_field

void main() {
    ChrysHit fld = chrysHit(0.0);
    vec2 uv = view();

    // The normalization shell (lib_chrys): every variable from its driver, mapped to its range here, once.
    ChrysIn N = chrysIn();
    float liftK = chrysSpan(N.emit, 1.0, 0.5, 1.5);
    float lampK = chrysSpan(N.sway, 1.0, 0.5, 1.5);
    float flareK = chrysSpan(N.glow, 1.0, 0.5, 1.5);
    float hueShift = chrysSpan(N.tone, 0.0, -0.06, 0.06);

    gT = uTime;  // uTime already integrates Speed (ShaderScene::update)
    gChaos = clamp(chrysSpan(N.chaos, 0.0, 0.0, 2.0) * chrysSpan(N.fray, 1.0, 0.5, 1.5), 0.0, 2.0);
    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    float sect = clamp(SND_HELD_HARD_CUT, 0.0, 1.0);
    float lift = clamp(sndSpectrum(0.10) * uIntensity, 0.0, 1.5) * liftK;
    gCols = 14.0 + 2.0 * floor(chrysSpan(N.rings, 0.0, 0.0, 4.0) + 0.5);
    gR = chrysSpan(N.reach, TU_RADIUS, TU_RADIUS - 0.6, TU_RADIUS + 0.6);
    gLiftW = 0.18 * lift;
    gHmax = 0.06 + 1.25 * gLiftW + TU_THICK + 0.03;
    gWidth = 0.30 * (CHRYS_TAU * gR / gCols) / TU_ROW;
    gTwistAmp = chrysSpan(N.curl, 0.0, -1.0, 1.0);
    gBend = chrysSpan(N.bend, 0.0, 0.0, 0.5);
    gLip = 1.9 + 0.5 * gChaos;

    // The palette steps at the bloom's peak, hidden behind its petals.
    float sectionIdx = SND_CUT_COUNT - (sect > 0.5 ? 1.0 : 0.0);
    float hue = chrysHash(vec2(sectionIdx * 0.73, 4.0)) + gT * 0.002 + hueShift;
    float nextHue = chrysHash(vec2(SND_CUT_COUNT * 0.73, 4.0)) + gT * 0.002 + hueShift;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 64, so this shift is exact
    // and invisible, and z keeps its precision far down the travel.
    ro.z = mod(ro.z, TU_ZWRAP);
    gEye = ro;

    // Skip the empty middle: nothing comes nearer the axis than the band's
    // inner radius less the bend.
    float rin = gR - gHmax - 1.42 * gBend - 0.05;
    float t = 0.03;
    float qa = dot(rd.xy, rd.xy);
    float qc = dot(ro.xy, ro.xy) - rin * rin;
    if (rin > 0.5 && qc < 0.0) {
        if (qa < 1e-6) {
            t = TU_FAR;
        } else {
            float qb = dot(ro.xy, rd.xy);
            t = max(t, (-qb + sqrt(max(qb * qb - qa * qc, 0.0))) / qa);
        }
    }

    float hitT = -1.0;
    for (int i = 0; i < TU_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (t > TU_FAR) break;
        fld = chrysField(ro + rd * t);
        float d = fld.d;
        float eps = 0.0012 + 0.0007 * t;
        if (d < eps) {
            hitT = t;
            break;
        }
        t += max(d * 0.9, eps);
    }

    vec3 fogCol = pal(hue + 0.12) * 0.12;
    float ahead = pow(max(rd.z, 0.0), 24.0);
    vec3 col = fogCol;
    float fogAmt = 1.0;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = chrysNormal(p, max(0.0015, 0.0008 * hitT));
        // The crevice under each lifted tip, from two taps along the normal.
        float occ = 0.0;
        for (int k = 0; k < 2; k++) {
            float hk = 0.05 + 0.1 * float(k);
            occ += (hk - chrysField(p + n * hk).d) * (1.0 - 0.4 * float(k));
        }
        float ao = clamp(1.0 - 2.2 * occ, 0.3, 1.0);
        // The hit at p again for the shading, after the normal and AO taps.
        fld = chrysField(p);

        vec3 base;
        if (fld.mat > 0.5) {
            vec3 pc = mix(pal(hue + 0.06 * fld.a.z) * 0.3, mix(pal(hue + 0.08 + 0.06 * fld.a.z), vec3(1.0), 0.2), fld.a.x);
            base = pc * chrysPetalShade(vec4(0.0, fld.a.x, fld.a.y, 0.0), 0.35);
        } else {
            base = pal(hue + 0.5) * 0.04;
        }
        // Headlamp from the eye: the tips tilt toward it as they lift.
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float lamp = 0.35 + 0.85 * ndv * lampK;
        col = base * lamp * ao;
        // The far flower's light rims the scale edges facing it.
        float fres = pow(1.0 - ndv, 3.0);
        col += pal(hue + 0.1) * 0.10 * fres * max(n.z + 0.3, 0.0) * fld.mat;
        col += vec3(0.25) * pow(ndv, 40.0) * fld.mat * fld.a.x * ao;

        fogAmt = 1.0 - exp(-hitT * 0.055);
        col = mix(col, fogCol, fogAmt);
    }
    col += pal(hue + 0.1) * 0.25 * ahead * fogAmt;

    // The flower at the vanishing point, and the bloom that hides the change.
    if (rd.z > 0.05) {
        vec2 vq = rd.xy / rd.z;
        float bump = chrysBump(sect);
        float size = 0.07 + 3.6 * uIntensity * flareK * bump;
        float bh = mix(hue, nextHue, smoothstep(0.55, 0.45, sect));
        vec2 fq = chrysTurn(gT * 0.1) * vq;
        // At rest the flower sits far down the bore, behind the fog; while it
        // blooms it opens in front of everything.
        float cover = smoothstep(0.15, 0.45, bump);
        float front = max(fogAmt, cover);
        // Behind the petals, a disc that only exists during the bloom, so the
        // gaps between petals never show the palette stepping.
        col = mix(col, pal(bh + 0.04) * 0.14, cover * smoothstep(size * 0.8, size * 0.55, length(vq)));
        for (int i = 0; i < 2; i++) {
            float k = float(i);
            float count = 9.0 + 4.0 * k;
            vec4 pet = chrysPetal(chrysTurn(chrysRingOffset(k, count)) * fq, count, size * 0.12, size * (1.0 - 0.3 * k), 0.5, 0.4 / max(size, 0.2), 0.4, 0.5);
            float m = smoothstep(0.006 * (1.0 + size), -0.006 * (1.0 + size), pet.x) * front;
            vec3 pc = mix(pal(bh) * 0.18, pal(bh + 0.08) * 0.42, clamp(pet.y, 0.0, 1.0)) * chrysPetalShade(pet, 0.4);
            col = mix(col, pc, m);
        }
        col = mix(col, pal(bh + 0.55) * 0.45, chrysCore(length(vq), size * 0.1) * front);
    }

    col *= 0.85 + 0.15 * swell;
    col *= 0.75 + 0.25 * smoothstep(2.2, 0.3, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
