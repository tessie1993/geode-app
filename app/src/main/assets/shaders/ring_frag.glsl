#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uSoundTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Morph, Warp and Turbulence are spent on the crown below, not on the rays.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_shell
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3

// Ring: the circular spectrum, built as an object. A crown of RG_SECTORS
// glass blades stands on a thin chrome hub around a glowing core; each blade
// is one spectrum band and grows out from the hub as far as its band is loud,
// so the crown's outline IS the spectrum, mirrored round the circle (bass
// opposite the highs) the way the flat ring drew it. Inside the hub the
// waveform runs round the ring plane as a glowing oscillogram line. The core
// is the scene's light: the blades are lit from the centre, and their tips
// glow with their own band.
//
// The camera is the shared rig (camAround): the eye circles the crown at
// 2 * CAM_SPIN_UNIT per unit of travel, so the 8192 travel wrap lands on a
// whole number of turns and is invisible; the dolly pulls it in. The crown is
// tilted toward the eye (the eye sits RG_TILT above the ring plane), so it
// always reads as an ellipse and never goes edge-on, and the orbit shows up
// as the crown turning past. uZoom, the rig's look, roll and shake reach the
// rays as on every rig style.
//
// ---- audio ------------------------------------------------------------------
//
//   spectrum (sndSpectrum)       each blade's length and thickness, and its tip glow
//   kick (SND_KICK_ENV)          the core swells (geometry); PULSE scales it
//   sub (SND_SUB_ATT)            the core's and the halo's brightness, as before
//   waveform (sndWave01)         the oscillogram line inside the hub
//   mids (sndShaped(SND_MIDDLE)) the oscillogram's brightness
//   drop (SND_HELD_SURGE)        the crown tips toward face-on and back, a 0-1-0 bump
//   pitch change (SND_HELD_NOTE_CHANGE) slides the hue
//   section (uOrbit)             a slow wobble of the tilt, eased by MotionField
//
// Overall level follows SND_ENERGY_TREND and SND_LEVEL only.
//
// Shape controls: Morph tips the blades up out of the ring plane (a flat
// sunburst becomes a cup of spikes), Warp undulates the ring (each blade's
// base rides a three-lobed wave) and widens the oscillogram, Turbulence
// shimmers the blade lengths. Knobs (offset from 0.5): k0 PULSE the kick
// swell, k1 TEXTURE the blade thickness, k2 FLOW the oscillogram swing, k3
// TONE the blades' lean on top of Morph's (a high melody lifts the crown into
// a cup, a low one droops it into an umbrella under the ring plane, and a
// small hue offset), k4 BUILD the blade gain, k5 SPACE the orbit radius.
// Every one of these, and the blade width, the core's size, the eye's tilt over
// the ring, the oscillogram's radius and the halo, is one normalized input
// (lib_shell) set in one block at the top of main and mapped there to its
// range: at rest (knobs 0.5, Morph, Warp and Turbulence 0) each is the
// constant the crown was tuned with. The crown is the field the grow hooks act
// on (its shell wrapper, about the crown's centre): Spawn sets a twin crown beside
// it, Fractal grows crowns a third the size on the blades, Front layer swells
// the crown and Back layer what lies past it.
//
// ---- why the estimate is safe ----------------------------------------------
//
// The blades are a polar repeat: a point measures only its own sector's blade.
// Every other blade lies across one of the sector's two boundary planes (each
// a full plane through the axis), and no blade comes within RG_GAP of a
// boundary (the hub radius times sin(pi / N), minus the blade's tangential
// half-width and rounding), so the distance to any other blade is at least
// the distance to the nearer boundary plus RG_GAP. That bound is only taken
// inside the annulus that holds every blade; outside it, the distance to the
// annulus (an exact 2D box distance in (r, y), since the annulus is a solid
// of revolution) is the bound; the box reaches RG_LMAX above and below the
// ring plane, since TONE can lean the blades either way. Each blade's length, base and tilt are
// constants of its sector, so its own box is exact. The core and hub are
// exact. The march steps 0.85 of the estimate and starts at the bounding
// sphere RG_BOUND, which the dolly's closest eye (0.65 * RG_CAM_MIN) never
// enters; a bubble round the eye is carved out all the same.

