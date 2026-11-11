#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// The shape controls are spent on the vortex itself (lib_neon), not on the rays.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_neon

// Glow Vortex: a flight down the throat of a neon whirlpool. The bore is a
// tube of dark glass whose wall is wound with the style's web of thin, glassy
// neon spirals - two crossing families of helices (the loxodromes of the flat
// version, now lying on a real cylinder), rings of a third family between them
// and a few broad glass ribbons riding the first. Smaller spiral eyes are
// sunk into the wall as funnel-shaped pits, each wound with its own spiral web
// and white-hot at its centre, and they orbit around the bore. Far down the
// throat, past the violet fog, the largest eye of all - the central vortex -
// burns where the tube vanishes. Everything on the wall is emission over dark
// glass, so the lines read as lit glass, as before.
//
// ---- the camera --------------------------------------------------------------
//
// camFly: the eye rides the shared rig's path down the bore axis (+z), so the
// rig's dives, pushes, yaw and shake all fly it; the eye's xy is pulled inside
// the bore whatever the rig's wander. Catalogue's spin for this style turns the
// rig against the arms' winding, as it turned the flat web.
//
// ---- the world wrap ----------------------------------------------------------
//
// Every z term has period GV_TILE = 16 world units, which divides the rig's
// 8192 travel wrap and the eye's rebase (ro.z mod 16): the helices advance a
// whole GV_M1 / GV_M2 turns per tile, the rings sit 8 per tile, the ribbons 3
// turns per tile, the pits repeat per tile, the hue ramp turns once per tile.
// Every angular count (arms, ribbons, pit arms) is an integer, so all of them
// close across atan's branch cut. The rebase and the wrap are both invisible.
//
// ---- what moves it -----------------------------------------------------------
//
// The six looks the flat web was drawn around, each driven by the shared
// controls (lib_neon) or the sound, so Customize, the macros, the director and
// the DNA all reach it:
//
// - core flare: SND_KICK_ENV flashes the pits and the central eye white-hot,
//   whitens the line cores and lights the walls from the far eye.
//   SND_SNARE_ENV lights the third family (the rings).
// - filigree (Morph, neonForm): the rings and the glass ribbons fill in. A
//   hard cut (SND_CUT_COUNT) changes the arm count (a count, so only there).
// - swirl (Warp, neonFlow): the bore bends - its axis curves away ahead of
//   the eye, a slow sway whose direction turns, so the throat winds off out of
//   sight; the line flow leans with it. The web pours along its helices on
//   SND_BASS_TIME as well as its own clock, so it streams faster in a heavy
//   passage and never runs backwards.
// - Turbulence (neonChaos) shakes the pits off their orbits.
// - hue drift: the key (neonKeyTurn) turns the ramp; SND_CHORD_HUE turns the
//   rings against the helices.
// - depth: SND_BUILD_UP narrows the throat, zooms the central eye in and lifts
//   the fog; the drop (SND_HELD_SURGE) narrows it further and wakes the pits
//   (deeper, brighter) while the rig itself dives.
// - spread: the pits orbit around the bore on SND_MID_TIME; the bore widens
//   in silence (neonQuiet). A hard cut (SND_CUT_COUNT) re-deals their orbits.
// - SND_HAT_ENV sparkles the ribbons. Intensity (neonGlow) is the emission.
// - The six style knobs (neonKnob) ride on the looks they name.
//
// ---- why the distance estimate is safe ---------------------------------------
//
// The scene is the inside of a tube of radius gR about a gently bent axis,
// d = wall(angle, z) - r. The wall moves inward by at most gAmp along the
// helix ridges (a cosine of the helix phase) and outward into the pits (a
// Gaussian of depth gDim and width gDimS in the wall's own chart). gLip bounds
// the gradient of that wall height at the smallest radius the wall reaches
// (ridge: gAmp / 2 times the phase gradient; pit: 0.86 gDim / gDimS), times
// the bend's z slope and a 10% margin for the chart measuring arc at gR rather
// than r, so d / gLip never overshoots where the wall is. Nearer the axis the
// wall is at least (gR - gAmp - r) / (1 + bend slope) away in every
// direction, which is also a lower bound; the march steps by the larger of
// the two, so it runs down the open throat in a few steps and slows only near
// the wall. Steps are bounded
// by the constant GV_MAX_STEPS and broken on the user's Detail (uSteps).
//
// Provenance: written from scratch for BeatRealms from general techniques - a
// cylinder chart (angle, z) with integer-wound helix families; line rendering
// with an ANALYTIC pixel footprint (the phase gradient times the surface's
// world-per-pixel size over the view cosine), so the lines stay one pixel thin
// at every distance and fade to their average where they crowd; the complex
// logarithm for the pits' and the central eye's spirals; sphere tracing with a
// Lipschitz-bounded wall height; tetrahedral normals. No code was taken from
// any published shader.

