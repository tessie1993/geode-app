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

// Lucid Ink: suminagashi, Japanese floating-ink marbling, now a bath you glide
// over. A wide, still tray of water lies below the camera under a paper-
// coloured sky; on its surface floats one colour of ink, marbled exactly as
// the 2D style marbled the plane: combed by a rake of sine teeth, then read
// through the family's deepest nest (five levels of smooth simplex warp, three
// on a low Detail budget) and a final ridged read whose creases nest like the
// concentric drops of a real bath. The rake is now there in body: a row of
// lacquered stylus tines stands out of the water every 32 units, each at a
// crest of the comb, so the ink visibly bends forward past every tine, and a
// faint ring of ripples spreads from each tine's foot.
//
// Still the only Lucid style on a light ground. The water mirrors the paper
// sky (Fresnel, brighter toward grazing), the ink film is matte and absorbs
// it, and the fog is paper too, so the bath dissolves into the page toward
// the horizon. The recolour is the 2D one: dark ink lines where the ridges
// crest, a paler wash where they spread, the palette only as a tint.
//
// ---- camera and the wrap -----------------------------------------------------
//
// The shared rig flies above the bath (camFly). The water sits 3.4 units
// below the path (the rig's paths wander at most about 2 below it) and the
// tines stop 0.8 above the water, so the eye never meets either; a small
// sphere round the eye is carved out of everything as well. The eye is
// rebased onto a 32-unit grid (ro.z = uCamPos.z - 32 floor(uCamPos.z / 32)):
// the swell has z rates that are whole multiples of 2 pi / 16 and the tine
// rows repeat every 32, so the march is exact across the rebase and z stays
// precise. The marbling needs a longer period, or the bath would visibly
// repeat: it is read on an annulus in the nest's plane, one turn per 512
// world units (theta = 2 pi z / 512, radius R + x), with z taken as
// mod(rebase, 512) + local z. 512 divides the rig's 8192 travel wrap, so
// neither the rebase nor the wrap moves a single ink line. The comb's
// along-z wave is a whole number (64) of cycles per turn for the same reason.
//
// ---- audio ------------------------------------------------------------------
//
// As in the 2D style, and only the ink moves; the page's brightness does not.
// Spectral FLUX (sndLevel(SND_FLUX)) is the GRAIN: busy, changing spectra make
// the pigment granulate into a fine speckle fixed to the bath (its pattern
// rides the same annulus, so flux changes how much shows, never where; mean
// zero, so the tone of the bath holds). The swell (0.5 * (SND_BASS_PRESENCE +
// SND_MID_PRESENCE)) pulls the rake deeper, bending the ink further past each
// tine, and lifts the ripple rings at the tines' feet. SND_MID_PRESENCE
// spreads the wash. SND_BASS_PRESENCE also heaves the water's swell a little,
// a smooth height, so the bath breathes on the low end.
//
// ---- macros and knobs -------------------------------------------------------
//
// Speed: uTime (already Speed-integrated) and uFlowTime, the bath's drift.
// Complexity: uMorph adds rake teeth (continuous; the tines close up) and,
// past 0.5, an octave (lucidDetail). Chaos: uTurbulence/uWarp raise warp
// strength (lucidChaos) and jitter the bath (lucidJitter); uTwist, the
// kaleidoscope, uZoom and uRotation reach the view through lucidView. Color:
// ink pal(0.6) near black, wash pal(0.3), paper tinted by pal(0.1). Energy:
// uIntensity scales the grain and the swell-driven rake.
// Knobs, all neutral at 0.5: k0 PULSE the rake's push, k1 TEXTURE the
// marbling scale, k2 FLOW the water's swell, k3 TONE the ink's hue, k4 BUILD
// how far the ink lines spread, k5 SPACE how deep the bath lies below.
//
// ---- why the distance estimate is safe --------------------------------------
//
// The water is a height field y = h(x, z): its distance (y - h) / sqrt(1 +
// |grad h|^2) is bounded below by (y - h) / (1 + max |grad h|), and h is two
// sines whose weighted gradient is at most gB * 1.0 (0.6 * |(0.37, 2u)| +
// 0.4 * |(0.23, 3u)|, u = 2 pi / 16), so dividing by 1 + gB is safe. The ink
// is colour only, never geometry. A tine is a capped cylinder, an exact
// distance, repeated by mod in x (the comb's crest spacing) and z (32):
// each copy is thin and centred in its cell, so the nearest copy is the
// right one. The eye sphere joins through max. Steps take 0.9 of the bound.

