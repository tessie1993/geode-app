#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Morph, Warp and Turbulence are spent on the water (see map), not on the rays.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3

// Ripples: a low flight over a black lake whose water glows where it moves.
// The flat style's two interfering ring sources become a field of them: the
// lake is cut into 16 x 16 pools, each with a fixed source (the standing wave)
// and a wandering one that circles it, and their rings cross and interfere
// wherever two pools meet. As in the flat style, the light is the wave
// itself: crests and troughs glow in the palette, read at the wave's height,
// and the still water between them stays dark. The ripples also mirror a dim
// horizon glow, so every ring catches a line of light as it rolls.
//
// Flown with camFly. The lake sits at RP_WATER, below the rig's y wander, and
// the eye is held above the local wave so it rides a swell rather than
// diving into it. The pools' hash repeats every 64 cells along z (1024 world
// units) and every other z term is a whole multiple of RP_KZ = 2pi/1024, so
// the eye's z is rebased into 0..1024, exactly and invisibly, and the rig's
// 8192 travel wrap never shows.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (SND_BASS_ATT)         tightens the standing rings, as the flat style did.
// Level (SND_VOL_ATT)         how tall the standing waves stand.
// Mids (SND_MID_ATT)          the wandering sources' rings; silent mids, no second set.
// Kick (SND_KICK_ENV)         a stone in every pool: a ring that runs out from the
//                             fixed source as the envelope decays.
// Pitch (SND_HELD_NOTE_CHANGE) slides the glow's hue, as a hump.
// SND_ENERGY_TREND            the long swell under everything, and overall level.
//
// Morph tightens every ring, Warp bends the lake's grid of pools, Turbulence
// adds a fine chop. Knobs, each read as an offset from 0.5 (exactly this
// picture while Note & key reactions are off): k0 PULSE the kick ring's
// height, k1 TEXTURE the chop, k2 FLOW the wandering rings, k3 TONE the hue,
// k4 BUILD the glow, k5 SPACE how far you see (less fog).
//
// ---- why the estimate is safe ----------------------------------------------
//
// The water is a heightfield, y = RP_WATER + h(xz), and the march steps the
// vertical gap divided by sqrt(1 + G^2), where G bounds |grad h|: that is a
// true lower bound on the distance to the surface. G is summed per frame from
// the live amplitudes (gLip): each sine term contributes amplitude times
// (wavenumber + the window's slope, 1.5 / RP_REACH), the kick ring its
// Gaussian's slope, and the warp multiplies it all by its Jacobian bound.
// A point is reached by at most two fixed sources (their centres, jittered
// +-2 in a 16 cell, are too far apart for a radius-8 disc to hold three) and
// at most four wandering ones, so those are the multipliers. Each source is
// windowed to zero at RP_REACH, half a cell, and the 2 x 2 pools gathered
// around a point always contain every source within that reach, so h has no
// seams. The march starts on the plane above the tallest possible crest, and a
// ray that runs out of Detail before it lands takes the flat plane instead.

#define RP_MAX_STEPS 112
#define RP_WATER -2.2
#define RP_CELL 16.0
/** Half a cell: every source fades to nothing at this radius. */
#define RP_REACH 8.0
/** Cells along z before the pools repeat: 64 * 16 = 1024, which divides 8192. */
#define RP_ZCELLS 64.0
#define RP_ZWRAP 1024.0
/** 2pi / 1024: the z wavenumber unit that keeps the lake periodic over the rebase. */
#define RP_KZ 0.0061359232
#define RP_FAR 70.0
#define RP_TAU 6.2831853

/** Standing-wave amplitude and wavenumber. */
float gAb;
float gKb;
/** Wandering-wave amplitude and wavenumber. */
float gAm;
float gKm;
/** Kick ring amplitude and radius. */
float gAk;
float gRk;
/** Swell and chop amplitudes. */
float gAs;
float gAt;
float gWarp;

/** Warp: each axis is pushed by the OTHER axis only, so the Jacobian is I plus an off-diagonal of at most 0.3 * gWarp. */
vec2 warpXZ(vec2 q) {
    return q + gWarp * vec2(sin(q.y * (49.0 * RP_KZ) + uTime * 0.5), cos(q.x * 0.3 + uTime * 0.4));
}

