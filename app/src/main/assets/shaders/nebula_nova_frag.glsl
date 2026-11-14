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
//#include lib_nebula

// Nebula Nova: a supernova's blast shell, now a real sphere of shocked gas
// hanging in space with the eye circling it. A thin, bright shock front
// expands from a dying star, decelerating as it sweeps up gas; its skin breaks
// into Rayleigh-Taylor fingers as it slows; it starts white-hot and cools
// through the palette to a dull red as it grows. Inside, a compact remnant
// glows at the centre; around it lie faint ambient nebulosity and stars.
//
// Volume-marched, emissive. The shell is a radial band of glowing gas at
// radius rr(direction): the fingers are 3D noise read on the unit sphere of
// DIRECTIONS (no angle, so no seam anywhere on the sphere) and re-rolled per
// section (SND_CUT_COUNT), so each section's blast has its own shape. Seen
// through, the shell brightens toward its limb, where the ray runs along the
// skin - the ring the flat style drew is now what a sphere of gas does. A
// little extinction lets the near skin dim the far one, so it reads as a
// volume rather than a decal.
//
// The camera is the shared rig, object-centred (camAround) at NV_CAM_R,
// turning slowly about +y at one CAM_SPIN_UNIT (one turn per 1024 units of
// travel, which divides the rig's 8192 wrap, so the orbit never jumps). The
// rig's look, banking and dolly still apply; nothing in the scene depends on
// world z, so there is no z period to keep.
//
// The shell's age is the music's, not the clock's: it is one minus the
// envelope of the last drop or section change, so the blast happens ON the
// event and the shell coasts out to its full size as the envelope dies, then
// holds there as a cool remnant until the next one. When a new blast goes,
// the old shell does not vanish: it stays drawn at full size, fading on the
// new envelope, until the new front reaches it.
//
// ---- why the march is safe ----------------------------------------------------
//
// It is a sampled volume inside a bounding sphere (nebSphere, NV_BOUND), with
// one constant-bound loop (NEB_VOL_MAX, breaking on uSteps, the Detail
// control) and the transmittance break. Empty space is skipped by RADIAL gap
// only: r = |p| has unit gradient, so a point whose radius is g away from a
// band's [inner, outer] bounds is at least g from every point of that band,
// and stepping 0.9 g can never jump a skin. The bounds hold the fingers' full
// reach (fbm is 0..1, so fingers - 0.5 is within +-0.5). Inside a band the
// step is capped at half the skin's width so the thin young shell is never
// stepped over. If the dolly and SPACE pull the eye inside NV_BOUND, nebSphere
// starts the march at the eye, so nothing behind it is drawn and nothing breaks.
//
// ---- audio --------------------------------------------------------------------
//
// A drop (SND_HELD_SURGE) or a section change (SND_HELD_HARD_CUT) RESTARTS THE
// SHELL from the star. Its colour is its age: hot at the blast, cooling as it
// expands. The new shell is small when it is hottest, so the blast is a point
// of light, not a full-frame flash. The hats twinkle the stars. The key turns
// the cooled remnant's hue. Overall level follows
// (0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)) gently.
//
// ---- macros and knobs -----------------------------------------------------------
//
// Speed: already in uTime, which churns the shell's texture and the twinkle
// (the expansion is the music's). Complexity: uMorph adds octaves to the shell
// texture (nebVolOctaves). Chaos: uWarp/uTurbulence swirl the remnant
// (nebWarp3); uTwist via view(). Colour: the cooling ramp and the nebulosity
// are pal(). Energy: uIntensity scales the fingers' reach. Space: the rig.
// Knobs (each 0.5 = as tuned): k0 PULSE thickens the shock front, k1 TEXTURE
// sharpens the gas texture, k2 FLOW lengthens the fingers, k3 TONE turns the
// hue, k4 BUILD keeps the shell hot longer, k5 SPACE moves the eye in or out.

#define NV_CAM_R 4.0
/** Bounding sphere of everything drawn: either shell's full reach at Energy 2 and FLOW 1. */
#define NV_BOUND 2.6
/** Shell radius at the blast and at full growth, world units. */
#define NV_R0 0.1
#define NV_RFULL 1.55
/** Extinction per unit density: enough for the near skin to dim the far one. */
#define NV_SIGMA 2.5

