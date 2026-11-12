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
//#include lib_touch
//#include lib_lucid

// Lucid Weave: the family's plain form, and its free style, now a loom you fly
// through. Two sheets of woven cloth, a floor and a ceiling, billow slowly
// above and below the camera; each is a plain weave of broad flat ribbons,
// warp threads running along the flight and weft threads across it, passing
// over and under one another at every crossing. The 2D style's marbled field
// is printed on the ribbons: every ribbon carries its own read of the same
// three-level value-noise nest, so the marbling slides along the threads and
// the two thread sets read as complements where they cross. The cubic
// contrast curve and the cube still crush the mids, so only the crests of the
// marbling glow; the rest of the cloth is lit, shadowed where a thread dives
// under its neighbour, and lost in fog toward the far end of the loom.
//
// It is still the cheapest Lucid: the march only sees ribbons (rounded boxes
// on two sine heights), and the nest is read once per pixel, at the hit.
//
// ---- camera and the wrap -----------------------------------------------------
//
// The shared rig flies the gap between the sheets (camFly). Everything that
// depends on z repeats every 16 world units, which divides the rig's 8192
// travel wrap: the over-under heights have period 2, the weft colour reads
// its thread index mod 8, the billow's z rates are whole multiples of
// 2 pi / 16, and a warp ribbon's marbling is read along a circle in the nest's
// plane, one loop per 16 units (an annulus, so its colour closes on itself).
// The eye is rebased by mod(z, 16) before the march, so z stays precise and
// neither the rebase nor the wrap can show. x and y are not wrapped. A sphere
// around the eye is carved out of everything, so the eye is never in cloth.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (sndShaped(SND_LOW), slew-limited) is the warp STRENGTH, as in the 2D
// style: quiet passages unknot the marbling into soft ribbons, a heavy low end
// ties it into tight knots. It also lifts the billow of the cloth a little,
// so the loom heaves on the low end. Both are smooth geometric parameters, so
// the cloth kneads rather than jumps. Travel of the marbling and of the
// billow rides uFlowTime (loudness sets the rate, never the direction).
// Overall level follows SND_INTENSITY gently. A finger pushes the marbling
// aside where it touches.
//
// ---- macros and knobs -------------------------------------------------------
//
// Speed: uTime and uFlowTime. Complexity: uMorph >= 0.5 adds a nest level and
// an octave (lucidDetail). Chaos: uTurbulence/uWarp raise warp strength
// (lucidChaos) and jitter the nest's input (lucidJitter); uTwist, uKaleido/
// uSymmetry, uZoom and uRotation reach the view through lucidView. Color:
// pal() tints every channel. Energy: uIntensity scales the bass push.
// Knobs: k0 PULSE bass push, k1 TEXTURE marbling scale, k2 FLOW billow height,
// k3 TONE hue, k4 BUILD crest glow, k5 SPACE the gap between the sheets. All
// neutral at 0.5.
//
// ---- why the distance estimate is safe --------------------------------------
//
// A ribbon is a rounded box in 2D, (across, v - h), where v = y - sheet
// height. The box distance is 1-Lipschitz in its two arguments; their
// Jacobian is the identity plus one row (-db/dx, 0, -db/dz - dh/dz), so its
// norm is at most 1 + |grad b| + |dh/dz|. The billow b is a sum of two sines
// whose gradient is at most gB * 0.695 (their weighted frequencies), and the
// over-under height h has slope at most LW_A * pi. gLip is that sum, and the
// distance is divided by it. Each sheet measures its nearest thread of each
// set and that thread's nearer neighbour (the neighbour sits on the opposite
// over-under phase, so it can be the closer one from above); threads further
// away are at least a full pitch off, and the 0.9 step factor covers the
// rest. The eye sphere joins through max, which keeps the bound.

#define LW_MAX_STEPS 100
#define LW_FAR 32.0
#define LW_ZWRAP 16.0
/** 2 pi / 16: the z frequency unit. Every z rate is an integer multiple. */
#define LW_U 0.39269908
#define LW_PI 3.14159265
/** Over-under amplitude, ribbon half width, half thickness and corner radius. */
#define LW_A 0.14
#define LW_W 0.36
#define LW_TH 0.05
#define LW_R 0.035
/** Radius of a warp ribbon's colour loop in the nest's plane: 0.4 nest units per world unit. */
#define LW_RING 1.0185916
#define LW_BUBBLE 0.7

