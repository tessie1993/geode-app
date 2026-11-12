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
//#include lib_neon

// Ribbon Storm: six glowing silk ribbons swirling around a dark core, circled
// by the eye.
//
// WHAT IS DRAWN, built the way the Flame, Opal and Hollows families are. Each
// ribbon is a comet: a head running round a closed curve (two on a Lissajous
// figure, two on a helix, two on a torus knot), with a tail RS_TAIL of curve
// parameter behind it, cut into RS_SEGS straight segments. Each is real silk,
// sphere-traced and shaded once: a flat strip that twists along its length,
// pleated across its width into a zigzag, and branching toward the head into
// two strands that spiral round it, each wound in turn by two finer threads:
// ribbon, strands, threads, the same spiral at three scales. The strip's frame
// is carried from segment to segment by parallel transport, so the twist runs
// on through every joint without a kink. Silk is lit from the core on both
// faces, its edges and strands glow by themselves, and every ribbon keeps the
// soft halo of the old style: the halos of all six add, the volumetric glow.
//
// COST. Per pixel the ray's exact closest approach to each of the 48 segments
// is measured once (as the old style did for its glow); a segment farther than
// RS_CULL from the ray is dropped, and only the rest (at most RS_KEEP, a few
// on most rays) are marched. One loop of RS_STEPS steps (80 at the default
// Detail, 100 at most), each one map() over the kept segments: one sine and
// cosine each, no noise and no texture. The hit adds four map() for its
// normal. The palette is read five times per pixel (rsSetup) and mixed.
//
// THE CONTROLS, read normalized through lib_neon:
//
//   Morph       gRsForm    the pleats fold deeper, from flat silk to a sharp zigzag
//   Warp        gRsFlow    the ribbons bend in toward one braid through the core
//   Turbulence  gRsChaos   the ribbons flutter
//   Intensity   neonGlow() the emission
//   k0 PULSE the emission, k1 TEXTURE the strands' and threads' line width,
//   k2 FLOW the bend, k3 TONE the palette position, k4 BUILD the glow of the
//   edges, the strands and the halos, k5 SPACE the reach (how far out the eye
//   circles).
//
// THE SOUND, heard the way the families hear it: lib_sound's shaped reads, in
// rsSetup(), on the motion only, never on a colour, a brightness or a beat.
//
//   gRsSwell  the low band, sndShaped(SND_LOW), deepens the Warp's pull toward
//   the braid, never past the Warp's own reach.
//
// It is times SND_DRIVE, the music's absolute magnitude: how MUCH moves
// follows how loud and dense the music is, and the band only shapes the push.
// No sound band turns it (owner 2026-11-12, no style spins to the music).
// What moves on a hit is the reaction matrix, through the params above. uTime
// stops in silence and SND_DRIVE falls to 0, so everything here comes to rest.
// Every clock phase is whole cycles per uTime wrap (RS_RATE), and every curve
// has whole-number frequencies in its parameter, so a head wraps exactly.
//
// WHY THE ESTIMATE IS SAFE. Every motion of a ribbon (curve, braid, flutter,
// turn) moves the segment VERTICES; space is never bent. Within a segment the
// cross-section is a zigzag (a mirror repeat, exact) cut to the width (a max,
// a lower bound), and the strands and threads are mirror pairs (exact). What
// is not exact is the twist: the cross-section turns as it runs along, at
// most RS_TWIST_MAX radians per world unit (each segment's share of the twist
// is capped by its length), the strands twice and the threads four times as
// fast, round points at most 0.22, 0.17 and 0.21 from the axis; so the strip,
// strand and thread distances are divided by 1.1, 1.2 and 1.4, bounds on
// their gradients. Every surface lies within RS_REACH of its segment, and a
// dropped segment is farther than RS_CULL from every point of the ray, so the
// field never returns more than RS_CULL - RS_REACH. The widest the ribbons
// reach, (1.48 + 0.11) * RS_RADIUS plus RS_REACH, is under RS_BOUND = 3.2,
// inside the closest the eye comes (0.65 * RS_EYE_MIN = 3.9).

