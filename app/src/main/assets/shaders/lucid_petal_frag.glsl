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

// Lucid Petal: a flower that never stops opening, now a corridor of them you
// fly through. The 2D style measured the plane in petal-shaped rings (the
// radius divided by a rose curve) and banded them into layers that bloomed
// outward from the centre. Here every layer is a real corolla: a thin cupped
// sheet of petals hung across a tube whose own cross-section is a softer rose,
// one corolla every 4 world units down the flight. Each faces the camera, its
// petals cupped toward the eye around a throat the camera flies through, so
// the rings still bloom outward from the centre, only now by perspective.
// Far ahead the corollas are closed buds (a steep cup); as one comes near it
// opens flat, so the flower opens without end. Each layer has its own turn
// and its own read of the family's value nest (three levels), so petals are
// veined and mottled differently ring by ring, and layers alternate their
// channel order, as before.
//
// Coloured as fire opal: a warm palette body read from the nest, shaded
// darker toward each petal's edge, with play-of-colour flecks - small cells of
// saturated colour fixed to the petal that light up inside the body. The tube
// wall between corollas is the same opal, dim. The far end of the corridor is
// fog that falls to black: the dark eye at the heart of the flower.
//
// ---- camera and the wrap -----------------------------------------------------
//
// camFly: the rig's path runs down +z through the throats (the throat is wider
// than the glide path's usual wander; a sphere round the eye is carved out of
// everything, so a wider orbit grazes a petal instead of entering it). Every z
// term repeats every 64 world units: a corolla per 4, its turn read from its
// index mod 16, and the wall's opal read round a circle once per 64. 64
// divides the rig's 8192 travel wrap, and the eye is rebased by mod(z, 64)
// before the march, so z stays precise and neither the rebase nor the wrap
// shows. The opening is measured from the rebased eye to the rebased corolla,
// so it agrees on both sides of the rebase.
//
// ---- audio ------------------------------------------------------------------
//
// SUB-BASS (sndSpectrum(0.10)) swells the petals, as in the 2D style: it
// deepens the rose curve of every corolla, so on a sub drop the outline goes
// from a near-circle to deep lobes, and past half a swell the petals split
// apart into separate blades. It moves geometry only, never light. Busy mids
// (sndShaped(SND_MIDDLE)) brighten the opal flecks' colour, not the frame.
// Brightness follows SND_LEVEL gently. The rig carries the music's own camera
// moves (kick dolly, drop push).
//
// ---- macros and knobs -------------------------------------------------------
//
// Speed: uTime and uFlowTime (the corollas' turn), and the rig's fly speed.
// Complexity: uMorph adds petals (5 -> 8, whole petals only, stepped at
// thirds) and, past 0.5, a nest level and an octave (lucidDetail). Chaos:
// uTurbulence/uWarp raise warp strength (lucidChaos) and jitter the veins
// (lucidJitter); uTwist twists the view through lucidView. Kaleido: when
// uKaleido is on, uSymmetry sets the petal count (3..12) instead of folding
// the view. Color: body and flecks are pal(). Energy: uIntensity scales the
// sub swell and the fleck response. Space: uZoom/uRotation through lucidView.
// Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE the sub swell,
// k1 TEXTURE the opal's grain, k2 FLOW how far ahead the buds open, k3 TONE
// the hue, k4 BUILD the fleck glow, k5 SPACE the corridor's size.
//
// ---- why the distance estimate is safe --------------------------------------
//
// Everything is measured against rose radii R * rose(theta), rose = 1 -
// amp * (0.5 - 0.5 cos(P theta)), with P a whole number (so there is no seam
// at atan's cut). |d rose / d theta| <= amp P / 2 = rpm and rose >= 1 - amp =
// rmn. A radial gap R rose - r has gradient sqrt(1 + (R rose' / r)^2), and it
// is divided by that bound at the point's own r; inside R rmn the plain
// R rmn - r is used instead, a true lower bound, so the axis never sees the
// 1 / r blow-up. A corolla is the sheet z = zc - k (r - Rin rose), whose
// gradient is at most sqrt(1 + k^2 (1 + (rpm / rmn)^2)) where it exists
// (r >= Rin rmn); it is cut by the throat, the tips and, when the petals
// split, angular gaps whose arc distance has gradient under 1.2. Those join by
// max, which keeps the bound. A corolla's whole sheet stays within z in
// [zc - k (Rtip - Rin), zc] with k <= 0.8, at least 0.45 inside its own
// 4-unit cell, so each step is also clamped to the cell edge plus 0.45 and the
// march only ever measures the corolla of the cell it is in. The eye sphere
// joins through max.

