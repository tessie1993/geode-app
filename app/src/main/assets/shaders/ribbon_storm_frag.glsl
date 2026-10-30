#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3

// Ribbon Storm: six glowing silk ribbons swirling around a dark core, circled
// by the eye.
//
// There is no march. Each ribbon is a comet: a head running round a closed
// curve, with a tail RS_TAIL of curve parameter behind it, cut into RS_SEGS
// straight segments. Every pixel measures the ray's exact closest approach to
// each segment (48 in all), keeps the nearest per ribbon, and turns that
// distance into a core and a soft halo. Taking the nearest rather than
// summing per segment is what keeps the joints from beading. The halos of
// all six do add, which is the volumetric glow.
//
// A ribbon is flat silk, not a tube: its width follows |cos| of a twist phase
// that winds along its length, so it narrows and brightens where it turns
// edge-on and widens and dims where it faces the eye.
//
// The camera is the shared rig (camAround). Every curve has whole-number
// frequencies in its parameter, so the head phase is wrapped to one turn
// exactly. The widest the ribbons reach (1.48 * RS_RADIUS * 1.38 = 3.7) stays
// well inside the closest the eye comes (RS_EYE * 0.65 = 5.2), and the glow
// fades to nothing right at the eye.
//
// ---- audio ------------------------------------------------------------------
//
// Each sound moves a different thing:
//
// - The six band regions sub..presence (uBandsLo, uBandsHi.xy) are one per
//   ribbon, and set its width and brightness.
// - Kick (uHits.x) whips every ribbon outward: the curve's radius grows, the
//   heads further than the tails.
// - Snare (uHits.y) spins every ribbon a full turn about its own length as the
//   envelope falls (the turn starts where the last one ended, so it does not
//   jump), winds the twist tighter, and flashes the ribbon edges white.
// - Hats (uHits.z) light sparkles that ride along the ribbon edges.
// - An onset (uHits.w) re-colours one ribbon, picked by uHitCount: each hit
//   on a ribbon turns its hue by the golden ratio, sliding there over the
//   envelope, and the new colour holds.
// - A drop (uEvents.y) braids all six into one tight vortex through the core
//   and swaps to the complementary palette, unwinding over its 2.5 s.
// - A pitch change (uEvents.w) rotates the hue.
// - The section count (uSectionCount) picks the curve family: Lissajous,
//   helix or torus knot. A section change (uEvents.z) morphs from the old
//   family to the new over 1.5 s.
//
// Overall level rides uSwell and uEnergySmooth.

#define RS_RIBBONS 6
#define RS_SEGS 8
#define RS_TAU 6.2831853
#define RS_PI 3.14159265
/** camAround radius. The dolly pulls the eye in to 0.65 of this. */
#define RS_EYE 8.0
#define RS_RADIUS 1.8
/** Curve parameter behind each head that its tail covers, radians. */
#define RS_TAIL 1.7

/** This section's curve family, the one it is leaving, and how much of the old one is left. */
int gFam;
int gFamOld;
float gFamMix;
/** 0..1, how far the drop has pulled the ribbons into the vortex. */
float gBraid;
float gKick;

/** The band region ribbon fi follows: sub, bass, low-mid, mid, high-mid, presence. */
float ribbonBand(float fi) {
    vec4 lo = vec4(equal(vec4(fi), vec4(0.0, 1.0, 2.0, 3.0)));
    vec2 hi = vec2(equal(vec2(fi), vec2(4.0, 5.0)));
    return dot(lo, uBandsLo) + dot(hi, uBandsHi.xy);
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
    // A (2,3) torus knot; each ribbon's copy is turned about y and tipped, so
    // the six read as a cage rather than one knot drawn six times.
    float rr = 0.66 + 0.34 * cos(3.0 * th);
    vec3 k = vec3(rr * cos(2.0 * th), 0.5 * sin(3.0 * th), rr * sin(2.0 * th));
    k.yz = rot2(0.45 * sin(ph)) * k.yz;
    k.xz = rot2(0.5 * ph) * k.xz;
    return k * 1.15;
}