#define RG_MAX_STEPS 96
#define RG_TAU 6.2831853
#define RG_SECTORS 48.0
/** Hub radius: where the blades stand. */
#define RG_RIN 1.45
/** Blade tangential half-width at rest, before rounding (gTh: at most 0.034, inside RG_GAP's margin). */
#define RG_TH 0.03
#define RG_ROUND 0.012
/** (RG_RIN - 0.2) * sin(pi / 48) - (RG_TH + RG_ROUND) = 0.0397, less a margin. */
#define RG_GAP 0.035
/** Longest a blade can be: 0.06 + 1.2 * 1.5625 + 0.3. */
#define RG_LMAX 2.25
/** Every surface lies inside this sphere: RG_RIN + RG_LMAX + thickness + warp lift. */
#define RG_BOUND 4.3
/** Orbit radius at SPACE 0 and 1; the dolly pulls in to 0.65 of it, 4.55 at the closest. */
#define RG_CAM_MIN 7.0
#define RG_CAM_MAX 9.0
/** The eye's elevation above the ring plane at rest, radians (sin = 0.6). */
#define RG_TILT 0.6435
/** Radius of the oscillogram line in the ring plane. */
#define RG_WAVE_R 1.12
/** How much light a blade's glass stops face-on (its rim stops more), and how many skins a ray crosses. */
#define RG_GLASS 0.55
#define RG_HITS 4

float gPhase;
float gGain;
float gThick;
float gLean;
float gWarp;
float gTurb;
float gCore;
float gTh;

float gMat;
float gBand;
float gSecFrac;
float gAlong;

/** Mirrored round the circle so the spectrum (and the palette) meets itself without a seam. */
float mirror01(float u) {
    return abs(2.0 * fract(u) - 1.0);
}

/** The crown, hub and core: the field the grow hooks act on (map() hands it to its shell wrapper). */
float ringField(vec3 p) {
    float r = length(p.xz);
    float ang = r > 1e-5 ? atan(p.z, p.x) : 0.0; // atan(0, 0) is undefined in GLSL ES

    // ---- the blade of this sector ----
    float u = ang / RG_TAU + gPhase;
    float s = floor(u * RG_SECTORS);
    float secFrac = fract((s + 0.5) / RG_SECTORS);
    float sec = mod(s, RG_SECTORS);
    float centre = ((s + 0.5) / RG_SECTORS - gPhase) * RG_TAU;
    vec2 cs = vec2(cos(centre), sin(centre));
    float xr = dot(p.xz, cs);
    float zt = dot(p.xz, vec2(-cs.y, cs.x));

    float band = clamp(sndSpectrum(mirror01(secFrac)), 0.0, 1.2);
    float h = hash11(sec + 0.37);
    float len = 0.06 + band * gGain
              + gTurb * 0.3 * (0.5 + 0.5 * sin(uTime * (2.0 + 1.5 * h) + h * RG_TAU));
    float base = gWarp * 0.25 * sin(3.0 * secFrac * RG_TAU + uTime * 0.6);
    vec2 dir = vec2(cos(gLean), sin(gLean));
    vec2 q = vec2(xr - RG_RIN, p.y - base);
    float along = dot(q, dir);
    float perp = dot(q, vec2(-dir.y, dir.x));
    float hp = (0.05 + 0.05 * band) * gThick;
    float dOwn = sdBox(vec3(along - len * 0.5, perp, zt), vec3(len * 0.5, hp, gTh)) - RG_ROUND;

    // Distance to the nearer sector boundary plane (both are full planes through the axis).
    const float sw = 0.0654031; // sin(pi / 48)
    const float cw = 0.9978589; // cos(pi / 48)
    float toEdge = max(sw * xr - cw * abs(zt), 0.0);
    // The annulus every blade lives in, as a box in (r, y).
    vec2 bc = vec2(RG_RIN + 0.5 * RG_LMAX, 0.0);
    vec2 bh = vec2(0.5 * RG_LMAX + 0.5, RG_LMAX + 0.5);
    vec2 bq = abs(vec2(r, p.y) - bc) - bh;
    float dBound = length(max(bq, 0.0)) + min(max(bq.x, bq.y), 0.0);
    float dBlades = min(dOwn, max(toEdge + RG_GAP, dBound));

    // ---- the hub and the core ----
    float dHub = sdTorus(p, vec2(RG_RIN - 0.1, 0.06));
    float dCore = length(p) - gCore;

    float d = dBlades;
    gMat = 1.0;
    if (dHub < d) {
        d = dHub;
        gMat = 2.0;
    }
    if (dCore < d) {
        d = dCore;
        gMat = 0.0;
    }
    gBand = band;
    gSecFrac = secFrac;
    gAlong = clamp(along / max(len, 1e-3), 0.0, 1.0);
    return d;
}