/** Height of the water above RP_WATER at xz. */
float lake(vec2 xz) {
    vec2 q = warpXZ(xz);
    vec2 base = floor(q / RP_CELL - 0.5);
    float h = 0.0;
    for (int j = 0; j < 2; j++) {
        for (int i = 0; i < 2; i++) {
            vec2 cell = base + vec2(float(i), float(j));
            uint u = uhash3(uvec3(ivec3(int(cell.x), int(mod(cell.y, RP_ZCELLS)), 11)));
            float h1 = uhashUnit(u);
            float h2 = uhashUnit(uhash(u));
            float ph = uhashUnit(uhash(u ^ 0x9e37u)) * RP_TAU;
            vec2 c = (cell + 0.5) * RP_CELL + (vec2(h1, h2) - 0.5) * 4.0;

            // The fixed source: the standing rings, and the kick's stone.
            float r = length(q - c);
            float win = 1.0 - smoothstep(0.0, RP_REACH, r);
            float rk = (r - gRk) * 2.5;
            h += win * (gAb * sin(r * gKb - uTime * 2.2 + ph) + gAk * (0.5 + 0.5 * h1) * exp(-rk * rk));

            // The wandering source circles within 2.5 of it, so it never leaves the cell.
            vec2 c2 = c + 2.5 * vec2(sin(uTime * 0.4 + ph), cos(uTime * 0.3 + ph * 1.7));
            float r2 = length(q - c2);
            float win2 = 1.0 - smoothstep(0.0, RP_REACH, r2);
            h += win2 * gAm * sin(r2 * gKm - uTime * 2.6 + ph);
        }
    }
    // The long swell and the fine chop; both z wavenumbers are whole multiples of RP_KZ.
    h += gAs * sin(q.x * 0.21 + q.y * (37.0 * RP_KZ) + uTime * 0.6);
    h += gAt * sin(q.x * 1.9 + uTime * 1.3) * sin(q.y * (310.0 * RP_KZ) - uTime * 1.1);
    return h;
}

vec3 skyColour(vec3 rd, float hue) {
    float up = clamp(rd.y, -1.0, 1.0);
    vec3 col = mix(pal(hue + 0.6) * 0.05, vec3(0.002, 0.002, 0.005), smoothstep(0.0, 0.5, up));
    // The glow on the horizon ahead, which every ring catches.
    float ahead = max(dot(rd, normalize(vec3(0.0, 0.06, 1.0))), 0.0);
    col += pal(hue + 0.5) * (0.45 * pow(ahead, 24.0) + 0.06 * ahead * ahead);
    col += pal(hue + 0.5) * 0.10 * exp(-abs(up) * 30.0);
    vec3 sp = rd * 150.0;
    float s = hashCell(ivec3(floor(sp)));
    float star = (1.0 - smoothstep(0.0, 0.3, length(fract(sp) - 0.5))) * step(0.997, s);
    col += vec3(0.7) * star * smoothstep(0.05, 0.3, up);
    return col;
}