#define RS_TAU 6.2831853
#define RS_PI 3.14159265
#define RS_MAX_STEPS 100
/** uSteps (64..128 by Detail, 102 at its default, less on a hot phone) on this loop, as the families take it. */
#define RS_STEPS (min(uSteps * 0.78, float(RS_MAX_STEPS)))
/** k whole cycles per uTime wrap (7100 s, ShaderScene). */
#define RS_RATE(k) (RS_TAU * float(k) / 7100.0)
#define RS_RIBBONS 6
#define RS_SEGS 8
/** The most segments one ray marches. */
#define RS_KEEP 24
#define RS_RADIUS 1.8
/** Curve parameter behind each head that its tail covers, radians. */
#define RS_TAIL 1.7
/** The farthest any surface lies from its segment's axis, and the cull radius round the ray. */
#define RS_REACH 0.26
#define RS_CULL 0.58
#define RS_BOUND 3.2
/** camAround radius at SPACE 0 and 1. The dolly pulls the eye in to 0.65 of it. */
#define RS_EYE_MIN 6.0
#define RS_EYE_MAX 8.0
/** The most the cross-section turns per world unit along a ribbon. */
#define RS_TWIST_MAX 1.6
#define RS_THICK 0.012
#define RS_STRAND 0.016
/** How far the strands spiral out from the ribbon at its head. */
#define RS_BRANCH 0.14

float gRsForm;
float gRsFlow;
float gRsChaos;
/** The low band's deepening of the braid, 0..1.5. */
float gRsSwell;
/** The strands' and threads' line width, and the glow, both 1 at rest. */
float gRsLine;
float gRsGlow;
float gRsTone;
vec3 gRsPal0;
vec3 gRsPal1;
vec3 gRsPal2;
vec3 gRsPal3;
vec3 gRsPal4;

/** How deep the pleats fold, how far the ribbons are pulled into the braid, and the flutter. */
float gPleat;
float gBraid;
float gFlutter;
/** The kept segments: start and ribbon * RS_SEGS + segment; end and the twist at the start; the carried frame normal and the twist across the segment. */
vec4 gA[RS_KEEP];
vec4 gB[RS_KEEP];
vec4 gF[RS_KEEP];
int gCount;
/** Set by map(): the ribbon, u along it (0 tail, 1 head), the part (0 strip, 1 strand, 2 thread) and how near the strip's edge (0 middle, 1 edge). */
float gHitRib;
float gHitU;
float gHitPart;
float gHitEdge;

mat2 rsRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

/** The user's palette at t, from the five reads of rsSetup(): there and back, so no seam. */
vec3 rsCol(float t) {
    float x = 4.0 * abs(2.0 * fract(0.5 * t) - 1.0);
    vec3 c = mix(gRsPal0, gRsPal1, smoothstep(0.0, 1.0, clamp(x, 0.0, 1.0)));
    c = mix(c, gRsPal2, smoothstep(0.0, 1.0, clamp(x - 1.0, 0.0, 1.0)));
    c = mix(c, gRsPal3, smoothstep(0.0, 1.0, clamp(x - 2.0, 0.0, 1.0)));
    return mix(c, gRsPal4, smoothstep(0.0, 1.0, clamp(x - 3.0, 0.0, 1.0)));
}