#define LI_MAX_STEPS 96
#define LI_FAR 42.0
/** Eye rebase grid: tine rows repeat every 32, the swell every 16. */
#define LI_ZWRAP 32.0
/** One turn of the marbling's annulus per 512 world units (divides 8192). */
#define LI_TEX_PERIOD 512.0
/** 2 pi / 16: the swell's z frequency unit. */
#define LI_U16 0.39269908
#define LI_TAU 6.2831853
#define LI_PI 3.14159265
/** Tine radius and how far the tines stand out of the water. */
#define LI_TR 0.055
#define LI_TINE_H 0.8
#define LI_BUBBLE 0.5

float gWY;
float gB;
float gPh;
float gLip;
/** Nest units per world unit, the comb's teeth and its sideways phase. */
float gS;
float gTeeth;
float gTinePh;
vec3 gEye;

/** The bath's swell height at (x, z). z rates are 2 and 3 times LI_U16. */
float waterH(vec2 xz) {
    return gWY + gB * (0.6 * sin(0.37 * xz.x + 2.0 * LI_U16 * xz.y + gPh)
                     + 0.4 * sin(-0.23 * xz.x + 3.0 * LI_U16 * xz.y - 0.7 * gPh + 1.3));
}

/** The nearest tine's horizontal offset (x, z): x at the comb's crests, z on rows every 32. */
vec2 tineOffset(vec3 p) {
    float k = gS * gTeeth;
    float u = p.x * k + gTinePh;
    float dx = (mod(u + 0.5 * LI_PI, LI_TAU) - LI_PI) / k;
    float dz = mod(p.z + 0.5 * LI_ZWRAP, LI_ZWRAP) - 0.5 * LI_ZWRAP;
    return vec2(dx, dz);
}

float tineDist(vec3 p) {
    vec2 w = vec2(length(tineOffset(p)) - LI_TR, p.y - (gWY + LI_TINE_H));
    return min(max(w.x, w.y), 0.0) + length(max(w, 0.0));
}

/** The style's world at p: its distance, and in `found` what is there (no slots). */
float mapAt(vec3 p, inout LucidHit found) {
    float d = min((p.y - waterH(p.xz)) / gLip, tineDist(p));
    return max(d, LI_BUBBLE - length(p - gEye));
}

/** The style's world at p as a hit, for lib_lucid_field. */
LucidHit lucidMap(vec3 p) {
    LucidHit h = lucidHit(0.0);
    float d = mapAt(p, h);
    h.d = d;
    return h;
}

//#include lib_lucid_field

/** The paper sky, and the fog below the horizon, so the bath fades into the page. */
vec3 skyCol(vec3 dir, vec3 fogCol, vec3 paper, vec3 L) {
    vec3 c = mix(fogCol, paper * 1.04, smoothstep(0.0, 0.5, dir.y));
    return c + vec3(1.0, 0.95, 0.85) * 0.05 * pow(max(dot(dir, L), 0.0), 8.0);
}

