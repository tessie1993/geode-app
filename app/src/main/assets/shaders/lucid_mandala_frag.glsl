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
//#include lib_lucid

// Lucid Mandala: a flight down the bore of a jewelled mandala. The flat style
// folded one wedge of a simplex nest around the screen centre; here the fold
// is around the z axis, so the space the eye flies through is cut into
// mirrored wedges and everything in one wedge repeats in every other. The bore
// is a faceted prism of those wedges, and the family's simplex nest (four
// levels deep, as before) is painted on its faces as the stone the mandala is
// carved from. Every 4 units along the bore a rosette of cut jewels stands in
// the air, two rings of them per wedge (one on the wedge's centre line, one on
// its mirror line, so they interleave like a mandala's petals). The jewels are
// born at the heart (the axis), stream outward along the radius growing as
// they go, and dissolve again at the rim: the old "born at its heart,
// dissolving at the rim" stream, now with depth. Successive rosettes are
// phase-shifted, so down the bore the rings read as a slow spiral. The far
// end of the bore is a soft glow: the mandala's heart, seen from inside.
//
// The camera is the shared rig (camFly): the eye rides the rig's path down +z
// near the axis, and the rig's wander off it is what turns the fold into
// perspective. The whole bore twists once per 64 world units; the rosette
// cells are 4 long; the face pattern reads z through a mirrored triangle wave
// of period 128 (a crease every 64 units, which on a mandala reads as one more
// mirror line), so the eye is rebased by 128, which divides the rig's 8192
// travel wrap: the wrap and the rebase are both invisible.
//
// ---- audio ------------------------------------------------------------------
//
// Hats (SND_HAT_ENV) add FOLD SEGMENTS, as in the flat style: 8 wedges at rest,
// growing toward 12 as the hats get busy. The fold count is an integer, so the
// geometry is cross-faded between the two neighbouring even counts by the
// fractional part: arms split and merge instead of popping. uIntensity scales
// how far the hats reach. The slow accumulated turn (uDrift) spins the whole
// bore. sndShaped(SND_MIDDLE) sharpens the jewel contrast. Kick (SND_KICK_ENV)
// swells the jewels a little (k0 PULSE scales it, eased by the envelope).
// Brightness follows SND_LEVEL gently.
//
// ---- macros and knobs -------------------------------------------------------
//
// Speed is in uTime and uFlowTime (the outward stream). Complexity: uMorph adds
// up to 4 wedges and, past 0.5, a nest level and an octave (lucidDetail).
// Chaos: uTurbulence/uWarp raise the nest's warp strength and jitter it
// (lucidChaos, lucidJitter). Kaleido: when uKaleido is on, uSymmetry replaces
// the 8-wedge rest count (lucidView(false): the user's fold is this style's
// fold, never applied twice); the bore keeps at least 4 faces. Color: pal()
// picks every stone. Space: uZoom/uRotation/uTwist through lucidView, as a
// lens on the rays. Detail (uSteps) bounds the march and trims the nest.
// Knobs (offsets from 0.5): k0 PULSE kick swell, k1 TEXTURE nest scale on the
// faces, k2 FLOW how far each rosette's ring phase leads the last (the spiral),
// k3 TONE hue, k4 BUILD facet glints, k5 SPACE bore width.
//
// ---- why the distance estimate is safe --------------------------------------
//
// The angular fold is a piecewise isometry and the fold count is an integer,
// so a folded field is exact wherever each wedge's contents stay inside the
// wedge: every jewel's size is capped by its centre's distance to the nearest
// mirror line (rad * sin(seg / 2)), so none crosses into a neighbour. The bore
// face in a folded wedge is a plane, exact. A jewel is an octahedron cut by a
// slab (an intersection, a lower bound). Each cell measures only its own
// rosette, clamped to the cell boundary plus the gap no jewel can cross. The
// twist rotates xy by an angle linear in z; its Lipschitz bound
// sqrt(1 + (r_max * twist)^2), with r_max the prism's corner, divides the
// field. The fold cross-fade mixes two 1-Lipschitz fields, which stays
// 1-Lipschitz. A sphere around the eye is carved out of everything.
//
// The nest itself is never marched: it is read once per pixel, at the hit,
// exactly as often as the flat style read it.

