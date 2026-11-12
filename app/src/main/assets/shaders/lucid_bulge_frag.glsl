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

// Lucid Bulge: soap bubbles swelling out of a flat film, now a flight between
// two films. A floor and a ceiling of soap film billow slowly above and below
// the camera, and out of each one bubbles bulge toward the gap: spheres sunk
// part way behind the film and joined to it by a soft meniscus, each slowly
// rising and settling on its own clock, so the skin looks as if it breathes
// domes out of itself. Small bubbles line the flight lane, big ones stand to
// the sides.
//
// The look is the 2D style's, carried onto real surfaces. A bubble is skinned
// exactly as the old one was: its own surface direction, mapped from the pole
// by arc length (asin, so the rim compresses as a pattern painted on a ball
// does), feeds a simplex nest four levels deep that slides round the sphere.
// Its colour is thin-film interference: the nest sets the film's thickness,
// the true view angle adds the angle-of-view shift (so the rim runs through
// more colours than the face), and the film drains toward each bubble's base
// so its foot is thicker than its crown. A soft specular window and a dark
// limb make it round. The films between the bubbles keep the cheaper
// two-level nest, dimmer, with a contact shadow round every bubble's foot.
//
// ---- camera and the wrap -----------------------------------------------------
//
// The shared rig flies the gap (camFly). Bubbles sit one per 8 x 8 cell of the
// film; a cell's hashes read its z index mod 32, so the bubbles repeat every
// 256 world units. The billow's z rates are whole multiples of 2 pi / 16. The
// flat film's marbling is read through a conformal map of the plane onto an
// annulus, q = R exp(k x) (cos k z, sin k z) with k = 2 pi / 256: it is
// periodic in z with period 256 and, being conformal, never stretches the
// noise more one way than the other. 256 divides the rig's 8192 travel wrap,
// so the eye is rebased by mod(z, 256) before the march and neither the rebase
// nor the wrap can show. The bubble skins are read in each bubble's own frame,
// so they never see z at all. A sphere round the eye is carved out of
// everything, so a rig path that brushes a dome never puts the eye in soap.
//
// ---- audio ------------------------------------------------------------------
//
// PITCH walks the hue, as in the 2D style. The key detector's eased hue
// (uKeyHue; uKeyStrength blends between the walked colour and the resting
// one) and each pitch change (SND_HELD_NOTE_CHANGE, through lucidBump so the
// nudge swells in rather than jumps) rotate the film's colour: the walk turns
// the interference phase and the channel order together, both periodic in a
// whole turn, so the eased key hue wrapping from 0.99 to 0.01 is seamless.
// uBreath gently inflates every bubble, and so does the low end
// (sndShaped(SND_LOW), slew-limited), a little: the domes swell on the bass,
// smoothly, never on one frame. Brightness follows SND_INTENSITY gently. The
// billow and the film's drift ride uFlowTime, so loudness sets their rate,
// never their position.
//
// ---- macros and knobs -------------------------------------------------------
//
// Speed: uTime and uFlowTime (sphere spin, film drift, the bubbles' rise).
// Complexity: uMorph thickens the film (more interference orders) and, past
// 0.5, adds a nest level and an octave (lucidDetail). Chaos: uTurbulence/uWarp
// raise warp strength (lucidChaos) and jitter the film (lucidJitter); uTwist,
// uKaleido/uSymmetry, uZoom and uRotation reach the view through lucidView.
// Color: the film is half pal(), half the physical interference ramp. Energy:
// uIntensity scales the pitch nudge and the breathing. Knobs: k0 PULSE bass
// swell, k1 TEXTURE marbling scale, k2 FLOW billow height, k3 TONE hue offset,
// k4 BUILD specular and rim glow, k5 SPACE the gap between the films. All
// neutral at 0.5.
//
// ---- why the distance estimate is safe --------------------------------------
//
// One film is measured at a time: the one on the point's own side of y = 0 (the films are mirror images through |y|, the ceiling's cells
// shifted half a cell). Every part of the other film lies at least gG beyond
// y = 0 (its height minus the billow minus the tallest dome), so its distance
// is at least |y| + gG, and the field takes the min with that: still a lower
// bound. The film is a height field, (H + b - |y|), divided by 1 plus the
// billow's largest slope. A bubble is an exact sphere, joined to its film by a
// polynomial smooth min, which only lowers the value, and by at most k / 4.
// Only the nearest cell's bubble is measured; every sphere stays LB_GAP inside
// its 8 x 8 cell (largest radius plus jitter), so a neighbour's blended bubble
// is at least the wall distance plus LB_GAP minus k / 4 away, and the field
// takes the min with that. The eye sphere joins through max, which keeps the
// bound.