void rsSetup() {
    gRsForm = neonForm();
    gRsFlow = clamp(neonFlow() + 0.5 * neonKnob(2), 0.0, 1.0);
    gRsChaos = neonChaos();
    gRsLine = 1.0 + neonKnob(1);
    gRsGlow = 1.0 + neonKnob(4);
    gRsTone = 0.6 * neonKnob(3);
    // The sound (see the header): the absolute drive sets how much, the band only the shape.
    float drive = clamp(SND_DRIVE, 0.0, 1.0);
    gRsSwell = sndShaped(SND_LOW) * drive;
    gRsPal0 = pal(0.0);
    gRsPal1 = pal(0.25);
    gRsPal2 = pal(0.5);
    gRsPal3 = pal(0.75);
    gRsPal4 = pal(1.0);

    gPleat = 0.15 + 0.9 * gRsForm;
    gBraid = 0.6 * min(gRsFlow + 0.3 * gRsSwell, 1.0);
    gFlutter = 0.06 * gRsChaos;
}

/** A point on curve family `fam`, unit scale, for curve parameter th and ribbon phase ph. */
vec3 familyPoint(int fam, float th, float ph) {
    if (fam == 0) {
        // Lissajous 2:3:1. Largest radius sqrt(1 + 0.62^2 + 0.9^2) = 1.48.
        return vec3(sin(2.0 * th + ph), 0.62 * sin(3.0 * th + 2.0 * ph), 0.9 * sin(th + 0.5 * ph + 1.1));
    }
    if (fam == 1) {
        // A helix about +y: three turns per loop while it bobs up and down once.
        float a = 3.0 * th + ph;
        return vec3(0.95 * cos(a), 1.05 * sin(th + ph), 0.95 * sin(a));
    }
    // A (2,3) torus knot, turned about y and tipped by the ribbon's phase.
    float rr = 0.66 + 0.34 * cos(3.0 * th);
    vec3 k = vec3(rr * cos(2.0 * th), 0.5 * sin(3.0 * th), rr * sin(2.0 * th));
    k.yz = rsRot(0.45 * sin(ph)) * k.yz;
    k.xz = rsRot(0.5 * ph) * k.xz;
    return k * 1.15;
}

/**
 * The braid. Six strands spaced round one axis, their radius rocking in and
 * out with 3 * ph, so neighbouring strands swing opposite ways and weave over
 * and under each other.
 */
vec3 vortexPoint(float th, float ph) {
    float a = 4.0 * th + ph;
    float rr = 0.42 + 0.22 * sin(2.0 * th + 3.0 * ph);
    return vec3(rr * cos(a), 1.25 * sin(th), rr * sin(a));
}

/** World position of ribbon fi at curve parameter th. */
vec3 ribbonPoint(float th, float ph, float fi, int fam) {
    vec3 p = mix(familyPoint(fam, th, ph), vortexPoint(th, ph), gBraid);
    // Turbulence: a flutter riding the curve (by its parameter, so it travels with the ribbon).
    p += gFlutter * sin(vec3(3.0, 4.0, 5.0) * th + vec3(1.3, 2.9, 0.7) * fi
                        + uTime * vec3(RS_RATE(1921), RS_RATE(2599), RS_RATE(2147)));
    return p * RS_RADIUS;
}

/** A ribbon's half-width at its widest. */
float ribbonWidth(float fi) {
    return 0.12 + 0.06 * fract(fi * 0.618 + 0.3);
}

/**
 * Closest approach between the ray ro + rd*t (t >= 0, rd unit) and segment
 * a..b. Returns (distance, t, s), s the position along the segment, 0..1.
 * Solved jointly, then s re-solved for the t clamped to the front of the eye.
 */
vec3 raySegment(vec3 ro, vec3 rd, vec3 a, vec3 b) {
    vec3 ba = b - a;
    vec3 oa = ro - a;
    float baba = max(dot(ba, ba), 1e-6);
    float bard = dot(ba, rd);
    float baoa = dot(ba, oa);
    float rdoa = dot(rd, oa);
    // >= 0 by Cauchy-Schwarz; it only nears 0 when the ray runs along the segment.
    float den = max(baba - bard * bard, 1e-6);
    float s = clamp((baoa - bard * rdoa) / den, 0.0, 1.0);
    float t = max(s * bard - rdoa, 0.0);
    s = clamp((baoa + t * bard) / baba, 0.0, 1.0);
    vec3 c = oa + rd * t - ba * s;
    return vec3(length(c), t, s);
}