#define LP_MAX_STEPS 96
#define LP_FAR 40.0
#define LP_TAU 6.2831853
/** Rebase period of the eye's z; divides 8192. */
#define LP_ZWRAP 64.0
/** World units per corolla, and corollas before the turn pattern repeats (4 * 16 = 64). */
#define LP_CELL 4.0
#define LP_LAYERS 16.0
/** Where in its cell a corolla's throat sits (cell-local z). */
#define LP_THROAT_Z 1.4
/** Half-thickness of a petal sheet and of the tube wall. */
#define LP_TH 0.05
#define LP_WALL_TH 0.12
/** Cup slope of a closed bud and of an open flower. */
#define LP_K_BUD 0.8
#define LP_K_OPEN 0.28
#define LP_BUBBLE 0.8

vec3 gEye;
float gP;
float gAmp;
float gRmn;
float gRpm;
float gAmpW;
float gRmnW;
float gRpmW;
float gW;
float gRin;
float gRtip;
float gRw;
float gTurn;
float gTurnW;
float gOpenFar;

float roseAt(float th, float amp) {
    return 1.0 - amp * (0.5 - 0.5 * cos(gP * th));
}

/** Signed radial gap to r = R * rose, positive inside (smaller r), scaled to a distance bound (see the header). */
float radialGap(float r, float R, float rose, float rmn, float rpm) {
    float lo = R * rmn;
    if (r < lo) return lo - r;
    float s = R * rpm / r;
    return (R * rose - r) / sqrt(1.0 + s * s);
}