#define LB_MAX_STEPS 110
#define LB_FAR 48.0
#define LB_ZWRAP 256.0
/** Bubble cell size on the film, x and z. 256 / 8 = 32 cells per wrap. */
#define LB_CELL 8.0
#define LB_ZCELLS 32.0
/** 2 pi / 16: the billow's z frequency unit. */
#define LB_U 0.39269908
/** 2 pi / 256: the conformal film map's angular rate. */
#define LB_K 0.024543693
/** Largest bubble radius (before inflation) and centre jitter (each axis, half range). */
#define LB_RMAX 2.8
#define LB_JIT 0.5
/** Largest inflation factor: breath and bass together are clamped to this. */
#define LB_INFL 1.2
/** How far every sphere stays inside its cell: CELL / 2 - RMAX * INFL - JIT. */
#define LB_GAP 0.14
/** Meniscus radius where a bubble meets its film. */
#define LB_MEN 0.3
#define LB_BUBBLE 0.7

float gH;
float gB;
float gPh;
float gLipB;
float gG;
float gInfl;
float gT;
vec3 gEye;
/** Set by map(): 1 on a bubble, 0 on the film; the side; the bubble's frame. */
float gMat;
float gSgn;
vec3 gLoc;
float gR;
float gRim;
float gCellH;

/** The billow of the film (in |y|), z rates LB_U and 2 LB_U. Slope at most gB * 0.632. */
float billow(vec2 xz, float sgn) {
    float ph = gPh + (sgn > 0.0 ? 2.1 : 0.0);
    return gB * (0.6 * sin(0.31 * xz.x + LB_U * xz.y + ph)
               + 0.4 * sin(-0.23 * xz.x + 2.0 * LB_U * xz.y - 0.7 * ph + 1.3));
}