float map(vec3 p) {
    float d = RS_CULL - RS_REACH;
    gHitRib = -1.0;
    for (int j = 0; j < RS_KEEP; j++) {
        if (j >= gCount) break;
        int code = int(gA[j].w + 0.5);
        int ri = code / RS_SEGS;
        int k = code - ri * RS_SEGS;
        vec3 a = gA[j].xyz;
        vec3 ba = gB[j].xyz - a;
        float bb = max(dot(ba, ba), 1e-6);
        float s = clamp(dot(p - a, ba) / bb, 0.0, 1.0);
        vec3 q = p - a - ba * s;
        vec3 T = ba * inversesqrt(bb);
        vec3 N0 = gF[j].xyz;
        vec3 B0 = cross(T, N0);
        float ang = gB[j].w + gF[j].w * s;
        float ca = cos(ang);
        float sa = sin(ang);
        // The twisted cross-section, and how far past the segment's ends (only the tail's and the head's show).
        vec2 c0 = vec2(dot(q, ca * N0 + sa * B0), dot(q, ca * B0 - sa * N0));
        float cz = dot(q, T);
        float u = (float(k) + s) / float(RS_SEGS);
        float w = ribbonWidth(float(ri)) * (0.15 + 0.85 * smoothstep(0.0, 0.55, u));

        // The strip: a zigzag across its width (a mirror repeat), cut to the width.
        float per = max(0.55 * w, 0.01);
        float amp = gPleat * per;
        float xm = abs(mod(c0.x + per, 2.0 * per) - per);
        vec2 leg = vec2(per, -2.0 * amp);
        vec2 pa = vec2(xm, c0.y - amp);
        float hl = clamp(dot(pa, leg) / dot(leg, leg), 0.0, 1.0);
        vec2 wq = vec2(max(length(pa - leg * hl), abs(c0.x) - w) - RS_THICK, abs(cz));
        float dStrip = (length(max(wq, 0.0)) + min(max(wq.x, wq.y), 0.0)) / 1.1;

        // The strands: a mirror pair at rc from the axis, turning round it twice as fast as the strip.
        float rc = RS_BRANCH * smoothstep(0.3, 1.0, u);
        float th = RS_STRAND * gRsLine * smoothstep(0.3, 0.6, u);
        vec2 cr = vec2(ca * c0.x + sa * c0.y, ca * c0.y - sa * c0.x);
        cr.x = abs(cr.x);
        vec2 sq = cr - vec2(rc, 0.0);
        float dStrand = (length(vec3(sq, cz)) - th) / 1.2;
        // The threads: the same again round each strand, at 0.4 of its reach and twice its turn.
        float c2 = ca * ca - sa * sa;
        float s2 = 2.0 * sa * ca;
        vec2 tq = vec2(c2 * sq.x + s2 * sq.y, c2 * sq.y - s2 * sq.x);
        tq.x = abs(tq.x);
        float dThread = (length(vec3(tq - vec2(0.4 * rc, 0.0), cz)) - 0.5 * th) / 1.4;

        float dj = min(dStrip, min(dStrand, dThread));
        if (dj < d) {
            d = dj;
            gHitRib = float(ri);
            gHitU = u;
            gHitPart = dStrip <= min(dStrand, dThread) ? 0.0 : (dStrand <= dThread ? 1.0 : 2.0);
            gHitEdge = clamp(abs(c0.x) / max(w, 1e-4), 0.0, 1.0);
        }
    }
    return d;
}

/** The field's gradient at p, unit length; straight back along the ray where the field is flat. */
vec3 rsNormal(vec3 p, vec3 rd, float eps) {
    vec2 e = vec2(1.0, -1.0) * eps;
    vec3 g = e.xyy * map(p + e.xyy) + e.yyx * map(p + e.yyx) + e.yxy * map(p + e.yxy) + e.xxx * map(p + e.xxx);
    float l = length(g);
    return l > 1.0e-9 ? g / l : -rd;
}