float gH;
float gB;
float gPh;
float gLip;
vec3 gEye;

/** The slow billow of sheet `sgn` (-1 floor, +1 ceiling). z rates are LW_U and 2 LW_U. */
float billow(vec2 xz, float sgn) {
    float ph = gPh + (sgn > 0.0 ? 2.1 : 0.0);
    return gB * (0.6 * sin(0.45 * xz.x + LW_U * xz.y + ph)
               + 0.4 * sin(-0.31 * xz.x + 2.0 * LW_U * xz.y - 0.7 * ph + 1.3));
}

/** A flat ribbon's cross-section: a rounded box. */
float ribbon(vec2 q) {
    vec2 d = abs(q) - vec2(LW_W - LW_R, LW_TH - LW_R);
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - LW_R;
}

/**
 * Distance (not yet divided by gLip) to sheet `sgn`. info: x = 0 for a warp
 * thread, 1 for weft; y = the thread's index; z = the across coordinate;
 * w = its over-under parity (+1 or -1).
 */
float sheet(vec3 p, float sgn, out vec4 info) {
    float v = p.y - (sgn * gH + billow(p.xz, sgn));
    float sz = sin(LW_PI * p.z);
    float sx = sin(LW_PI * p.x);

    // Warp: along z, centred on x = i + 0.5, height A (-1)^i sin(pi z).
    float ix = floor(p.x);
    float dx = p.x - ix - 0.5;
    float nx = ix + (dx > 0.0 ? 1.0 : -1.0);
    float pi0 = 1.0 - 2.0 * mod(ix, 2.0);
    float hw = LW_A * pi0 * sz;
    float dw0 = ribbon(vec2(dx, v - hw));
    float dw1 = ribbon(vec2(p.x - nx - 0.5, v + hw));

    // Weft: along x, centred on z = j + 0.5, height -A (-1)^j sin(pi x), so
    // at every crossing it sits on the far side of the warp.
    float jz = floor(p.z);
    float dz = p.z - jz - 0.5;
    float nz = jz + (dz > 0.0 ? 1.0 : -1.0);
    float pj0 = 1.0 - 2.0 * mod(jz, 2.0);
    float hf = -LW_A * pj0 * sx;
    float df0 = ribbon(vec2(dz, v - hf));
    float df1 = ribbon(vec2(p.z - nz - 0.5, v + hf));

    float d = dw0;
    info = vec4(0.0, ix, dx, pi0);
    if (dw1 < d) {
        d = dw1;
        info = vec4(0.0, nx, p.x - nx - 0.5, -pi0);
    }
    if (df0 < d) {
        d = df0;
        info = vec4(1.0, jz, dz, pj0);
    }
    if (df1 < d) {
        d = df1;
        info = vec4(1.0, nz, p.z - nz - 0.5, -pj0);
    }
    return d;
}