#define LM_MAX_STEPS 96
#define LM_FAR 36.0
#define LM_TAU 6.2831853
/** The eye's z rebase: the face pattern's mirror period, divides 8192. */
#define LM_ZWRAP 128.0
/** One full turn of the bore per 64 units (divides LM_ZWRAP). */
#define LM_TWIST (6.2831853 / 64.0)
/** Rosette cell length along z (divides LM_ZWRAP). */
#define LM_CELL 4.0
/** Base apothem of the bore. */
#define LM_R 4.4
/** The largest jewel's reach from its centre, with margin. */
#define LM_GEM_MAX 0.95
#define LM_BUBBLE 0.6

float gSegLo;
float gSegHi;
float gBlend;
float gR;
float gGem;
float gSpin;
float gLead;
float gStream;
float gLip;
vec3 gEye;
/** Set by map(): 1 on a jewel, 0 on the bore face. */
float gMat;
/** Set by map(): the jewel's ring phase (0 at the heart, 1 at the rim) and its hash. */
float gPh;
float gGemHash;

float lmKnob(int i) { return clamp(uKnob[i], 0.0, 1.0) - 0.5; }

/** Twist and fold `p` into one wedge of `seg` radians: x along the wedge's centre line, y across it. */
vec3 lmFold(vec3 p, float seg) {
    float r = length(p.xy);
    // atan(0, 0) is undefined in GLSL ES; the fold is continuous at r = 0 anyway.
    float a = r > 1e-5 ? atan(p.y, p.x) : 0.0;
    a += p.z * LM_TWIST + gSpin;
    a = abs(mod(a + 0.5 * seg, seg) - 0.5 * seg);
    return vec3(r * cos(a), r * sin(a), p.z);
}

/** One jewel at (rad, ang) in the wedge frame, ring phase ph. */
float lmJewel(vec3 q, float lz, float rad, float ang, float ph, float halfSeg, float h) {
    vec3 s = vec3(q.x - rad * cos(ang), q.y - rad * sin(ang), lz);
    float sa = gSpin * 3.0 + h * LM_TAU;
    s.xy = mat2(cos(sa), -sin(sa), sin(sa), cos(sa)) * s.xy;
    // Born at the heart, full mid-way, dissolved at the rim; never wider than
    // its wedge (the distance from its centre to the nearest mirror line).
    float size = gGem * sin(3.14159265 * ph);
    size = min(size, min(0.9 * rad * sin(halfSeg), 0.8 * (gR - rad)));
    size = max(size, 0.0);
    return max(sdOctahedron(s, size), abs(s.z) - 0.42 * size);
}

/** The bore of `seg`-radian wedges at `p`. Writes the material globals. */
float lmBore(vec3 p, float seg, out float mat, out float ph, out float gh) {
    vec3 q = lmFold(p, seg);
    float wall = gR - q.x;

    float cz = floor(q.z / LM_CELL);
    float lz = q.z - (cz + 0.5) * LM_CELL;
    float ci = mod(cz, LM_ZWRAP / LM_CELL);
    float h = uhashUnit(uhash(uint(ci) + 0x9e1u));
    // Each rosette's rings lead the last by gLead, so the bore reads as a spiral.
    float base = gStream + ci * gLead + h * 0.25;
    float phA = fract(base);
    float phB = fract(base + 0.5);
    float rIn = 0.3;
    float rOut = gR * 0.86;
    float halfSeg = 0.5 * seg;
    float ja = lmJewel(q, lz, mix(rIn, rOut, phA), 0.0, phA, halfSeg, h);
    float jb = lmJewel(q, lz, mix(rIn, rOut, phB), halfSeg, phB, halfSeg, h + 0.37);
    float gem = min(ja, jb);
    // A neighbour cell's jewel is at least the boundary distance plus the gap away.
    gem = min(gem, 0.5 * LM_CELL - abs(lz) + (0.5 * LM_CELL - LM_GEM_MAX));

    mat = gem < wall ? 1.0 : 0.0;
    ph = ja < jb ? phA : phB;
    gh = ja < jb ? h : fract(h + 0.37);
    return min(wall, gem);
}