/**
 * The drop's braid. Six strands spaced round one axis, their radius rocking
 * in and out with 3 * ph, so neighbouring strands swing opposite ways and
 * weave over and under each other.
 */
vec3 vortexPoint(float th, float ph) {
    float a = 4.0 * th + ph;
    float rr = 0.42 + 0.22 * sin(2.0 * th + 3.0 * ph);
    return vec3(rr * cos(a), 1.25 * sin(th), rr * sin(a));
}

/** World position of a ribbon at curve parameter th; u is 0 at the tail, 1 at the head. */
vec3 ribbonPoint(float th, float ph, float u) {
    vec3 p = familyPoint(gFam, th, ph);
    if (gFamMix > 0.001) p = mix(p, familyPoint(gFamOld, th, ph), gFamMix);
    if (gBraid > 0.001) p = mix(p, vortexPoint(th, ph), gBraid);
    // The kick whip: the head flies out further than the tail.
    return p * RS_RADIUS * (1.0 + 0.38 * gKick * (0.35 + 0.65 * u));
}

/**
 * Closest approach between the ray ro + rd*t (t >= 0, rd unit) and segment
 * a..b. Returns (distance, t, s), s the position along the segment, 0..1.
 *
 * Solved jointly, then s re-solved for the t clamped to the front of the
 * eye, so a segment behind the camera is measured from the eye itself.
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

void main() {
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    camAround(uv, RS_EYE, 4.0 * CAM_SPIN_UNIT, ro, rd);

    float swell = clamp(uSwell, 0.0, 1.5);
    float energy = clamp(uEnergySmooth, 0.0, 1.5);
    gKick = clamp(uHits.x, 0.0, 1.0);
    float snare = clamp(uHits.y, 0.0, 1.0);
    float hats = clamp(uHits.z, 0.0, 1.0);
    float onset = clamp(uHits.w, 0.0, 1.0);
    float drop = clamp(uEvents.y, 0.0, 1.0);
    float pitch = clamp(uEvents.w, 0.0, 1.0);

    gBraid = smoothstep(0.0, 0.55, drop);
    float swap = smoothstep(0.05, 0.6, drop);
    float sec = floor(uSectionCount + 0.5);
    gFam = int(mod(sec, 3.0));
    gFamOld = int(mod(sec + 2.0, 3.0));
    gFamMix = smoothstep(0.0, 1.0, clamp(uEvents.z, 0.0, 1.0));
    float hits = floor(uHitCount + 0.5);
    // The k-th onset lands on ribbon k mod 6.
    float chosen = mod(hits, 6.0);
    float hueBase = 0.2 * pitch;

    // The snare spin. At rest this is a whole turn, which is the same as none;
    // a hit drops it to zero (the same angle again) and the falling envelope
    // winds it back up through one full turn. Squared, so a hit that lands
    // while the last is still decaying jumps by only the square of what was left.
    float spin = RS_TAU * (1.0 - snare * snare);
    float twistRate = RS_PI * (1.4 + 3.0 * snare);

    vec3 col = vec3(0.0);
    for (int i = 0; i < RS_RIBBONS; i++) {
        float fi = float(i);
        float ph = fi * (RS_TAU / 6.0);
        float band = clamp(ribbonBand(fi), 0.0, 1.0);
        // Whole-number frequencies in every family: one turn of th is exact.
        float head = mod(uFlowPhase * (0.30 + 0.045 * fi) + fi * 2.3, RS_TAU);
        float wBase = 0.07 + 0.13 * band;
        float spinPh = spin + fi * 1.7 + uTime * 0.6;

        float bestKey = 1e9;
        float bestD = 1e9;
        float bestT = 0.0;
        float bestU = 0.0;
        float bestFace = 1.0;
        float bestW = wBase;
        vec3 a = ribbonPoint(head - RS_TAIL, ph, 0.0);
        for (int k = 1; k <= RS_SEGS; k++) {
            float u1 = float(k) / float(RS_SEGS);
            vec3 b = ribbonPoint(head - RS_TAIL * (1.0 - u1), ph, u1);
            vec3 hit = raySegment(ro, rd, a, b);
            float u = (float(k - 1) + hit.z) / float(RS_SEGS);
            float face = abs(cos(spinPh + u * twistRate));
            // Tapers to a thread at the tail; turns edge-on with the twist.
            float w = wBase * (0.15 + 0.85 * smoothstep(0.0, 0.55, u)) * (0.2 + 0.8 * face);
            float key = hit.x / w;
            if (key < bestKey) {
                bestKey = key;
                bestD = hit.x;
                bestT = hit.y;
                bestU = u;
                bestFace = face;
                bestW = w;
            }
            a = b;
        }

        float core = exp(-bestKey * bestKey * 1.4);
        float haloW = bestW * 4.0 + 0.04;
        float halo = haloW * haloW / (bestD * bestD + haloW * haloW);
        // Edge-on silk catches the light: thinner there, and brighter.
        float sheen = 1.0 / (0.45 + 0.55 * bestFace);
        // Out to nothing right at the eye, dimmer on the far side of the core.
        float fade = smoothstep(0.6, 2.2, bestT) * exp(-max(bestT - RS_EYE, 0.0) * 0.12);
        float tail = smoothstep(0.0, 0.35, bestU);

        // Onsets that have landed on this ribbon so far: the k in 1..hits with
        // k mod 6 == fi. The newest slides in over the onset envelope.
        float onsetHits = floor((hits + 6.0 - fi) / 6.0) - 1.0;
        float isChosen = 1.0 - step(0.5, abs(chosen - fi));
        float hue = hueBase + fi * 0.13 + 0.618 * (onsetHits - onset * isChosen) + 0.12 * bestU;
        vec3 rc = mix(pal(hue), pal(hue + 0.5), swap);

        float bright = 0.4 + 1.1 * band;
        col += rc * (core * bright * sheen * 0.9 + halo * (0.10 + 0.18 * band)) * fade * tail;

        // The snare's white edges: the rim of the ribbon, not its body.
        float e = (bestKey - 1.0) / 0.28;
        col += vec3(1.0) * exp(-e * e) * snare * 0.7 * fade * tail;

        // Hat sparkles. The slots are counted from the head, so they ride
        // along with the ribbon instead of sitting still in space.
        float slot = bestU * 30.0;
        float hs = hashCell(ivec3(i, int(floor(slot)), 41));
        float along = smoothstep(0.45, 0.05, abs(fract(slot) - 0.5));
        float tw = 0.5 + 0.5 * sin(uTime * (11.0 + 13.0 * hs) + hs * 50.0);
        float es = (bestKey - 0.85) / 0.4;
        col += mix(vec3(1.0), rc, 0.25) * hats * step(0.5, hs) * exp(-es * es) * along * tw * 1.2 * fade;
    }

    // The sky: a deep gradient, a faint nebula, and the core's glow, which the
    // drop's vortex fills.
    vec3 bg = mix(pal(hueBase + 0.62) * 0.045, pal(hueBase + 0.78) * 0.012, smoothstep(-0.6, 0.8, rd.y));
    float neb = motionFbm(rd.xy * 2.2 + vec2(rd.z * 1.7, -rd.z * 1.3) + uFlowPhase * 0.015);
    bg += pal(hueBase + 0.7 + 0.2 * neb) * 0.05 * neb * neb;
    float toCore = pow(max(dot(rd, normalize(-ro)), 0.0), 24.0);
    bg += mix(pal(hueBase + 0.1), pal(hueBase + 0.6), swap) * toCore * (0.05 + 0.25 * gBraid + 0.06 * swell);
    col += bg;

    col *= 0.80 + 0.30 * swell + 0.10 * energy;
    // A soft shoulder: six cores crossing in one pixel would otherwise clip flat.
    col = 1.0 - exp(-1.25 * col);
    col *= 0.7 + 0.3 * smoothstep(2.2, 0.5, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