void main() {
    vec2 uv = view();

    float k0 = uKnob[0];
    float k1 = uKnob[1];
    float k2 = uKnob[2];
    float k3 = uKnob[3];
    float k4 = uKnob[4];
    float k5 = uKnob[5];

    float bass = sndLevel(SND_BASS_ATT);
    float vol = sndLevel(SND_VOL_ATT);
    float mid = sndLevel(SND_MID_ATT);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float pitch = clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0);
    float pitchBump = 4.0 * pitch * (1.0 - pitch);
    float morph = clamp(uMorph, 0.0, 2.0);

    gAb = 0.05 + 0.09 * vol;
    gKb = (2.4 + 1.3 * bass) * (1.0 + 0.5 * morph);
    gAm = 0.08 * mid * (0.5 + k2);
    gKm = 3.1 * (1.0 + 0.5 * morph);
    gAk = 0.16 * kick * (0.5 + k0);
    // The ring runs out from the source as the envelope falls.
    gRk = 0.5 + 7.0 * (1.0 - kick);
    gAs = 0.10 + 0.04 * swell;
    gAt = (0.015 + 0.035 * clamp(uTurbulence, 0.0, 2.0)) * (0.4 + 1.2 * k1);
    gWarp = 0.9 * clamp(uWarp, 0.0, 2.0);

    // Gradient bound of lake() (see the header): 0.1875 is the window's slope,
    // 2.15 the kick Gaussian's (2.5 * sqrt(2 / e)), 0.3 and 2.7 the swell's and
    // the chop's wavevector lengths.
    float lip = 2.0 * gAb * (gKb + 0.1875) + 4.0 * gAm * (gKm + 0.1875) + 2.0 * gAk * (2.15 + 0.1875)
              + gAs * 0.3 + gAt * 2.7;
    lip *= 1.0 + 0.3 * gWarp;
    float stepK = 0.9 / sqrt(1.0 + lip * lip);
    float amp = 2.0 * gAb + 4.0 * gAm + 2.0 * gAk + gAs + gAt;
    float waterTop = RP_WATER + amp;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Exact, invisible rebase (see the header): far down the 8192 travel a
    // highp z keeps ~0.001 of fraction, and the normals would speckle.
    ro.z = mod(ro.z, RP_ZWRAP);
    // Ride the swell: never closer than 0.45 to the water under the eye.
    ro.y = max(ro.y, RP_WATER + lake(ro.xz) + 0.45);

    float hue = 0.5 + 0.18 * pitchBump + 0.25 * (k3 - 0.5) + 0.03 * sin(uTime * 0.02);
    float fogK = 0.032 * (1.5 - k5);
    vec3 sky = skyColour(rd, hue);
    vec3 horizon = pal(hue + 0.6) * 0.05;
    vec3 col = sky;

    if (rd.y < -0.001) {
        // Nothing above the tallest possible crest can be water.
        float t = max((ro.y - waterTop) / -rd.y, 0.0);
        float hitT = -1.0;
        float tPrev = t;
        float gPrev = 1.0;
        for (int i = 0; i < RP_MAX_STEPS; i++) {
            // uSteps is the user's Detail; this loop's own bound is the constant.
            if (float(i) >= uSteps) break;
            if (t > RP_FAR) break;
            vec3 p = ro + rd * t;
            float gap = p.y - (RP_WATER + lake(p.xz));
            if (gap < 0.0015 * t + 0.001) {
                // Between the last two samples, for a smooth waterline.
                hitT = mix(tPrev, t, clamp(gPrev / max(gPrev - gap, 1e-4), 0.0, 1.0));
                break;
            }
            tPrev = t;
            gPrev = gap;
            t += max(gap * stepK, 0.004 + 0.002 * t);
        }
        // Out of Detail before landing: the flat lake, where the ray meets it.
        if (hitT < 0.0) hitT = (ro.y - RP_WATER) / -rd.y;

        vec3 p = ro + rd * hitT;
        float e = 0.02 + 0.0015 * hitT;
        float h0 = lake(p.xz);
        vec3 n = normalize(vec3(h0 - lake(p.xz + vec2(e, 0.0)), e, h0 - lake(p.xz + vec2(0.0, e))));
        // Far ripples are finer than a pixel: let them settle flat instead of shimmering.
        n = normalize(mix(n, vec3(0.0, 1.0, 0.0), smoothstep(25.0, RP_FAR, hitT)));
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = 0.03 + 0.97 * pow(1.0 - ndv, 5.0);
        vec3 r = reflect(rd, n);
        r.y = abs(r.y);
        vec3 deep = pal(hue + 0.55) * 0.010 + vec3(0.001, 0.002, 0.004);
        col = mix(deep, skyColour(r, hue) * 1.3, fres);

        // The flat style's light: the wave glows by its own height, crests and
        // troughs in the palette's two sides, still water black.
        float v = clamp(h0 / max(amp, 0.05), -1.0, 1.0);
        float glow = (0.30 + 0.8 * vol) * (0.5 + k4) * exp(-hitT * 0.025);
        col += pal(hue + v * 0.4) * abs(v) * glow;
        // The kick ring flares white-hot along its crest as it runs out.
        col += mix(pal(hue + 0.15), vec3(1.0), 0.4) * kick * smoothstep(0.35, 0.9, v) * 0.5 * glow;

        col = mix(col, horizon, 1.0 - exp(-hitT * fogK));
    }

    col *= 0.82 + 0.25 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