#define GV_TAU 6.2831853
#define GV_PI 3.1415927
/** z period of everything, world units: divides the rig's 8192 wrap. */
#define GV_TILE 16.0
#define GV_ZK (GV_TAU / GV_TILE)
#define GV_MAX_STEPS 100
#define GV_FAR 34.0
#define GV_EYES 3
/** Whole turns per tile of the first and second helix families (divisible by 3 for the ribbons). */
#define GV_M1 9.0
#define GV_M2 6.0

/** World size of one pixel on the surface being drawn (lines()). */
float gPx;
float gR;
float gAmp;
float gArms;
float gFlow;
float gEyeZ;
float gBend;
vec2 gBendDir;
float gDim;
float gDimS;
float gLip;
/** 1 + the bent axis's largest z slope within GV_FAR of the eye. */
float gBendLip;
/** Per pit: angle around the bore, z within the tile, wake 0..1. */
vec3 gEye[GV_EYES];

vec3 neon(float t) {
    // Violet -> electric blue -> cyan -> acid green -> gold -> magenta, with a
    // pull toward the user's palette so the colour controls still move it.
    vec3 c = 0.5 + 0.5 * cos(GV_TAU * (t + vec3(0.60, 0.78, 0.98)));
    c = c * c * c * vec3(1.1, 1.0, 1.35) + vec3(0.02, 0.0, 0.06);
    return neonPal(c, t);
}

/**
 * A family of lines x = const (spacing 1) where x's gradient is `grad` (units
 * of x per world unit on the surface, or per unit of whatever gPx measures).
 * Returns (thin core, soft glow), faded to their average where the lines are
 * too close to resolve.
 */
vec2 lines(float x, float grad) {
    float spacingPx = 1.0 / max(grad * gPx, 1e-6);
    float d = abs(fract(x) - 0.5) * spacingPx; // pixels to the nearest line
    float core = exp(-d * d * 0.9);
    float glow = 1.0 / (1.0 + d * 0.35);
    glow *= glow;
    float resolve = smoothstep(1.5, 6.0, spacingPx);
    float avgCore = min(1.6 / spacingPx, 1.0);
    float avgGlow = min(5.0 / spacingPx, 1.0);
    return mix(vec2(avgCore, avgGlow), vec2(core, glow), resolve);
}

/**
 * A glass ribbon centred on the lines x = const: a translucent fill `width`
 * wide (fraction of the spacing) with bright rims, like a lit glass blade
 * seen edge-on. Fades out where it cannot be resolved.
 */
float ribbon(float x, float grad, float width) {
    float spacingPx = 1.0 / max(grad * gPx, 1e-6);
    float f = abs(fract(x) - 0.5);
    float halfW = width * 0.5;
    float rimPx = abs(f - halfW) * spacingPx;
    float rim = exp(-rimPx * rimPx * 0.6) + 0.25 / (1.0 + rimPx * 0.5);
    float fill = smoothstep(halfW + 0.5 / spacingPx, halfW - 0.5 / spacingPx, f) * (0.18 + 0.25 * f / max(halfW, 1e-3));
    return (rim + fill) * smoothstep(4.0, 14.0, spacingPx * width);
}