/** Polynomial smooth min: never above min(a, b), so a lower bound stays one. */
float sminP(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

/** The film on side `sgn` (-1 floor, +1 ceiling) with its bubbles. Sets the g* hit globals. */
float sheet(vec3 p, float sgn) {
    float ys = abs(p.y);
    // The ceiling's cells sit half a cell over, so the films are not one mirror.
    vec2 shift = sgn > 0.0 ? vec2(0.5 * LB_CELL) : vec2(0.0);
    vec2 a = p.xz + shift;
    vec2 cell = floor(a / LB_CELL);
    vec2 key = vec2(cell.x, mod(cell.y, LB_ZCELLS)) + (sgn > 0.0 ? vec2(41.0, 13.0) : vec2(0.0));
    float h1 = lucidHash(key);
    vec2 h2 = lucidHash2(key + 7.7);
    float h3 = lucidHash(key + 19.3);

    vec2 c = (cell + 0.5) * LB_CELL + (h2 - 0.5) * (2.0 * LB_JIT);
    vec2 l = a - c;
    float wx = c.x - shift.x;
    // Small bubbles under the flight lane, big ones to the sides.
    float lane = mix(0.32, 1.0, smoothstep(3.0, 9.0, abs(wx)));
    float R = (1.4 + (LB_RMAX - 1.4) * h1) * lane * gInfl;
    // Each bubble rises and settles on its own slow clock.
    float rise = 0.5 + 0.5 * sin(gT * 0.3 + h3 * 6.2831853);
    float e = R * (0.5 - 0.3 * rise);
    float hs = gH + billow(c - shift, sgn);
    vec3 loc = vec3(l.x, ys - (hs + e), l.y);

    float dP = (gH + billow(p.xz, sgn) - ys) / gLipB;
    float dS = h3 < 0.12 ? 1e3 : length(loc) - R;
    float d = sminP(dP, dS, LB_MEN);
    // Any neighbour's blended bubble is at least this far (see the header).
    vec2 la = abs(a - (cell + 0.5) * LB_CELL);
    d = min(d, 0.5 * LB_CELL - max(la.x, la.y) + LB_GAP - 0.25 * LB_MEN);

    gMat = dS < dP ? 1.0 : 0.0;
    gSgn = sgn;
    gLoc = loc;
    gR = max(R, 1e-3);
    gRim = length(l) - sqrt(max(R * R - e * e, 0.0));
    gCellH = h1 + h3;
    return d;
}

float map(vec3 p) {
    float sgn = p.y < 0.0 ? -1.0 : 1.0;
    // The other film lies at least gG beyond y = 0.
    float d = min(sheet(p, sgn), abs(p.y) + gG);
    return max(d, LB_BUBBLE - length(p - gEye));
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** The film colour at a hue walk; periodic in `walk` with period 1 (film phase and channel order both wrap). */
vec3 walkFilm(float thick, float walk) {
    vec3 film = mix(lucidFilm(thick + walk), pal(thick * 0.5), 0.5);
    return lucidChannelTurn(film, walk * 3.0);
}

void main() {
    vec2 uv = lucidView(true);
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)
    float flow = uFlowTime;
    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float chaos = lucidChaos();

    float kPulse = uKnob[0] - 0.5;
    float kTex = uKnob[1] - 0.5;
    float kFlow = uKnob[2] - 0.5;
    float kTone = uKnob[3] - 0.5;
    float kBuild = uKnob[4] - 0.5;
    float kSpace = uKnob[5] - 0.5;

    // The walk is never scaled (a scaled circular hue would jump where it
    // wraps); key confidence instead blends the walked film with the resting one.
    float walk = uKeyHue + 0.2 * lucidBump(SND_HELD_NOTE_CHANGE) * uIntensity + 0.4 * kTone;
    float rest = 0.4 * kTone;
    float keyed = clamp(uKeyStrength, 0.0, 1.0);
    float orders = 1.0 + 0.8 * clamp(uMorph, 0.0, 1.0);
    float bass = clamp(sndShaped(SND_LOW), 0.0, 1.0);

    gT = t;
    float breath = (clamp(uBreath, 0.9, 1.1) - 1.0) * clamp(uIntensity, 0.0, 2.0);
    gInfl = clamp(1.0 + breath + 0.08 * bass * (1.0 + 1.6 * kPulse), 0.8, LB_INFL);
    gH = 4.4 * (1.0 + 0.5 * kSpace);
    gB = clamp(0.3 * (1.0 + 0.9 * kFlow), 0.0, 0.5);
    gPh = flow * 0.3 + t * 0.04;
    gLipB = 1.0 + gB * 0.632;
    // Tallest dome above its film: 0.8 R at full rise.
    gG = max(gH - gB - 0.8 * LB_RMAX * LB_INFL, 0.0);

    vec3 ro;
    vec3 rd;
    // The mids roll the view about the way ahead (lib_lucid's lucidMidTurn).
    camFly(lucidRot(lucidMidTurn()) * uv, ro, rd);
    // Every z term repeats every 256 units (see the header): exact and invisible.
    ro.z = mod(ro.z, LB_ZWRAP);
    gEye = ro;

    float tm = 0.03;
    float hitT = -1.0;
    float used = 0.0;
    for (int i = 0; i < LB_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (tm > LB_FAR) break;
        used = float(i);
        float d = map(ro + rd * tm);
        float eps = 0.0008 * tm + 0.0005;
        if (d < eps) {
            hitT = tm;
            break;
        }
        tm += max(d * 0.9, eps);
    }

    float fogK = 0.045;
    float ahead = pow(max(rd.z, 0.0), 6.0);
    vec3 fogCol = walkFilm(0.55, rest) * (0.025 + 0.08 * ahead) + vec3(0.004, 0.003, 0.007);
    vec3 col = fogCol;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 nrm = normalAt(p, max(0.0012, 0.001 * hitT));
        // Restore the hit's globals after the normal taps.
        map(p);
        float sgn = gSgn;
        float ndv = clamp(dot(nrm, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.1, 0.3, 1.0);
        // The light comes from the gap, so floor and ceiling are both lit.
        vec3 L = normalize(vec3(-0.45, -0.8 * sgn, 0.4));
        float dif = max(dot(nrm, L), 0.0);
        float spec = pow(max(dot(reflect(rd, nrm), L), 0.0), 40.0);
        float glow = 1.0 + 1.2 * kBuild;
        // The ceiling's film reads one interference order further on.
        float sideShift = sgn > 0.0 ? 0.17 : 0.0;

        vec3 surf;
        if (gMat > 0.5) {
            int depth = lucidDetail(4, 3);
            int oct = lucidDetail(3, 2);
            // The bubble's own frame: local y points away from the gap, so the
            // pole (the crown) is -y. Arc length from the pole compresses the rim.
            vec3 ns = gLoc / gR;
            float up = clamp(-ns.y, -1.0, 1.0);
            vec2 s = vec2(ns.x, ns.z * -sgn);
            float rs = length(s);
            vec2 q = s * (asin(min(rs, 0.9999)) / max(rs, 1e-4)) * 2.2 * (1.0 + 0.8 * kTex);
            q += vec2(t * 0.12, t * 0.03) + 9.0 * vec2(gCellH, fract(gCellH * 3.7));
            q += lucidJitter(q, t);
            vec4 n = lucidNest(q, depth, 1.35 * chaos, t * 0.2, LUCID_SIMPLEX, oct);
            // View angle thickens the optical path; the film drains to the foot.
            float thick = (0.9 + 0.5 * n.z + 0.7 * (1.0 - ndv) + 0.25 * (1.0 - up) + sideShift) * orders;
            vec3 film = mix(walkFilm(thick, walk), walkFilm(thick, rest), 1.0 - keyed);
            float limb = 0.35 + 0.65 * pow(ndv, 0.6);
            surf = film * limb * (0.55 + 0.45 * smoothstep(-0.6, 0.8, n.z)) * (0.6 + 0.4 * dif) * (0.55 + 0.45 * ao);
            // The soft window: a sharp highlight on a broad sheen.
            float sheen = pow(max(dot(reflect(rd, nrm), L), 0.0), 6.0);
            surf += vec3(spec * 0.55 + sheen * 0.06) * glow;
            surf += pal(0.5 + rest) * fres * 0.25 * glow;
        } else {
            // The flat film: same colouring, two levels, dimmer, read through the
            // conformal annulus map so it repeats with the 256 wrap.
            float sc = 0.45 * (1.0 + 0.8 * kTex);
            float th = p.z * LB_K + (sgn > 0.0 ? 3.14159265 : 0.0);
            vec2 q = (sc / LB_K) * exp(clamp(p.x, -60.0, 60.0) * LB_K) * vec2(cos(th), sin(th));
            q += vec2(-flow * 0.05, t * 0.02);
            q += lucidJitter(q, t);
            vec4 n = lucidNest(q, 2, 1.1 * chaos, t * 0.15, LUCID_SIMPLEX, 2);
            float thick = (0.6 + 0.45 * n.z + 0.35 * (1.0 - ndv) + sideShift) * orders;
            vec3 film = mix(walkFilm(thick, walk), walkFilm(thick, rest), 1.0 - keyed);
            surf = film * 0.32 * smoothstep(-0.7, 0.9, n.z) * (0.55 + 0.45 * dif) * ao;
            surf += vec3(spec) * 0.12 * glow + pal(0.5 + rest) * fres * 0.08;
            // Contact shadow where a bubble meets the film.
            surf *= 0.25 + 0.75 * smoothstep(-0.2, 0.9, gRim);
        }
        surf = lucidCubic(surf, 0.45);
        col = mix(surf, fogCol, 1.0 - exp(-hitT * fogK));
    }

    col *= 0.85 + 0.12 * swell;
    col *= 0.75 + 0.25 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