float gT;
float gOct;
float gSect;
float gE;
float gFlow;
float gTexK;

/** A shell density at radius r about front rr, width w: sharp outside, softer behind the front. */
float shellAt(float r, float rr, float w) {
    float x = (r - rr) / w;
    return x > 0.0 ? exp(-x * x * 3.0) : exp(-x * x * 0.6);
}

/** The fingers, -0.5..0.5, as noise on the sphere of directions. */
float fingersAt(vec3 dir) {
    return nebFbm3(dir * 2.6 + vec3(gSect, gT * 0.015, 0.7 * gSect), 3.0) - 0.5;
}

/** Radial gap from r to the band [lo, hi]: 0 inside it. */
float bandGap(float r, float lo, float hi) {
    return max(lo - r, r - hi);
}

/**
 * The frame's shells, set in main() before the march: the current front's radius, finger reach and
 * width; the previous shell's finger reach, width and weight, and whether it still shows.
 */
float gShellR;
float gShellFa;
float gShellW;
float gOldFa;
float gOldW;
float gOldK;
bool gOldOn;

/**
 * What the field holds at a point (lib_nebula_field): the shells' density (the previous one by its
 * weight), and each shell's own, which the march lights in its own colour.
 */
struct NebHit {
    float dens;
    float cur;
    float old;
};

NebHit nebMap(vec3 p, float oct) {
    float r = length(p);
    vec3 dir = p / max(r, 1e-4);
    float fing = fingersAt(dir);
    vec3 q = nebWarp3(p, gT) * 3.5 + vec3(gSect, -gT * 0.03, 0.0);
    float tex = clamp(0.5 + gTexK * (nebFbm3(q, oct) - 0.5) * 2.0, 0.0, 1.0);
    float texW = 0.5 + 0.9 * tex;
    NebHit h;
    float rr = gShellR * (1.0 + fing * gShellFa);
    h.cur = shellAt(r, rr, gShellW) * texW;
    h.old = 0.0;
    h.dens = h.cur;
    if (gOldOn) {
        float rOld = NV_RFULL * (1.0 + fing * gOldFa);
        h.old = shellAt(r, rOld, gOldW) * texW;
        h.dens += h.old * gOldK;
    }
    return h;
}

//#include lib_nebula_field

