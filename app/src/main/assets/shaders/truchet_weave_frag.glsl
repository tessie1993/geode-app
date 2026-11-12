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
//#include lib_truchet

// Truchet Weave: a hall between two woven cloths, flown through. The floor and
// the ceiling are each the flat style's two Truchet tilings, laid one over the
// other and woven - only now every ribbon is a real tape with thickness that
// rises over and dips under the ribbons it crosses. The ceiling is the floor
// mirrored, read from another stretch of the tiling, so it never repeats the
// floor above it. Behind each weave hangs a dark backing cloth, cross-hatched
// faintly by the two layers' bands, as the flat style's cloth was.
//
// THE WEAVE. Layer A tiles on the plain grid; layer B is shifted half a cell
// sideways, which (as in the flat style) puts every crossing of two ribbons
// INSIDE one cell of A and makes B's arcs meet A's cell borders only at A's
// corners, while A's arcs meet them only at its edge midpoints. So one smooth
// lift, W = sin(pi x) sin(pi z) in cell units, does the whole weave: it is zero
// on every A cell border (where both layers' ribbons sit at the cloth's middle)
// and its sign is A's checkerboard parity, so A rides at +lift * W and B at
// -lift * W. Every crossing is at |W| = 0.69 by symmetry (circles of radius 0.5
// about corners half a cell apart), so the two tapes pass 0.25+ apart there and
// each ribbon goes alternately over and under as it runs through the grid - the
// flat style's parity rule, now as geometry.
//
// THE CAMERA. The eye rides the rig's path (uCamPos x and z, height held near
// the hall's middle so it never brushes a cloth) and looks along the rig's
// view, so Space - path, look, shake, FOV - all apply. The cell is one world
// unit and the tile ids wrap at 64 cells along z (even, so the parity that
// sets the flow direction survives); the lift has period 2, and the Chaos bend
// and the hue field use only whole multiples of 2 pi / 64 in z. So the eye is
// rebased mod 64 every frame, exactly and invisibly, and 64 divides the rig's
// 8192 travel wrap.
//
// ---- audio ------------------------------------------------------------------
//
// Onsets re-roll a scattered quarter of each layer's tiles, the two layers on
// different cells, so the cloth re-weaves itself patch by patch, a quarter turn
// at a time. The kick (SND_KICK_ENV) widens the tapes for a moment - the weave
// tightens on the drum. The bass (sndShaped(SND_LOW)) breathes the lift: the
// ribbons climb higher over each other and the cloth billows. The snare
// (SND_SNARE_ENV) flares the glints that run along every ribbon in its path's
// own direction; the highs (sndShaped(SND_HIGH)) brighten the tapes' sheen.
// The key leans the hue. Brightness follows SND_INTENSITY only.
//
// ---- macros and knobs -------------------------------------------------------
//
// Speed: uTime already integrates it. Complexity: uMorph thins the tapes and
// adds threads. Chaos: uWarp/uTurbulence bend the plan before it is tiled (a
// z-periodic copy of truchetWarp), so the paths meander but stay joined;
// uTwist in view(). Energy: uIntensity scales the kick and the bass. Colour:
// warp and weft are two pal() tones half the wheel apart. Space: the rig.
// Knobs: k0 PULSE the kick widening, k1 TEXTURE the thread count, k2 FLOW the
// glint speed, k3 TONE a hue offset, k4 BUILD the glint brightness, k5 SPACE
// the hall's height.
//
// ---- why the estimate is safe ----------------------------------------------
//
// A tape is a rounded segment in the plane (a.d, y - lift): an exact 2D
// distance. a.d (distance to the nearest arc's full circle) has slope 1 in
// cell units, and the circles' unused parts only make it smaller, a lower
// bound. The lift tilts the tape: with |grad lift| <= lift * pi, the gradient
// of the pair is at most the largest singular value of [[1, 0], [lift pi, 1]],
// sqrt of the larger eigenvalue of [[1, a], [a, 1 + a^2]] for a = lift pi,
// and the Chaos bend stretches the plan by at most gJ. Both are computed per
// frame into gLip. Above a cloth's top only its plane is measured (all of its
// geometry lies below), and the far cloth is bounded by its plane too, so a
// ray never skips a cloth. A sphere around the eye is carved out of everything.