/** ringField through lib_shell's hooks: exactly ringField(p) at rest, and ringField read once, in a loop the compiler cannot unroll. */
float ringFieldGrown(vec3 p) {
    shellStart3(p, SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK);
    float r = 0.0;
    for (int i = SHELL_FROM; i < 4; i++) {
        if (shellPass3(i)) r = shellKeep3(i, ringField(shellAt3(i)));
    }
    return r;
}

float map(vec3 p) {
    return ringFieldGrown(p);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

vec3 skyColour(vec3 rd, float hueBase) {
    vec3 col = pal(hueBase + 0.6) * 0.025 * (0.6 + 0.4 * rd.y);
    vec3 sp = rd * 120.0;
    float s = hashCell(ivec3(floor(sp)));
    float star = (1.0 - smoothstep(0.0, 0.3, length(fract(sp) - 0.5))) * step(0.997, s);
    col += vec3(0.7) * star * (0.6 + 0.4 * sin(uTime * 2.0 + s * 400.0));
    return col;
}

void main() {
    vec2 uv = view();

    // ---- the shape inputs (lib_shell): each 0..1, 0.5 the tuned look --------------------
    float nPulse = shellKnob(0);                               // PULSE    the core's kick swell
    float nTexture = shellKnob(1);                             // TEXTURE  blade thickness and width
    float nFlow = shellKnob(2);                                // FLOW     the oscillogram's swing and radius
    float nTone = shellKnob(3);                                // TONE     the blades' lean, the hue
    float nBuild = shellKnob(4);                               // BUILD    blade gain, core size, halo
    float nSpace = shellKnob(5);                               // SPACE    orbit radius, the eye's tilt
    float nMorph = 0.5 + 0.5 * clamp(uMorph, 0.0, 1.0);        // Morph    the blades tip up
    float nWarp = 0.5 + 0.5 * clamp(uWarp, 0.0, 1.0);          // Warp     the ring's undulation
    float nTurb = 0.5 + 0.5 * clamp(uTurbulence, 0.0, 1.0);    // Turbulence the blade shimmer
    float kickGain = shellMap(nPulse, 0.2, 1.0, 1.8);
    gThick = shellMap(nTexture, 0.7, 1.0, 1.3);
    gTh = shellMap(nTexture, 0.022, RG_TH, 0.034);
    float swing = shellMap(nFlow, 0.6, 1.0, 1.4);
    float waveR = shellMap(nFlow, 1.0, RG_WAVE_R, 1.22);
    float leanTone = shellMap(nTone, -0.55, 0.0, 0.55);
    float hueTone = shellMap(nTone, -0.075, 0.0, 0.075);
    gGain = shellMap(nBuild, 0.9375, 1.25, 1.5625);
    float coreR = shellMap(nBuild, 0.4, 0.48, 0.58);
    float haloK = shellMap(nBuild, 0.45, 0.6, 0.8);
    float camR = shellMap(nSpace, RG_CAM_MIN, 0.5 * (RG_CAM_MIN + RG_CAM_MAX), RG_CAM_MAX);
    float tiltK = shellMap(nSpace, 0.5, RG_TILT, 0.8);
    float leanMorph = shellMap(nMorph, 0.0, 0.0, 0.85);
    gWarp = shellMap(nWarp, 0.0, 0.0, 1.0);
    gTurb = shellMap(nTurb, 0.0, 0.0, 1.0);
    // The hooks grow about the crown's centre, the subject; a twin stands 0.75 aside at full Spawn.
    shellSetup3(vec3(0.0), 2.5, 1.0, true);

    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float energy = clamp(sndShaped(SND_LEVEL), 0.0, 1.5);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float sub = sndLevel(SND_SUB_ATT);
    float mids = clamp(sndShaped(SND_MIDDLE), 0.0, 1.0);
    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float dropBump = 4.0 * drop * (1.0 - drop);
    float hueBase = 0.22 * clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0) + hueTone;

    float spin = 2.0 * CAM_SPIN_UNIT;
    float orbit = uCamTravel * spin;
    gPhase = fract(uTime * 0.02 + orbit / RG_TAU);
    gLean = clamp(leanMorph + leanTone, -0.7, 1.2);
    gCore = coreR + 0.16 * kick * kickGain;

    vec3 ro;
    vec3 rd;
    camAround(uv, camR, spin, ro, rd);
    // Into the crown's frame: undo the orbit's turn about +y (the eye then
    // sits near -z), then tip the ring plane toward the eye so the eye sits
    // beta above it. Both are rotations, so distances keep.
    float ca = cos(orbit);
    float sa = sin(orbit);
    mat3 unturn = mat3(ca, 0.0, sa, 0.0, 1.0, 0.0, -sa, 0.0, ca);
    float beta = tiltK + 0.3 * dropBump + 0.08 * uOrbit.x;
    float cb = cos(beta);
    float sb = sin(beta);
    mat3 tilt = mat3(1.0, 0.0, 0.0, 0.0, cb, sb, 0.0, -sb, cb);
    mat3 toRing = tilt * unturn;
    ro = toRing * ro;
    rd = toRing * rd;

    // March only inside the bounding sphere. The blades are glass: the ray
    // is traced on |field|, the way the Opal family traces its skins, so it
    // meets a blade's near face, carries on through it to its far face and to
    // the blades behind, each lit and laid over what lies past it, up to
    // RG_HITS skins. The core and the hub are opaque and end the ray.
    vec3 sky = skyColour(rd, hueBase);
    float coreLight = 0.7 + 0.8 * sub + 0.3 * kick;
    vec3 acc = vec3(0.0);
    float through = 1.0;
    // The first skin met, and how much it lets past: the oscillogram and the
    // core's halo behind it are seen through that much glass.
    float hitT = -1.0;
    float firstPass = 1.0;
    float bOff = dot(ro, rd);
    // The grown crown (a twin aside, children out of the blades, a layer opened) reaches past RG_BOUND
    // by lib_shell's slack while a hook is on; RG_BOUND itself at rest.
    float bound = RG_BOUND + SHELL_SLACK(RG_BOUND);
    float bDisc = bOff * bOff - (dot(ro, ro) - bound * bound);
    if (bDisc > 0.0) {
        float bRoot = sqrt(bDisc);
        float t = max(-bOff - bRoot, 0.0);
        float tEnd = -bOff + bRoot;
        int hits = 0;
        for (int i = 0; i < RG_MAX_STEPS; i++) {
            // uSteps is the user's Detail; this loop's own bound is the constant.
            if (float(i) >= uSteps) break;
            vec3 p = ro + rd * t;
            float d = abs(max(map(p), 0.5 - length(p - ro)));
            float eps = 0.0012 + 0.0007 * t;
            if (d < eps) {
                float hitMat = gMat;
                float secFrac = gSecFrac;
                float bandHit = gBand;
                float alongHit = gAlong;
                vec3 n = normalAt(p, max(0.0012, 0.0008 * t));
                // The side of the skin that faces the eye (a far face points away).
                if (dot(n, rd) > 0.0) n = -n;
                float occ = 0.0;
                float sc = 1.0;
                for (int k = 0; k < 2; k++) {
                    float hk = 0.08 + 0.14 * float(k);
                    occ += (hk - map(p + n * hk)) * sc;
                    sc *= 0.6;
                }
                float ao = clamp(1.0 - 1.8 * occ, 0.3, 1.0);

                float rl = length(p);
                vec3 L = -p / max(rl, 1e-4);
                float fall = coreLight / (1.0 + 0.35 * rl * rl);
                float dif = max(dot(n, L), 0.0);
                float ndv = clamp(dot(n, -rd), 0.0, 1.0);
                float fres = pow(1.0 - ndv, 3.0);
                float spec = pow(max(dot(reflect(rd, n), L), 0.0), 32.0);

                vec3 c;
                float opacity = 1.0;
                if (hitMat < 0.5) {
                    // The core: emissive, brighter at the limb.
                    vec3 cc = pal(hueBase + 0.1);
                    c = cc * (0.55 + 0.6 * sub + 0.25 * kick) * (0.7 + 0.6 * fres) + vec3(0.15) * fres;
                } else if (hitMat < 1.5) {
                    // A blade: glass lit from the core, its tip glowing with its
                    // band, clearer face-on and denser at the rim, as glass is.
                    vec3 bc = pal(hueBase + 0.5 * mirror01(secFrac));
                    float tip = smoothstep(0.15, 1.0, alongHit);
                    c = bc * (0.05 + 0.85 * dif * fall) * ao
                      + bc * tip * (0.08 + 0.9 * bandHit)
                      + mix(vec3(1.0), bc, 0.4) * spec * fall * 0.5
                      + bc * fres * 0.18;
                    opacity = RG_GLASS + (1.0 - RG_GLASS) * fres;
                } else {
                    // The hub: dark chrome catching the core and the crown's colour.
                    vec3 hc = pal(hueBase + 0.5 * mirror01(secFrac) + 0.25);
                    c = hc * (0.03 + 0.4 * dif * fall) * ao + vec3(0.8) * spec * fall * 0.6 + hc * fres * 0.3;
                }
                c = mix(c, sky, 1.0 - exp(-max(t - 3.0, 0.0) * 0.04));
                acc += through * opacity * c;
                if (hitT < 0.0) {
                    hitT = t;
                    firstPass = 1.0 - opacity;
                }
                through *= 1.0 - opacity;
                hits++;
                if (hits >= RG_HITS || through < 0.03) break;
                t += 6.0 * eps;
                continue;
            }
            t += d * 0.85;
            if (t > tEnd) break;
        }
    }
    vec3 col = acc + sky * through;

    // The oscillogram: the waveform drawn round the ring plane (y = 0), in
    // front of whatever the march hit. Analytic, so it costs no steps.
    if (abs(rd.y) > 1e-4) {
        float tp = -ro.y / rd.y;
        float lineSeen = (hitT < 0.0 || tp < hitT) ? 1.0 : firstPass;
        if (tp > 0.0 && lineSeen > 0.0) {
            vec3 pp = ro + rd * tp;
            float pr = length(pp.xz);
            float pa = pr > 1e-5 ? atan(pp.z, pp.x) : 0.0;
            float wu = fract(pa / RG_TAU + gPhase);
            float w = sndWave01(mirror01(wu));
            float amp = 0.45 * (swing + 0.6 * gWarp);
            float off = abs(pr - (waveR + (w - 0.5) * amp));
            float line = 1.0 - smoothstep(0.0, 0.02 + 0.002 * tp, off);
            float glowW = exp(-off * 16.0) * 0.3;
            col += pal(hueBase + 0.3 + 0.5 * mirror01(wu)) * (line + glowW) * (0.35 + 0.9 * mids) * lineSeen;
        }
    }

    // The core's halo, from the ray's closest pass by the centre, seen through
    // whatever glass lies in front of it (hidden behind chrome or the core).
    float tc = max(-dot(ro, rd), 0.0);
    float haloSeen = (hitT < 0.0 || hitT > tc) ? 1.0 : through;
    float dc = length(ro + rd * tc);
    col += pal(hueBase + 0.1) * exp(-dc * 2.2) * (0.25 + 0.6 * sub) * haloK * haloSeen;

    col *= 0.8 + 0.25 * swell + 0.1 * energy;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