float mapAt(vec3 p, inout LucidHit found) {
    float r = length(p.xy);
    float th = atan(p.y, p.x);

    // The tube: a shell on half the petals' rose depth, so it stays round behind them.
    float rw = roseAt(th + gTurnW, gAmpW);
    float dW = abs(radialGap(r, gRw, rw, gRmnW, gRpmW)) - LP_WALL_TH;

    // The corolla of this cell.
    float cell = floor(p.z / LP_CELL);
    float lzc = p.z - (cell + 0.5) * LP_CELL;
    float id = mod(cell, LP_LAYERS);
    float thL = th + gTurn + id * 0.77;
    float rl = roseAt(thL, gAmp);
    float zc = (cell + 0.5) * LP_CELL + LP_THROAT_Z;
    float open = smoothstep(gOpenFar, 3.0, zc - gEye.z);
    float k = mix(LP_K_BUD, LP_K_OPEN, open);
    float g = (lzc - LP_THROAT_Z) + k * (r - gRin * rl);
    float sq = gRpm / gRmn;
    float lg = sqrt(1.0 + k * k * (1.0 + sq * sq));
    float dSheet = abs(g) / lg - LP_TH;
    float dHole = radialGap(r, gRin, rl, gRmn, gRpm);
    float dTip = -radialGap(r, gRtip, rl, gRmn, gRpm);
    float fa = fract(thL * gP / LP_TAU + 0.5) - 0.5;
    float dGap = (abs(fa) - gW) * (LP_TAU / gP) * r / 1.2;
    float dP = max(max(dSheet, dHole), max(dTip, dGap));
    // Only this cell's corolla is measured: never step past the cell edge by
    // more than the gap every corolla keeps from it.
    dP = min(dP, 0.5 * LP_CELL - abs(lzc) + 0.45);

    found.mat = dP < dW ? 1.0 : 0.0;
    found.a.x = id;
    found.a.y = thL;
    found.a.z = rl;
    found.a.w = fa;
    found.b.x = open;
    float d = min(dP, dW);
    return max(d, LP_BUBBLE - length(p - gEye));
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
    // The kaleidoscope is read as the petal count below, so the view is not folded.
    vec2 uv = lucidView(false);
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)
    float flow = uFlowTime;

    // The normalization shell (lib_lucid): every variable from its driver, mapped to its range at its one site.
    LucidIn N = lucidIn();

    float sub = clamp(sndSpectrum(0.10), 0.0, 1.0) * uIntensity * lucidSpan(N.emit, 1.0, 0.5, 1.5);
    float mid = clamp(sndShaped(SND_MIDDLE), 0.0, 1.5) * uIntensity;
    float swell = clamp(SND_LEVEL, 0.0, 1.5);

    int depth = lucidDetail(3, 2);
    int oct = lucidDetail(3, 2);

    gP = (uKaleido > 0.5 && uSymmetry >= 2.0) ? clamp(floor(uSymmetry + 0.5), 3.0, 12.0)
                                              : 5.0 + floor(min(N.depth, 0.999) * 3.0);
    float subC = clamp(sub, 0.0, 1.5);
    gAmp = clamp(0.12 + 0.24 * subC, 0.0, 0.45);
    gRmn = 1.0 - gAmp;
    gRpm = gAmp * gP * 0.5;
    gAmpW = 0.5 * gAmp;
    gRmnW = 1.0 - gAmpW;
    gRpmW = gAmpW * gP * 0.5;
    // Past half a swell the petals split into separate blades.
    gW = 0.5 - 0.36 * smoothstep(0.5, 1.2, subC);
    float space = lucidSpan(N.reach, 1.0, 0.75, 1.25);
    gRin = 2.6 * space;
    gRtip = 5.0 * space;
    gRw = 6.0 * space;
    gTurn = t * 0.03 + flow * 0.05;
    gTurnW = -t * 0.02;
    gOpenFar = clamp(24.0 * lucidSpan(N.flow, 1.0, 0.6, 1.4), 8.0, 38.0);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term repeats every 64 units (see the header): exact and invisible.
    ro.z = mod(ro.z, LP_ZWRAP);
    gEye = ro;

    float tm = 0.05;
    float hitT = -1.0;
    float used = 0.0;
    for (int i = 0; i < LP_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (tm > LP_FAR) break;
        used = float(i);
        fld = lucidField(ro + rd * tm);
        float d = fld.d;
        float eps = 0.0008 * tm + 0.0006;
        if (d < eps) {
            hitT = tm;
            break;
        }
        tm += max(d * 0.85, eps);
    }

    float hue = lucidSpan(N.tone, 0.0, -0.2, 0.2);
    // The dark eye: the far end of the corridor is fog that falls to black,
    // with a faint warm haze round it.
    float ahead = max(rd.z, 0.0);
    vec3 fogCol = pal(hue + 0.05) * (0.05 * (1.0 - pow(ahead, 12.0)) + 0.01);
    vec3 col = fogCol;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = lucidNormal(p, max(0.0012, 0.001 * hitT));
        // Restore the hit's globals after the normal taps.
        fld = lucidField(p);
        float r = length(p.xy);
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.1, 0.3, 1.0);
        // A headlight on the eye, and a warm light from down the corridor
        // that shines through the petals from behind.
        vec3 Lh = normalize(-rd + vec3(0.0, 0.35, 0.0));
        float dif = max(dot(n, Lh), 0.0);
        float spec = pow(max(dot(reflect(rd, n), Lh), 0.0), 36.0);

        vec2 q;
        float id = fld.a.x;
        if (fld.mat > 0.5) {
            // Where on its own petal this point is: across the blade and along it.
            float rho = r / fld.a.z;
            q = lucidRot(id * 0.77) * vec2(cos(fld.a.y), sin(fld.a.y)) * rho * 0.42 * lucidSpan(N.scale, 1.0, 0.6, 1.4)
              + vec2(id * 3.1, id * 1.7);
        } else {
            // The wall's opal is read round two circles: the angle, and z once per 64.
            float tz = p.z * (LP_TAU / LP_ZWRAP);
            q = vec2(cos(atan(p.y, p.x)), sin(atan(p.y, p.x))) * 1.4
              + vec2(cos(tz), sin(tz)) * 4.0 + vec2(11.0, 3.0);
            q *= lucidSpan(N.scale, 1.0, 0.6, 1.4);
        }
        q += lucidJitter(q, t);
        vec4 nz = lucidNest(q, depth, 1.2 * lucidChaos(), t * 0.18, LUCID_VALUE, oct);

        // Opal body: warm, shaded darker toward the petal's edge.
        float body = 0.5 + 0.5 * nz.z;
        vec3 albedo = pal(hue + 0.02 + 0.14 * body + 0.06 * id) * (0.3 + 0.75 * body);
        if (fld.mat > 0.5) {
            float along = clamp((r / fld.a.z - gRin) / max(gRtip - gRin, 0.1), 0.0, 1.0);
            float across = clamp(abs(fld.a.w) / max(gW, 0.05), 0.0, 1.0);
            float edge = (1.0 - 0.65 * across * across) * smoothstep(1.0, 0.7, along) * (0.55 + 0.45 * smoothstep(0.0, 0.2, along));
            vec3 surf = albedo * (0.10 + 0.55 * dif) * ao;
            surf *= 0.3 + 0.7 * edge;
            // Light from down the corridor through the thin petal, strongest at its base.
            surf += albedo * (0.12 + 0.12 * fld.b.x) * (1.0 - along) * (1.0 - 0.5 * across);

            // Play of colour: small saturated cells fixed to the petal.
            vec3 c = lucidCells(q * 3.0 + nz.xy, 0.9);
            float fleck = smoothstep(0.35, 0.05, c.x) * step(0.55, c.z);
            surf += pal(c.z * 2.0 + nz.w + hue) * fleck * (0.3 + 0.35 * clamp(mid, 0.0, 1.0)) * lucidSpan(N.glow, 1.0, 0.4, 1.6) * edge;
            surf += pal(hue + 0.5 + 0.3 * nz.w) * fres * 0.15 + vec3(spec) * 0.18 * ao;

            surf = lucidChannelTurn(surf, mod(id, 2.0) * 0.5);
            col = lucidCubic(surf, 0.6);
        } else {
            vec3 surf = albedo * (0.04 + 0.22 * dif) * ao + pal(hue + 0.55) * fres * 0.06;
            col = lucidCubic(surf, 0.4);
        }
        col = mix(col, fogCol, 1.0 - exp(-hitT * 0.06));
    }

    col *= 0.85 + 0.12 * swell;
    col *= 0.75 + 0.25 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