#define WV_MAX_STEPS 100
#define WV_FAR 28.0
#define WV_ZWRAP 64.0
/** 2 pi / 64: z frequencies must be whole multiples of it to survive the rebase. */
#define WV_K 0.09817477
#define WV_BUBBLE 0.2

float gT;
float gHits;
float gEnv;
float gLift;
float gHw;
float gTh;
float gTop;
float gBack;
float gHalf;
float gLip;
vec3 gEye;

/**
 * What the field holds at a point (lib_truchet_field): the distance; the nearest tape's arc, its
 * layer (0 warp, 1 weft, 2 backing, 3 above the cloth) and the distance across it; both tiles' arc
 * distances and colours; the warped plan.
 */
struct TruchetHit {
    float d;
    TruchetArc arc;
    float layer;
    float u;
    float ad;
    float bd;
    float ta;
    float tb;
    vec2 plan;
};

/**
 * Chaos: truchetWarp's bend, rewritten so every z frequency is a whole multiple
 * of WV_K (3 and 12 of them, the nearest to the lib's 0.31 and 1.13), so the
 * mod-64 rebase leaves it unchanged. Each line adds a function of the other
 * axis only, so the map stays periodic in z by 64. Identity at default.
 */
vec2 weaveWarp(vec2 p, float t) {
    float a = 0.8 * (2.0 * gTruchetIn.warp);
    float b = 0.18 * (2.0 * gTruchetIn.chaos);
    if (a + b <= 1e-4) return p;
    p += a * vec2(sin(p.y * (3.0 * WV_K) + t * 0.21), sin(p.x * 0.29 - t * 0.17 + 1.3));
    p += b * vec2(sin(p.y * (12.0 * WV_K) - t * 0.37 + 2.1), sin(p.x * 1.07 + t * 0.33));
    return p;
}

/** truchetHue with its z rates moved onto WV_K multiples: the smooth hue variation. */
float weaveField(vec2 p, float t) {
    float a = sin(p.x * 0.23 + 1.7 * sin(p.y * (2.0 * WV_K) + t * 0.05) + t * 0.03);
    float b = sin(p.y * (2.0 * WV_K) - p.x * 0.07 - t * 0.04 + 1.9);
    return 0.5 * (a + b);
}

/** A flat tape across the ribbon: half-width gHw, half-thickness gTh, rounded edges. */
float tape(float u, float v) {
    vec2 q = abs(vec2(u, v)) - vec2(gHw - gTh, 0.0);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - gTh;
}

/** One woven cloth: yl is the height above its middle plane, xz its plan (cell units). */
TruchetHit cloth(vec2 xz, float yl) {
    TruchetHit h;
    if (yl > gTop + 0.3) {
        // Above the cloth: the distance to its top plane; the rest is never shaded here.
        h.d = yl - gTop;
        h.arc = TruchetArc(1.0, 0.0, 0.0, vec2(1.0, 0.0), 0.0);
        h.layer = 3.0;
        h.u = 0.0;
        h.ad = 1.0;
        h.bd = 1.0;
        h.ta = 0.0;
        h.tb = 0.0;
        h.plan = xz;
        return h;
    }
    vec2 p = weaveWarp(xz, gT);
    h.plan = p;
    vec2 idA = floor(p);
    idA.y = mod(idA.y, WV_ZWRAP);
    TruchetArc a = truchetSquareAt(idA, fract(p) - 0.5, gHits, gEnv, 4.0);
    vec2 pb = p + vec2(0.5, 0.0);
    vec2 idB = floor(pb);
    idB.y = mod(idB.y, WV_ZWRAP);
    TruchetArc b = truchetSquareAt(idB + vec2(131.0, 518.0), fract(pb) - 0.5, gHits, gEnv, 4.0);

    float hc = gLift * sin(TRUCHET_PI * p.x) * sin(TRUCHET_PI * p.y);
    float dA = tape(a.d, yl - hc) * gLip;
    float dB = tape(b.d, yl + hc) * gLip;
    float dK = yl + gBack;

    h.ad = a.d;
    h.bd = b.d;
    h.ta = a.cap;
    h.tb = b.cap;
    float d = dK;
    h.layer = 2.0;
    h.arc = a;
    h.u = 0.0;
    if (dA < d) {
        d = dA;
        h.layer = 0.0;
        h.u = a.d;
    }
    if (dB < d) {
        d = dB;
        h.layer = 1.0;
        h.arc = b;
        h.u = b.d;
    }
    h.d = d;
    return h;
}