void main() {
    vec2 uv = view();
    nebSetup();
    gT = nebTime();
    gE = nebEnergy();
    float swell = clamp(0.5 * (SND_BASS_PRESENCE * uSoundDrive + SND_MID_PRESENCE * uSoundDrive), 0.0, 1.5);
    float hats = clamp(SND_HAT_ENV * uSoundDrive, 0.0, 1.0);
    gOct = nebVolOctaves(3.0);

    float kPulse = gNebIn.pulse - 0.5;
    float kTexture = gNebIn.texture - 0.5;
    float kFlow = gNebIn.flow - 0.5;
    float kTone = gNebIn.tone - 0.5;
    float kBuild = gNebIn.build - 0.5;
    float kSpace = gNebIn.space - 0.5;

    float key = nebKeyShift(0.1) + 0.12 * kTone;
    gTexK = 0.9 * (1.0 + 0.8 * kTexture);
    gFlow = 1.0 + 0.6 * kFlow;

    float ev = max(clamp(SND_HELD_SURGE, 0.0, 1.0), clamp(SND_HELD_HARD_CUT, 0.0, 1.0));
    float age = 1.0 - ev;
    float heat = pow(ev, 1.5 - 0.8 * kBuild);
    gSect = mod(SND_CUT_COUNT, 64.0) * 1.37;

    // The current shell: decelerating, its fingers growing as it slows.
    float R = NV_R0 + (NV_RFULL - NV_R0) * pow(age, 0.55);
    float fa = (0.1 + 0.3 * age) * (0.5 + 0.5 * gE) * gFlow;
    float w = (0.025 + 0.065 * R) * (1.0 + 0.6 * kPulse);
    float curLo = R * (1.0 - 0.5 * fa) - 3.6 * w;
    float curHi = R * (1.0 + 0.5 * fa) + 2.2 * w;
    vec3 hot = nebEmission(mix(0.02, 0.6, heat) + key * (1.0 - heat), heat);
    vec3 hotK = hot * (0.55 + 0.45 * heat) * 0.2 / w;

    // The previous shell, at full size, fading on the new envelope until the
    // new front reaches it.
    float faOld = 0.4 * (0.5 + 0.5 * gE) * gFlow;
    float wOld = 0.13;
    float oldK = ev * (1.0 - smoothstep(NV_RFULL * 0.6, NV_RFULL * 0.9, R));
    float oldLo = NV_RFULL * (1.0 - 0.5 * faOld) - 3.6 * wOld;
    float oldHi = NV_RFULL * (1.0 + 0.5 * faOld) + 2.2 * wOld;
    vec3 coolK = nebEmission(0.02 + key, 0.0) * 0.55 * 0.2 / wOld * oldK;
    bool oldOn = oldK > 0.004;
    gShellR = R;
    gShellFa = fa;
    gShellW = w;
    gOldFa = faOld;
    gOldW = wOld;
    gOldK = oldK;
    gOldOn = oldOn;

    vec3 ro;
    vec3 rd;
    camAround(uv, NV_CAM_R * (1.0 + 0.25 * kSpace), CAM_SPIN_UNIT, ro, rd);

    vec4 acc = vec4(0.0, 0.0, 0.0, 1.0);
    float tc = -dot(ro, rd);
    float aMid = -1.0;
    // The bounding sphere, widened by growSlack while a grow control is on (lib_grow: a twin, children, a
    // layer opened); as it was at rest.
    vec2 tb = nebSphere(ro, rd, vec3(0.0), NV_BOUND + growSlack(NV_BOUND, 1.0, 0.2));
    if (tb.y > 0.0) {
        float tt = tb.x + nebDither() * 0.03;
        for (int i = 0; i < NEB_VOL_MAX; i++) {
            if (float(i) >= uSteps) break;
            if (tt > tb.y) break;
            if (aMid < 0.0 && tt >= tc) aMid = acc.a;
            vec3 p = ro + rd * tt;
            float r = length(p);
            float gap = bandGap(r, curLo, curHi);
            float wIn = gap <= 0.0 ? w : 1e3;
            if (oldOn) {
                float g2 = bandGap(r, oldLo, oldHi);
                if (g2 <= 0.0) wIn = min(wIn, wOld);
                gap = min(gap, g2);
            }
            if (gap > 0.0) {
                // Empty space: a radial gap is a true lower bound (see header).
                tt += max(gap * 0.9, 0.01);
                continue;
            }
            NebHit at = nebField(p, gOct);
            vec3 emit = hotK * at.cur;
            if (oldOn) emit += coolK * at.old;
            float dens = at.dens;
            float dt = min(nebVolDt(tt - tb.x, 0.02, 0.008), max(0.5 * wIn, 0.012));
            nebVolEmit(acc, emit, dens * NV_SIGMA, dt);
            if (acc.a < NEB_TRANS_MIN) break;
            tt += dt;
        }
    }
    if (aMid < 0.0) aMid = acc.a;

    vec3 col = acc.rgb;

    // The glow that fills the shell behind its front: an analytic chord, not
    // samples, so the empty interior can be skipped.
    vec2 ch = nebSphere(ro, rd, vec3(0.0), R * 0.85);
    if (ch.y > 0.0) {
        float len = ch.y - ch.x;
        col += hot * 0.06 * len / max(R, 0.05) * aMid;
    }

    // The compact remnant: a point at the centre, by the ray's closest approach.
    if (tc > 0.0) {
        float b2 = max(dot(ro, ro) - tc * tc, 0.0);
        float b = sqrt(b2);
        vec3 core = vec3(0.85, 0.92, 1.0) * (exp(-b2 * 900.0) + exp(-b * 10.0) * 0.2 * (0.4 + 0.6 * heat));
        col += core * aMid;
    }

    // Ambient nebulosity and stars, behind everything.
    vec2 sky = nebSky(rd);
    float amb = nebFbm2(sky * 1.4 + vec2(9.1, 2.3), 3.0);
    vec3 bg = nebEmission(0.55 + key, 0.0) * smoothstep(0.5, 0.85, amb) * 0.07;
    bg += nebStars(sky, 7.0, 2, vec2(0.0), 1.0, 0.2, hats, gT);
    col += acc.a * bg;

    col *= 0.85 + 0.15 * swell;
    col *= 0.8 + 0.2 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