float map(vec3 p) {
    float m;
    float ph;
    float gh;
    // gBlend is uniform, so these branches are coherent; the second bore is
    // only measured while the fold sits between two even counts.
    float d = lmBore(p, gBlend < 0.001 ? gSegLo : gSegHi, m, ph, gh);
    if (gBlend >= 0.001 && gBlend < 0.999) {
        float m0;
        float ph0;
        float gh0;
        float d0 = lmBore(p, gSegLo, m0, ph0, gh0);
        if (gBlend < 0.5) {
            ph = ph0;
            gh = gh0;
        }
        d = mix(d0, d, gBlend);
        m = mix(m0, m, gBlend);
    }
    gMat = m;
    gPh = ph;
    gGemHash = gh;
    return max(d / gLip, LM_BUBBLE - length(p - gEye));
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

void main() {
    vec2 uv = lucidView(false);
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)
    float flow = uFlowTime;
    float hat = clamp(SND_HAT_ENV, 0.0, 1.0) * uIntensity;
    float mid = clamp(sndShaped(SND_MIDDLE), 0.0, 1.5);
    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    // Eased: the swell rises with the envelope's fall, not on the hit's frame.
    float kickSwell = lucidBump(kick) * clamp(0.5 + lmKnob(0), 0.0, 1.0);

    int depth = lucidDetail(4, 3);
    int oct = lucidDetail(3, 2);

    // The fold count, cross-faded between the two neighbouring even counts.
    float rest = (uKaleido > 0.5 && uSymmetry >= 2.0) ? uSymmetry : 8.0;
    float segs = max(rest + 4.0 * hat + 4.0 * clamp(uMorph, 0.0, 1.0), 4.0);
    float lo = 2.0 * floor(segs * 0.5);
    float f = (segs - lo) * 0.5;
    gBlend = f * f * (3.0 - 2.0 * f);
    gSegLo = LM_TAU / lo;
    gSegHi = LM_TAU / (lo + 2.0);

    // uDrift is an accumulated angle: only the fixed 0.35 scales it (lib_scene_motion).
    gSpin = uDrift * 0.35 + t * 0.02;
    gR = max(3.4, LM_R + 1.4 * lmKnob(5));
    gGem = min(0.6 + 0.25 * kickSwell, 0.9);
    gStream = flow * 0.06 + t * 0.01;
    gLead = 0.11 + 0.18 * lmKnob(2);
    // The twist's Lipschitz bound at the prism's corner (apothem / cos(pi / 4)
    // for the fewest faces the bore allows, 4).
    float rMax = gR * 1.4142136;
    gLip = sqrt(1.0 + rMax * rMax * LM_TWIST * LM_TWIST);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term has a period dividing 128, so this rebase is exact.
    ro.z = mod(ro.z, LM_ZWRAP);
    gEye = ro;

    float tm = 0.03;
    float hitT = -1.0;
    float used = 0.0;
    float mat = 0.0;
    float ph = 0.0;
    float gh = 0.0;
    for (int i = 0; i < LM_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (tm > LM_FAR) break;
        used = float(i);
        float d = map(ro + rd * tm);
        float eps = 0.0008 * tm + 0.0005;
        if (d < eps) {
            hitT = tm;
            mat = gMat;
            ph = gPh;
            gh = gGemHash;
            break;
        }
        tm += max(d * 0.9, eps);
    }

    float hue = 0.03 * sin(t * 0.019) + 0.3 * lmKnob(3);
    float fogK = 0.055;
    // The far end of the bore is the mandala's heart, glowing softly.
    float ahead = pow(max(rd.z, 0.0), 10.0);
    vec3 fogCol = pal(hue + 0.6) * (0.03 + (0.22 + 0.2 * swell) * ahead) + vec3(0.004, 0.003, 0.007);
    vec3 col = fogCol;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.0012 * hitT));
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.2, 0.25, 1.0);
        float spec = pow(max(dot(reflect(rd, n), -rd), 0.0), 48.0);
        float build = clamp(0.5 + lmKnob(4), 0.0, 1.0);
        float px = hitT * 2.0 / (max(uCamFocal, 0.1) * uResolution.y);
        float r = length(p.xy) / gR;

        vec3 surf;
        if (mat < 0.5) {
            // The bore face: the flat style's jewel field, read once. The face's
            // own frame is (across the wedge, along the bore); z goes through a
            // mirrored triangle wave of period 128 so the rebase lands on it.
            vec3 qa = lmFold(p, gSegLo);
            vec3 qb = lmFold(p, gSegHi);
            vec3 q = mix(qa, qb, gBlend);
            float zz = 64.0 * abs(2.0 * fract(p.z / LM_ZWRAP) - 1.0);
            float dens = 1.0 + 0.6 * lmKnob(1);
            vec2 fq = vec2(zz * 0.55 - flow * 0.35 - t * 0.05, q.y * 1.1) * dens;
            fq += lucidJitter(fq, t);
            vec4 nst = lucidNest(fq, depth, 1.6 * lucidChaos(), t * 0.25, LUCID_SIMPLEX, oct);

            float fh = 0.5 + 0.5 * nst.z;
            float ring = 0.5 + 0.5 * cos(zz * 1.2 - flow * 0.8 + nst.x * 2.0);
            vec3 stone = pal(fh * 0.8 + 0.15 * ring + hue);
            surf = stone * (0.2 + 0.85 * smoothstep(0.15, 0.95, fh)) * (0.45 + 0.55 * ao);
            // Facet glints along the creases of the nest.
            surf += pal(fh + 0.5 + hue) * smoothstep(0.55, 1.1, nst.w) * (0.3 + 0.4 * build);
            // The mirror joints: the prism's corners, where two faces meet.
            float seg = mix(gSegLo, gSegHi, gBlend);
            float joint = 1.0 - smoothstep(0.0, 0.03 + 1.5 * px, abs(gR * tan(0.5 * seg) - q.y));
            surf += pal(hue + 0.55) * joint * (0.25 + 0.35 * swell);
            surf += fogCol * fres + vec3(spec) * 0.15 * ao;
            surf = lucidChannelTurn(surf, 1.4 * r - t * 0.03);
        } else {
            // A cut jewel: a palette hue per stone, facets steered by the normal,
            // a dispersed fake refraction (each channel at its own offset) and a
            // hard glint. The ring phase picks the setting, as the radius did.
            float gHue = hue + gh * 0.8 + 0.15 * ph;
            vec3 body = pal(gHue + 0.25 * dot(n, vec3(0.35, 0.5, 0.2)));
            float diff = clamp(dot(n, normalize(vec3(0.0, 0.4, 1.0))) * 0.5 + 0.5, 0.0, 1.0);
            float film = gHue + 1.3 * (1.0 - ndv);
            vec3 thin = vec3(pal(film).r, pal(film + 0.13).g, pal(film + 0.26).b);
            surf = body * (0.1 + 0.6 * diff) * ao
                 + thin * (0.15 + 0.7 * fres)
                 + vec3(spec) * (0.35 + 0.65 * build)
                 + body * 0.25 * kickSwell;
            surf = lucidChannelTurn(surf, 1.4 * r - t * 0.03);
        }
        surf = lucidCubic(surf, 0.55 + 0.3 * clamp(mid, 0.0, 1.0));
        col = mix(surf, fogCol, 1.0 - exp(-hitT * fogK));
    }

    col *= 0.85 + 0.12 * swell;
    col *= 0.75 + 0.25 * (1.0 - smoothstep(0.5, 2.0, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