float mapAt(vec3 p, inout LucidHit found) {
    vec4 i0;
    vec4 i1;
    float d = min(sheet(p, -1.0, i0), sheet(p, 1.0, i1)) / gLip;
    return max(d, LW_BUBBLE - length(p - gEye));
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
    vec2 uv = lucidView(true);
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)
    float flow = uFlowTime;

    // The normalization shell (lib_lucid): every variable from its driver, mapped to its range at its one site.
    LucidIn N = lucidIn();

    float bass = clamp(sndShaped(SND_LOW), 0.0, 1.5) * uIntensity;
    float swell = clamp(SND_INTENSITY, 0.0, 1.5);

    int depth = lucidDetail(3, 2);
    int oct = lucidDetail(3, 2);
    float strength = (1.0 + 1.25 * bass * lucidSpan(N.emit, 1.0, 0.5, 1.5)) * lucidChaos();

    // The 2D style's finger push, kept in screen space: it moves the marbling.
    vec2 push = (uv - touchAnchor()) * 0.9 * touchFalloff(uv, 0.45);

    gH = 3.0 * lucidSpan(N.reach, 1.0, 0.825, 1.175);
    gB = clamp((0.28 + 0.2 * min(bass, 1.0)) * lucidSpan(N.flow, 1.0, 0.55, 1.45), 0.0, 0.6);
    gPh = flow * 0.35 + t * 0.05;
    gLip = 1.0 + gB * 0.695 + LW_A * LW_PI;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term repeats every 16 units (see the header): exact and invisible.
    ro.z = mod(ro.z, LW_ZWRAP);
    gEye = ro;

    float tm = 0.03;
    float hitT = -1.0;
    float used = 0.0;
    for (int i = 0; i < LW_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (tm > LW_FAR) break;
        used = float(i);
        fld = lucidField(ro + rd * tm);
        float d = fld.d;
        float eps = 0.0008 * tm + 0.0005;
        if (d < eps) {
            hitT = tm;
            break;
        }
        tm += max(d * 0.9, eps);
    }

    float hue = lucidSpan(N.tone, 0.0, -0.2, 0.2) + 0.03 * sin(uTime * 0.021);
    float fogK = 0.07;
    float ahead = pow(max(rd.z, 0.0), 6.0);
    vec3 fogCol = pal(hue + 0.6) * (0.03 + 0.10 * ahead) + vec3(0.004, 0.003, 0.007);
    vec3 col = fogCol;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec4 i0;
        vec4 i1;
        float d0 = sheet(p, -1.0, i0);
        float d1 = sheet(p, 1.0, i1);
        float sgn = d0 < d1 ? -1.0 : 1.0;
        vec4 info = d0 < d1 ? i0 : i1;

        // Where on its own thread this point is, in the nest's plane.
        vec2 q;
        float hr;
        if (info.x < 0.5) {
            // A warp ribbon runs along z: read its marbling round a circle,
            // one loop per 16 units, so the colour closes over the rebase.
            float th = p.z * LW_U;
            q = (LW_RING + info.z * 0.9) * vec2(cos(th), sin(th))
              + vec2(7.3, 3.1) * (4.0 * lucidHash1(info.y + 31.0 * sgn));
            hr = info.w * sin(LW_PI * p.z);
        } else {
            // A weft ribbon runs along x; its index is read mod 8 (16 units).
            float jm = mod(info.y, 8.0);
            q = vec2(p.x * 0.4, info.z * 0.9)
              + vec2(19.0, 5.0) * (4.0 * lucidHash1(jm + 17.0 * sgn));
            hr = -info.w * sin(LW_PI * p.x);
        }
        q *= 1.5 * lucidSpan(N.scale, 1.0, 0.6, 1.4);
        q += 2.0 * push;
        q += lucidJitter(q, t);

        vec4 n = lucidNest(q, depth, strength, t * 0.30 + flow * 0.22, LUCID_VALUE, oct);

        // The 2D style's recolour: the nest's three channels as rgb, the
        // order turning slowly; the weft set is turned one step further, so
        // the two thread sets read as complements where they cross.
        vec3 raw = 0.5 + 0.5 * vec3(n.z, n.x, n.y);
        vec3 tint = pal(raw.x * 0.65 + 0.2 * n.w + hue);
        vec3 albedo = raw * (0.45 + 0.55 * tint);
        vec3 turned = lucidChannelTurn(albedo, t * 0.02 + 0.9 * n.w + 0.25 * uv.x + info.x);
        vec3 crest = lucidCube(lucidCubic(turned, 0.8) * 1.3);

        vec3 nrm = lucidNormal(p, max(0.0012, 0.001 * hitT));
        float ndv = clamp(dot(nrm, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.1, 0.3, 1.0);
        // The light comes from the gap, so floor and ceiling are both lit.
        vec3 L = normalize(vec3(0.35, -0.85 * sgn, 0.4));
        float dif = max(dot(nrm, L), 0.0);
        float spec = pow(max(dot(reflect(rd, nrm), L), 0.0), 30.0);
        // A thread diving under its neighbour is in that neighbour's shadow.
        float occ = 0.45 + 0.55 * (0.5 + 0.5 * hr);
        float across = info.z / LW_W;
        float edge = 1.0 - 0.35 * across * across;

        vec3 surf = turned * (0.05 + 0.35 * dif) * occ * edge * ao
                  + crest * (0.8 + lucidSpan(N.glow, 0.0, -0.45, 0.45) + 0.25 * swell) * (0.5 + 0.5 * occ)
                  + pal(hue + 0.5) * fres * 0.18 * occ
                  + vec3(spec) * 0.2 * occ * ao;
        col = mix(surf, fogCol, 1.0 - exp(-hitT * fogK));
    }

    col *= 0.85 + 0.12 * swell;
    col *= 0.75 + 0.25 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