TruchetHit truchetMap(vec3 w) {
    float yF = w.y + gHalf;
    float yC = gHalf - w.y;
    TruchetHit h;
    // Only the nearer cloth is tiled; the other is bounded by its top plane,
    // below which (in its own frame) all of its geometry lies.
    if (w.y < 0.0) {
        h = cloth(w.xz, yF);
        h.d = min(h.d, yC - gTop);
    } else {
        h = cloth(w.xz + vec2(37.0, 0.0), yC);
        h.d = min(h.d, yF - gTop);
    }
    h.d = max(h.d, WV_BUBBLE - length(w - gEye));
    return h;
}

//#include lib_truchet_field

void main() {
    vec2 uv = view();
    truchetSetup();
    float t = truchetTime();
    float e = truchetEnergy();
    float morph = gTruchetIn.detail;
    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float k0 = gTruchetIn.pulse - 0.5;
    float k1 = gTruchetIn.texture - 0.5;
    float k2 = gTruchetIn.flow - 0.5;
    float k3 = gTruchetIn.tone - 0.5;
    float k4 = gTruchetIn.build - 0.5;
    float k5 = gTruchetIn.space - 0.5;

    float kick = clamp(SND_KICK_ENV, 0.0, 1.0) * e * (1.0 + 0.8 * k0);
    float bass = clamp(sndShaped(SND_LOW) * e, 0.0, 1.5);
    float snare = clamp(SND_SNARE_ENV, 0.0, 1.0);
    float high = clamp(sndShaped(SND_HIGH), 0.0, 1.0);

    gT = t;
    gHits = floor(SND_HIT_TIME);
    gEnv = SND_ONSET_ENV;
    gHw = (0.12 + 0.03 * kick) * (1.0 - 0.3 * morph);
    gTh = 0.045 * (1.0 - 0.25 * morph);
    gLift = 0.18 * (1.0 + 0.4 * bass);
    gTop = gLift + gTh + 0.01;
    gBack = gLift + gTh + 0.12;
    gHalf = 1.15 + 0.35 * k5;
    float aw = 0.8 * (2.0 * gTruchetIn.warp);
    float bw = 0.18 * (2.0 * gTruchetIn.chaos);
    float gJ = (1.0 + 0.3 * aw) * (1.0 + 1.2 * bw);
    float sl = gLift * TRUCHET_PI;
    float eig = 0.5 * (2.0 + sl * sl + sqrt(sl * sl * sl * sl + 4.0 * sl * sl));
    gLip = 0.92 / (sqrt(eig) * gJ);

    vec3 ro = vec3(uCamPos.x, 0.3 * clamp(uCamPos.y, -1.0, 1.0), uCamPos.z);
    vec3 rd = camRayDir(uv);
    // Every z term tiles with a period dividing 64 (see the header), so this
    // shift is exact and invisible, and z keeps its precision.
    ro.z = mod(ro.z, WV_ZWRAP);
    gEye = ro;

    float hue = 0.04 * sin(t * 0.02) + 0.1 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue) + 0.3 * k3;
    vec3 fwd = normalize(vec3(uCamBasis[2].x, 0.0, uCamBasis[2].z) + vec3(0.0, 0.0, 1e-4));
    vec3 side = vec3(fwd.z, 0.0, -fwd.x);
    vec3 lightPos = ro + fwd * 3.0 + side * (1.6 * sin(t * 0.37)) + vec3(0.0, 0.25 * sin(t * 0.23), 0.0);

    float tm = 0.02;
    float hitT = -1.0;
    float used = 0.0;
    for (int i = 0; i < WV_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (tm > WV_FAR) break;
        used = float(i);
        float d = truchetField(ro + rd * tm).d;
        float eps = 0.0006 * tm + 0.0005;
        if (d < eps) {
            hitT = tm;
            break;
        }
        tm += max(d, eps * 0.5);
    }

    // Fog: the dark cloth colour, with a glow down the far end of the hall.
    float ahead = pow(max(rd.z, 0.0), 10.0);
    vec3 fogCol = pal(hue + 0.82) * (0.03 + 0.14 * ahead) + vec3(0.003, 0.003, 0.005);
    vec3 col = fogCol;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = truchetNormal(p, max(0.0012, 0.001 * hitT));
        float occ = 0.0;
        float sc = 1.0;
        for (int k = 0; k < 2; k++) {
            float hk = 0.06 + 0.12 * float(k);
            occ += (hk - truchetField(p + n * hk).d) * sc;
            sc *= 0.6;
        }
        float ao = clamp(1.0 - 2.2 * occ, 0.2, 1.0) * clamp(1.0 - used / max(uSteps, 1.0) * 0.6, 0.5, 1.0);
        // What the field holds at the hit.
        TruchetHit hit = truchetField(p);

        vec3 toL = lightPos - p;
        float dl = length(toL);
        vec3 l = toL / dl;
        float atten = 1.0 / (1.0 + 0.1 * dl * dl);
        float dif = max(dot(n, l), 0.0);
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float spec = pow(max(dot(reflect(rd, n), l), 0.0), 28.0);
        float field = 0.08 * weaveField(hit.plan, t);
        vec3 lightC = mix(vec3(1.0, 0.94, 0.86), pal(hue + 0.15), 0.3);

        if (hit.layer > 1.5) {
            // The backing cloth: dark, faintly cross-hatched by the two layers'
            // bands, and shadowed under the ribbons, as the flat cloth was.
            float px = hitT * 2.0 / (max(uCamFocal, 0.1) * uResolution.y);
            float tA = mix(1.0 - hit.ta, hit.ta, 1.0 - smoothstep(-px, px, hit.ad));
            float tB = mix(1.0 - hit.tb, hit.tb, 1.0 - smoothstep(-px, px, hit.bd));
            float under = smoothstep(gHw, gHw + 0.12, abs(hit.ad)) * smoothstep(gHw, gHw + 0.12, abs(hit.bd));
            col = pal(hue + 0.82) * (0.05 + 0.035 * tA + 0.03 * tB) * (0.4 + 1.4 * dif * atten)
                * (0.35 + 0.65 * under) * ao;
        } else {
            // A tape: warp or weft, rounded across, threaded along its length.
            vec3 tint = pal(hue + (hit.layer < 0.5 ? 0.08 : 0.58) + field);
            float u = clamp(hit.u / gHw, -1.0, 1.0);
            float crown = sqrt(1.0 - u * u);
            float threads = (7.0 + 4.0 * morph) * (1.0 + 0.6 * k1);
            float px = hitT * 2.0 / (max(uCamFocal, 0.1) * uResolution.y);
            float fine = 1.0 - smoothstep(0.25, 0.6, px * threads / (2.0 * gHw));
            float thread = mix(0.9, 0.82 + 0.18 * cos(u * threads * TRUCHET_PI), fine);
            vec3 tapeCol = tint * (0.25 + 0.75 * crown) * thread;
            col = tapeCol * (0.08 + 1.5 * dif * atten) * ao;
            col += lightC * spec * atten * (0.25 + 0.6 * high) * ao;
            col += tint * fres * 0.12 * ao;
            // Glints run along every ribbon in its path's own direction: two per
            // arc, a whole number, so they flow on from tile to tile unbroken.
            float flow = t * (0.35 + 0.3 * k2);
            float g = pow(0.5 + 0.5 * cos(TRUCHET_TAU * (2.0 * hit.arc.s - flow)), 10.0);
            col += mix(tint, vec3(1.0), 0.4) * g * crown * (0.08 + 0.9 * snare) * (1.0 + 0.8 * k4) * ao;
        }
        col = mix(col, fogCol, 1.0 - exp(-hitT * 0.08));
    }

    // The light itself, a soft glow hanging in the hall.
    vec3 toLight = lightPos - ro;
    float along = max(dot(toLight, rd), 0.0);
    float miss = length(toLight - rd * along);
    if (hitT < 0.0 || along < hitT) col += pal(hue + 0.15) * 0.05 / (1.0 + miss * miss * 60.0);

    col *= 0.85 + 0.12 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