float rsDither() {
    vec2 f = gl_FragCoord.xy;
    return fract(52.9829189 * fract(dot(f, vec2(0.06711056, 0.00583715))));
}

/** The sky: a deep gradient, a faint nebula, and the core's glow, which the braid fills. */
vec3 rsSky(vec3 ro, vec3 rd) {
    vec3 bg = mix(rsCol(gRsTone + 0.62) * 0.045, rsCol(gRsTone + 0.78) * 0.012, smoothstep(-0.6, 0.8, rd.y));
    float neb = motionFbm(rd.xy * 2.2 + vec2(rd.z * 1.7, -rd.z * 1.3) + 0.3 * sin(uTime * RS_RATE(10)));
    bg += rsCol(gRsTone + 0.7 + 0.2 * neb) * 0.05 * neb * neb;
    float toCore = pow(max(dot(rd, normalize(-ro)), 0.0), 24.0);
    return bg + rsCol(gRsTone + 0.1) * toCore * (0.05 + 0.25 * gBraid) * gRsGlow;
}

/** The light along one ray: the segments culled to the ray, sphere-traced and shaded once, then every ribbon's halo. */
vec3 rsTrace(vec3 ro, vec3 rd) {
    float hD[RS_RIBBONS];
    float hT[RS_RIBBONS];
    float hU[RS_RIBBONS];
    float twist = RS_PI * 1.4 / float(RS_SEGS);
    gCount = 0;
    for (int i = 0; i < RS_RIBBONS; i++) {
        float fi = float(i);
        float ph = fi * (RS_TAU / 6.0);
        int fam = i - 3 * (i / 3);
        // Whole cycles per uTime wrap and whole-number frequencies: the head wraps exactly.
        float head = mod(uTime * RS_RATE(340 + 50 * i) + fi * 2.3, RS_TAU);
        float ang = fi * 1.7 + uTime * RS_RATE(678);
        vec3 a = ribbonPoint(head - RS_TAIL, ph, fi, fam);
        vec3 nrm = vec3(0.0);
        float bD = 1e9;
        float bT = 0.0;
        float bU = 0.0;
        for (int k = 0; k < RS_SEGS; k++) {
            float u1 = float(k + 1) / float(RS_SEGS);
            vec3 b = ribbonPoint(head - RS_TAIL * (1.0 - u1), ph, fi, fam);
            vec3 ba = b - a;
            float len = length(ba);
            vec3 T = len > 1e-5 ? ba / len : vec3(0.0, 1.0, 0.0);
            // The frame: the first segment's normal points away from the core, and each next
            // one is the last carried over (parallel transport).
            vec3 ref = k == 0 ? a + b : nrm;
            vec3 nn = ref - T * dot(ref, T);
            float nl = length(nn);
            if (nl < 1e-4) {
                nn = cross(T, abs(T.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0));
                nl = length(nn);
            }
            nrm = nn / max(nl, 1e-6);
            float dAng = min(twist, RS_TWIST_MAX * len);
            vec3 hit = raySegment(ro, rd, a, b);
            if (hit.x < bD) {
                bD = hit.x;
                bT = hit.y;
                bU = (float(k) + hit.z) / float(RS_SEGS);
            }
            if (hit.x < RS_CULL && gCount < RS_KEEP) {
                gA[gCount] = vec4(a, float(i * RS_SEGS + k));
                gB[gCount] = vec4(b, ang);
                gF[gCount] = vec4(nrm, dAng);
                gCount++;
            }
            ang += dAng;
            a = b;
        }
        hD[i] = bD;
        hT[i] = bT;
        hU[i] = bU;
    }

    vec3 col = rsSky(ro, rd);
    float n = RS_STEPS;
    float hitT = -1.0;
    float bOff = dot(ro, rd);
    float bDisc = bOff * bOff - (dot(ro, ro) - RS_BOUND * RS_BOUND);
    if (bDisc > 0.0 && gCount > 0) {
        float bRoot = sqrt(bDisc);
        float t = max(-bOff - bRoot, 0.0) + 0.02 * rsDither();
        float tEnd = -bOff + bRoot;
        for (int i = 0; i < RS_MAX_STEPS; i++) {
            if (float(i) >= n || t > tEnd) break;
            float d = map(ro + rd * t);
            float eps = 0.0008 * (1.0 + t);
            if (d < eps) {
                hitT = t;
                break;
            }
            t += max(d * 0.9, 0.25 * eps);
        }
    }

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 nor = rsNormal(p, rd, 0.001 * (1.0 + hitT));
        // Restore the hit's globals after the normal's taps.
        map(p);
        float fi = float(clamp(int(gHitRib + 0.5), 0, RS_RIBBONS - 1));
        float part = min(gHitPart, 1.0);
        vec3 c = rsCol(gRsTone + fi * 0.17 + 0.25 * gHitU + 0.1 * gHitPart);
        // Lit by the core on both faces of the silk, falling off with distance from it.
        vec3 L = -p / max(length(p), 1e-3);
        float fall = 1.0 / (1.0 + 0.3 * dot(p, p));
        float ndv = abs(dot(nor, rd));
        float rim = (1.0 - ndv) * (1.0 - ndv) * (1.0 - ndv);
        vec3 silk = c * (0.18 + 1.2 * abs(dot(nor, L)) * fall) + rsCol(gRsTone + fi * 0.17 + 0.45) * rim * 0.6;
        // The strip's edges and the strands glow by themselves, like neon thread.
        silk += c * (smoothstep(0.7, 1.0, gHitEdge) * (1.0 - part) * 0.8 + part * 0.6) * gRsGlow;
        silk += mix(vec3(1.0), c, 0.4) * pow(max(dot(reflect(rd, nor), L), 0.0), 24.0) * fall * 0.5;
        col = mix(col, silk, 0.3 + 0.7 * smoothstep(0.0, 0.35, gHitU));
    }

    // Every ribbon's halo, from its closest approach to the ray: dimmer behind the silk the ray hit,
    // out to nothing right at the eye, and dimmer on the far side of the core.
    float eyeR = length(ro);
    for (int i = 0; i < RS_RIBBONS; i++) {
        float fi = float(i);
        float hw = ribbonWidth(fi) * (0.15 + 0.85 * smoothstep(0.0, 0.55, hU[i])) * 3.0 + 0.04;
        float halo = hw * hw / (hD[i] * hD[i] + hw * hw);
        float fade = smoothstep(0.6, 2.2, hT[i]) * exp(-max(hT[i] - eyeR, 0.0) * 0.12) * smoothstep(0.0, 0.35, hU[i]);
        float occl = hitT < 0.0 || hT[i] < hitT ? 1.0 : 0.3;
        col += rsCol(gRsTone + fi * 0.17 + 0.25 * hU[i]) * halo * 0.14 * gRsGlow * fade * occl;
    }
    return col;
}

/** `exposure` is the style's own; Intensity and PULSE scale the emission. */
vec4 rsFinish(vec3 col, float exposure) {
    col *= exposure * neonGlow() * (1.0 + 0.5 * neonKnob(0));
    col = max(mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, 1.2), 0.0);
    col = grade(neonAces(col));
    // No pixel leaves as a NaN or an infinity: the feedback pass would carry it for good.
    if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);
    return vec4(col, 1.0);
}

void main() {
    vec2 uv = view();
    rsSetup();
    vec3 ro;
    vec3 rd;
    camAround(uv, mix(RS_EYE_MIN, RS_EYE_MAX, 0.5 + neonKnob(5)), 4.0 * CAM_SPIN_UNIT, ro, rd);
    fragColor = rsFinish(rsTrace(ro, rd), 1.2);
}