void main() {
    LucidHit fld = lucidHit(0.0);
    vec2 uv = lucidView(true);
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)
    float flow = uFlowTime;

    // The normalization shell (lib_lucid): every variable from its driver, mapped to its range at its one site.
    LucidIn N = lucidIn();

    float flux = clamp(sndLevel(SND_FLUX), 0.0, 1.0) * uIntensity;
    float swell = clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    float mid = clamp(SND_MID_PRESENCE, 0.0, 1.5);
    float bass = clamp(SND_BASS_PRESENCE, 0.0, 1.5);

    // Five levels is the family's deepest; two octaves keep it affordable.
    int depth = lucidBudget(5, 3);
    int oct = lucidDetail(2, 1);

    gS = 0.35 * lucidSpan(N.scale, 1.0, 0.6, 1.4);
    gTeeth = lucidSpan(N.depth, 5.0, 5.0, 9.0);
    gTinePh = t * 0.11;
    gWY = -3.4 * lucidSpan(N.reach, 1.0, 0.875, 1.125);
    gB = clamp((0.14 + 0.1 * min(bass, 1.0)) * lucidSpan(N.flow, 1.0, 0.55, 1.45), 0.0, 0.4);
    gPh = flow * 0.3 + t * 0.04;
    gLip = 1.0 + gB;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every march term repeats every 32 in z (see the header): exact and invisible.
    float zBase = LI_ZWRAP * floor(ro.z / LI_ZWRAP);
    ro.z -= zBase;
    // Grow and layers (lib_grow) measure from the eye in the frame the world is read in.
    growEye(ro);
    gEye = ro;

    float tm = 0.03;
    float hitT = -1.0;
    for (int i = 0; i < LI_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (tm > LI_FAR) break;
        fld = lucidField(ro + rd * tm);
        float d = fld.d;
        float eps = 0.0008 * tm + 0.0005;
        if (d < eps) {
            hitT = tm;
            break;
        }
        tm += max(d * 0.9, eps);
    }

    vec3 paper = mix(vec3(0.86, 0.82, 0.74), pal(0.1), 0.12);
    vec3 fogCol = paper * 0.97;
    vec3 L = normalize(vec3(-0.3, 0.7, 0.6));
    vec3 inkCol = pal(0.6 + lucidSpan(N.tone, 0.0, -0.2, 0.2)) * 0.06 + vec3(0.02, 0.02, 0.03);
    vec3 washCol = mix(inkCol, pal(0.3 + lucidSpan(N.tone, 0.0, -0.2, 0.2)) * 0.5, 0.25);
    vec3 col = skyCol(rd, fogCol, paper, L);

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        bool onTine = tineDist(p) < (p.y - waterH(p.xz)) / gLip;
        vec3 surf;
        if (onTine) {
            // Lacquered black stylus: a little diffuse, a hard highlight, the
            // paper sky along its rim.
            vec3 nrm = lucidNormal(p, max(0.001, 0.0008 * hitT));
            float ndv = clamp(dot(nrm, -rd), 0.0, 1.0);
            float dif = max(dot(nrm, L), 0.0);
            vec3 r = reflect(rd, nrm);
            float spec = pow(max(dot(r, L), 0.0), 50.0);
            float fres = pow(1.0 - ndv, 3.0);
            surf = inkCol * 1.5 * (0.35 + 0.65 * dif) + skyCol(r, fogCol, paper, L) * (0.06 + 0.5 * fres)
                 + vec3(spec) * 0.45;
        } else {
            // The water's own normal, analytic, with a ring of ripples round
            // the nearest tine's foot.
            float a1 = 0.37 * p.x + 2.0 * LI_U16 * p.z + gPh;
            float a2 = -0.23 * p.x + 3.0 * LI_U16 * p.z - 0.7 * gPh + 1.3;
            float hx = gB * (0.6 * 0.37 * cos(a1) - 0.4 * 0.23 * cos(a2));
            float hz = gB * (0.6 * 2.0 * LI_U16 * cos(a1) + 0.4 * 3.0 * LI_U16 * cos(a2));
            vec2 to = tineOffset(p);
            float rr = length(to);
            float rip = sin(rr * 12.0 - t * 2.5) * exp(-rr * 1.2) * 0.07 * (0.5 + swell);
            vec2 slope = vec2(hx, hz) + to / max(rr, 1e-3) * rip;
            vec3 nrm = normalize(vec3(-slope.x, 1.0, -slope.y));
            float ndv = clamp(dot(nrm, -rd), 0.0, 1.0);
            float fres = 0.04 + 0.96 * pow(1.0 - ndv, 5.0);
            vec3 r = reflect(rd, nrm);
            float spec = pow(max(dot(r, L), 0.0), 60.0);

            // Where on the bath this is, on the marbling's annulus (see the header).
            float R = LI_TEX_PERIOD * gS / LI_TAU;
            float zTex = mod(zBase, LI_TEX_PERIOD) + p.z;
            float th0 = LI_TAU * zTex / LI_TEX_PERIOD + mod(flow * 0.08 / R, LI_TAU);
            float xn = p.x * gS;
            // The rake: crests at the tines pull the ink forward along z, and
            // a long wave (64 per turn) sways it sideways, and back.
            float rake = (0.18 + 0.16 * swell * uIntensity * lucidSpan(N.emit, 1.0, 0.5, 1.5));
            float th = th0 + (rake / R) * sin(xn * gTeeth + gTinePh);
            xn += 0.35 * rake * sin(64.0 * th0 - t * 0.07);
            vec2 q = (R + xn) * vec2(cos(th), sin(th));
            q += lucidJitter(q, t);

            // The nest WARPS with smooth simplex and only the final read is
            // ridged: ridged noise fed back through five levels is chaotic
            // and dissolves into static. Smooth folds carrying sharp rings
            // is suminagashi.
            float strength = 0.85 * lucidChaos();
            vec4 n = lucidNest(q, depth, strength, t * 0.12, LUCID_SIMPLEX, oct);
            n.z = lucidFbm(q * 1.6 + strength * n.xy, LUCID_RIDGED, oct);

            // Far off, a line is thinner than a pixel: widen the edges with
            // the footprint and ease the ink toward its mean density.
            float px = hitT * 2.0 / (max(uCamFocal, 0.1) * uResolution.y);
            float aa = clamp(px * gS * 6.0, 0.0, 0.3);
            float farK = smoothstep(14.0, LI_FAR, hitT);

            float ridge = 0.5 + 0.5 * n.z;
            float lines = smoothstep(0.72 - lucidSpan(N.glow, 0.0, -0.06, 0.06) - aa, 0.95 + aa * 0.3, ridge);
            float wash = smoothstep(0.25 - 0.1 * clamp(mid, 0.0, 1.0), 0.8, ridge) * (0.35 + 0.25 * n.w);
            lines = mix(lines, 0.12, farK);
            wash = mix(wash, 0.3, farK);
            float ink = clamp(max(lines, wash), 0.0, 1.0);

            // Granulation: a fixed speckle tied to the bath, revealed by flux.
            float speck = lucidValue(q * 9.0) * 0.5 + lucidValue(q * 24.3 + 9.1) * 0.5;
            speck *= 1.0 - farK;
            float grain = clamp(flux, 0.0, 1.5) * 0.25;
            ink = clamp(ink * (1.0 + grain * speck * 1.6), 0.0, 1.0);

            // The bath: paper seen through still water, the paper sky in it.
            vec3 bath = paper * 0.9 * (0.95 + 0.05 * speck * (0.3 + grain));
            vec3 water = mix(bath, skyCol(r, fogCol, paper, L), fres);
            // The ink film is matte: it covers the mirror, keeping a dull sheen.
            surf = mix(water, washCol, wash * (1.0 - lines));
            surf = mix(surf, inkCol, lines);
            surf = mix(surf, inkCol, (ink - max(lines, wash)) * 0.6);
            surf += vec3(spec) * (0.35 * (1.0 - ink) + 0.08 * ink);
        }
        col = mix(surf, fogCol, 1.0 - exp(-hitT * 0.045));
    }

    col *= 0.8;
    fragColor = vec4(grade(col), 1.0);
}