/** The bore's axis offset at world z: a curve away from the eye, flat at the eye itself. */
vec2 axisOff(float z) {
    float dz = z - gEyeZ;
    return gBend * dz * dz * gBendDir;
}

/** The first helix family's phase; ridges (and lines) where it is a whole turn. */
float phase1(float ang, float z) {
    return gArms * ang + GV_M1 * GV_ZK * z - gFlow;
}

/** Pit i's chart offset (arc, z) from a point at angle `ang`, world z `z`; z wrapped to the nearest tile. */
vec2 pitOffset(int i, float ang, float z) {
    vec3 e = gEye[i];
    float da = mod(ang - e.x + GV_PI, GV_TAU) - GV_PI;
    float dz = mod(z - e.y + 0.5 * GV_TILE, GV_TILE) - 0.5 * GV_TILE;
    return vec2(da * gR, dz);
}

/** Bore coordinates of p: (radius from the bent axis, angle). */
vec2 bore(vec3 p) {
    vec2 q = p.xy - axisOff(p.z);
    float r = length(q);
    // atan(0, 0) is undefined in GLSL ES: nudge x on the axis itself.
    float ang = atan(q.y, q.x + step(r, 1e-6) * 1e-6);
    return vec2(r, ang);
}

float map(vec3 p) {
    vec2 ra = bore(p);
    float wall = gR - gAmp * (0.5 + 0.5 * cos(phase1(ra.y, p.z)));
    for (int i = 0; i < GV_EYES; i++) {
        vec2 v = pitOffset(i, ra.y, p.z);
        wall += gDim * gEye[i].z * exp(-dot(v, v) / (gDimS * gDimS));
    }
    // The Lipschitz-bounded estimate near the wall, and the open throat's
    // clearance nearer the axis (over the bend's slope, since the axis curves
    // under a moving point): both are lower bounds, so take the larger.
    return max((wall - ra.x) / gLip, (gR - gAmp - ra.x) / gBendLip);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** The violet swirl the fog is made of: soft bands in log-polar space about the throat's end. */
vec3 swirlCol(vec2 sp) {
    float r = max(length(sp), 1e-3);
    float ang = atan(sp.y, sp.x + step(r, 1.001e-3) * 1e-6);
    float swirl = 0.5 + 0.5 * sin(ang * 3.0 + log(r) * 4.0 - uTime * 0.2);
    swirl *= 0.5 + 0.5 * sin(ang * 5.0 - log(r) * 2.0 + uTime * 0.13);
    return mix(vec3(0.035, 0.01, 0.08), vec3(0.0, 0.05, 0.06), swirl) * (0.6 + 0.8 * swirl);
}

void main() {
    vec2 uv = view();

    // The six looks, each 0..1 with 0.5 as tuned: see "what moves it" above.
    float surge = neonSurge();
    float quiet = neonQuiet();
    float glow = neonGlow();
    float kPulse = clamp(0.15 + 0.85 * neonKick() + 0.4 * surge, 0.0, 1.0) * (1.0 - 0.6 * quiet);
    float kTex = mix(0.5, 1.0, neonForm());
    float kFlow = clamp(0.5 + 0.5 * neonFlow(), 0.0, 1.0);
    float kTone = 0.5 + neonKeyTurn() / 0.6;
    float kBuild = clamp(0.4 + 0.5 * neonBuild() + 0.6 * surge, 0.0, 1.0);
    float kSpace = clamp(0.5 + 0.4 * quiet - 0.2 * surge, 0.0, 1.0);
    // Note & key reactions' style knobs ride on top (lib_neon).
    kPulse = clamp(kPulse + neonKnob(0), 0.0, 1.0);
    kTex = clamp(kTex + neonKnob(1), 0.0, 1.0);
    kFlow = clamp(kFlow + neonKnob(2), 0.0, 1.0);
    kBuild = clamp(kBuild + neonKnob(4), 0.0, 1.5);
    kSpace = clamp(kSpace + neonKnob(5), 0.0, 1.0);
    kTone += neonKnob(3);
    float chaos = neonChaos();
    float snare = sndShaped(clamp(SND_SNARE_ENV, 0.0, 1.0)) * (1.0 - quiet);
    float cut = floor(SND_CUT_COUNT + 0.5);
    float deal = cut * 1.7;

    // ---- the vortex's shape for this frame ----
    gR = mix(2.5, 3.6, kSpace) * (1.0 - 0.14 * clamp(kBuild, 0.0, 1.0));
    gAmp = 0.12;
    // A count, so it changes only on a hard cut (lib_neon: never a count on Morph).
    // Always a multiple of 3, so the ribbons (a third of the arms) close too.
    gArms = 12.0 + 3.0 * mod(cut, 3.0);
    gFlow = (uTime * 0.12 + SND_BASS_TIME * 0.3) + kFlow * 1.2;
    float bendAmt = clamp(neonFlow() + neonKnob(2), 0.0, 1.0);
    gBend = 0.0035 * bendAmt;
    gBendDir = vec2(cos(uTime * 0.05 + deal), sin(uTime * 0.05 + deal));
    float wake = clamp(0.45 + 0.55 * surge + 0.2 * neonBuild(), 0.0, 1.0);
    gDim = 1.1;
    gDimS = 0.85;
    float rMin = gR - gAmp;
    float ridgeLip = 0.5 * gAmp * length(vec2(gArms / rMin, GV_M1 * GV_ZK));
    gBendLip = 1.0 + 2.0 * gBend * GV_FAR;
    gLip = 1.1 * (1.0 + ridgeLip + 0.86 * gDim / gDimS) * gBendLip;

    // The pits orbit around the bore (2D: the secondary eyes' orbits).
    float tOrb = uTime * 0.03 + SND_MID_TIME * 0.05 + deal;
    for (int i = 0; i < GV_EYES; i++) {
        float fi = float(i);
        float a = tOrb * (0.7 + 0.2 * fi) + fi * 2.1 + chaos * 0.25 * sin(uTime * 2.3 + fi * 4.1);
        float z = (fi + 0.5) * GV_TILE / float(GV_EYES) + 1.3 * sin(fi * 5.3 + deal) + chaos * 0.5 * cos(uTime * 1.9 + fi * 2.7);
        gEye[i] = vec3(a, z, wake * (0.75 + 0.25 * sin(fi * 2.9 + deal)));
    }

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with period 16 (see "the world wrap"), so this
    // rebase is exact and invisible, and z stays precise.
    ro.z = mod(ro.z, GV_TILE);
    gEyeZ = ro.z;
    // Keep the eye well inside the throat, whatever the rig's wander (the axis
    // is not bent at the eye, so its offset there is zero).
    float er = length(ro.xy);
    float maxR = max(gR - 1.3, 0.3);
    if (er > maxR) ro.xy *= maxR / er;

    float t = 0.05;
    bool hit = false;
    for (int i = 0; i < GV_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        float d = map(ro + rd * t);
        float eps = 0.0009 * t + 0.0006;
        if (d < eps) {
            hit = true;
            break;
        }
        t += max(d * 0.85, eps);
        if (t > GV_FAR) break;
    }

    // Where the throat's far end lies on screen: the bend carries it off axis.
    vec2 endOff = gBend * GV_FAR * gBendDir;
    vec2 sp = rd.xy / max(abs(rd.z), 0.08) - endOff;
    vec3 fogCol = swirlCol(sp * 2.0);
    float fogK = mix(0.075, 0.04, clamp(kBuild, 0.0, 1.0));
    float hue0 = kTone * 0.6 + 0.02 * uTime;
    vec3 eyeCol = mix(neon(0.55 + kTone * 0.6), vec3(1.0, 0.95, 0.85), 0.4 + 0.4 * kPulse);
    vec3 col = fogCol;

    if (hit) {
        vec3 p = ro + rd * t;
        vec3 n = normalAt(p, max(0.0015, 0.001 * t));
        vec2 ra = bore(p);
        float ang = ra.y;
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        // World size of a pixel here, stretched by the slant of the wall.
        gPx = t * 2.0 / (max(uCamFocal, 0.1) * uResolution.y) / max(ndv, 0.2);

        // ---- the web on the wall ----
        float hue = hue0 + p.z / GV_TILE;
        float r = max(ra.x, 0.5);
        float u1 = phase1(ang, p.z);
        float n2 = floor(gArms * 0.62 + 0.5);
        float u2 = n2 * ang - GV_M2 * GV_ZK * p.z - gFlow * 0.62;
        float g1 = length(vec2(gArms / r, GV_M1 * GV_ZK)) / GV_TAU;
        float g2 = length(vec2(n2 / r, GV_M2 * GV_ZK)) / GV_TAU;
        // The first family sits on the ridge crests.
        vec2 l1 = lines(u1 / GV_TAU + 0.5, g1);
        vec2 l2 = lines(u2 / GV_TAU + 0.5, g2);
        // The third family: rings round the bore, 8 per tile.
        vec2 l3 = lines(p.z * 0.5 - gFlow * 0.5, 0.5) * (0.35 + 0.65 * kTex + 1.2 * snare);

        // Flame-like variation along the arms (integer angular rates, z rates on the tile).
        float vary = 0.35 + 0.65 * pow(0.5 + 0.5 * sin(3.0 * ang + 2.0 * GV_ZK * p.z + uTime * 0.4), 2.0);
        float vary2 = 0.35 + 0.65 * pow(0.5 + 0.5 * sin(-2.0 * ang + 3.0 * GV_ZK * p.z - uTime * 0.3 + 1.0), 2.0);

        // ---- the pits: each its own small vortex ----
        vec3 pits = vec3(0.0);
        float pitNear = 0.0;
        float cores = 0.0;
        for (int i = 0; i < GV_EYES; i++) {
            float fi = float(i);
            vec2 v = pitOffset(i, ang, p.z);
            float lv = max(length(v), 1e-4);
            vec2 w = vec2(log(lv), atan(v.y, v.x + step(lv, 1.001e-4) * 1e-6));
            float ea = 6.0 + 2.0 * fi;
            float etw = mix(0.6, 1.4, kFlow);
            float ef = gFlow * (1.0 + 0.24 * fi) * 0.6;
            float en2 = floor(ea * 0.62 + 0.5);
            vec2 e1 = lines((w.x * etw * ea - ef + w.y * ea) / GV_TAU, ea * length(vec2(etw, 1.0)) / (GV_TAU * lv));
            vec2 e2 = lines((w.x * etw * ea * 0.62 - ef * 0.62 - w.y * en2) / GV_TAU, length(vec2(etw * ea * 0.62, en2)) / (GV_TAU * lv));
            float nearE = exp(-lv * mix(2.2, 1.2, clamp(kBuild, 0.0, 1.0))) * 1.4 * gEye[i].z;
            float eh = hue + 0.12 * (fi + 1.0) + 0.07 * w.x;
            pits += (neon(eh) * (e1.x * 0.9 + e1.y * 0.22) + neon(eh + 0.33) * (e2.x * 0.7 + e2.y * 0.18)) * nearE;
            pitNear += nearE;
            cores += exp(-lv * 9.0) * gEye[i].z;
        }
        float wallFade = 1.0 - 0.75 * clamp(pitNear, 0.0, 1.0);

        vec3 c1 = neon(hue);
        vec3 c2 = neon(hue + 0.33);
        vec3 c3 = neon(hue + 0.6 + 0.4 * SND_CHORD_HUE);
        // The kick whitens the line cores.
        float hot = 0.35 * kPulse;
        vec3 em = (mix(c1, vec3(1.0), hot) * l1.x * 0.9 + c1 * l1.y * 0.22) * vary
                + (mix(c2, vec3(1.0), hot) * l2.x * 0.7 + c2 * l2.y * 0.18) * vary2
                + c3 * (l3.x * 0.5 + l3.y * 0.12);
        // A few broad glass ribbons ride the first family at a third of its pitch.
        float rib = ribbon(u1 / (3.0 * GV_TAU) + 0.67, g1 / 3.0, 0.22) * vary;
        em += neon(hue + 0.15) * rib * (0.55 + 0.8 * neonSparkle()) * (0.4 + 0.6 * kTex);
        em = em * wallFade * 0.8 + pits * 0.8;
        em += eyeCol * cores * (0.3 + 1.6 * kPulse);

        // Dark glass: a violet body, the fog's swirl in the fresnel, and the
        // far eye's light glinting down the throat (+z) on the kick.
        vec3 refl = reflect(rd, n);
        vec2 rsp = refl.xy / max(abs(refl.z), 0.08) - endOff;
        float glint = pow(max(refl.z, 0.0), 24.0);
        vec3 body = vec3(0.02, 0.006, 0.05) * (0.4 + 0.6 * ndv)
                  + swirlCol(rsp * 2.0) * (0.3 + 1.2 * fres)
                  + eyeCol * glint * (0.15 + 0.6 * kPulse) * (0.4 + 0.6 * clamp(kBuild, 0.0, 1.0));

        float fog = 1.0 - exp(-t * fogK);
        fog = max(fog, smoothstep(GV_FAR * 0.7, GV_FAR, t));
        col = mix(body + em * glow, fogCol, fog);
    } else if (rd.z > 0.02) {
        // ---- the central eye, where the throat vanishes ----
        // How far the ray really got: a march that ran out of Detail budget
        // short of the end shows fog, not the eye.
        float esc = smoothstep(GV_FAR * 0.55, GV_FAR, t);
        float zoom = mix(1.1, 0.55, clamp(kBuild, 0.0, 1.0));
        float scale = zoom * GV_FAR / gR;
        vec2 pp = (rd.xy / rd.z - endOff) * scale;
        gPx = 2.0 / (max(uCamFocal, 0.1) * uResolution.y * rd.z) * scale;
        float lp = max(length(pp), 1e-4);
        vec2 w = vec2(log(lp), atan(pp.y, pp.x + step(lp, 1.001e-4) * 1e-6));
        float twist = mix(0.55, 1.6, kFlow);
        float u = w.x * twist - gFlow;
        float n2 = floor(gArms * 0.62 + 0.5);
        float gr = 1.0 / lp;
        vec2 l1 = lines((u * gArms + w.y * gArms) / GV_TAU, gr * gArms * length(vec2(twist, 1.0)) / GV_TAU);
        vec2 l2 = lines((u * gArms * 0.62 - w.y * n2) / GV_TAU, gr * length(vec2(twist * gArms * 0.62, n2)) / GV_TAU);
        float hue = 0.07 * w.x + hue0;
        vec3 web = neon(hue) * (l1.x * 0.9 + l1.y * 0.22) + neon(hue + 0.33) * (l2.x * 0.7 + l2.y * 0.18);
        web *= smoothstep(1.3, 0.25, lp) * (0.5 + 0.7 * clamp(kBuild, 0.0, 1.0));
        float core = exp(-lp * 6.0);
        col += (web * 0.7 * glow + eyeCol * core * (0.15 + 1.0 * kPulse)) * esc;
    }

    col *= 0.9 + 0.6 * kPulse;
    col *= 0.75 + 0.25 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(neonFinish(col, 1.2), 1.0);
}
