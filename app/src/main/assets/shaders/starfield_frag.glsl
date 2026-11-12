#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uSoundTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3

// Starfield: a flight through a star-forming region. Three layers, near to far:
//
//   1. band-lit stars, a true 3D field of points the eye flies between (the
//      old field, kept: cell-hashed, walked cell by cell, so every star is a
//      fixed point in the world and parallax is real);
//   2. a nebula, ray-marched as an emission-absorption volume: wispy sheets
//      of glowing gas, lit from behind by the core star at the vanishing point,
//      self-shadowed by one sample toward that light, and fogging everything
//      behind it (the stars and sky dim by the gas's transmittance);
//   3. a sky of distant stars at infinity, which turns with the view but never
//      parallaxes, so depth reads at once against the near field.
//
// The camera is the shared rig (camFly): travel, look, banking, FOV and dolly
// are all the Camera tab's, and the composite leaves the picture alone.
//
// ---- audio: one sound, one property -----------------------------------------
//
// Spectrum (sndSpectrum)  each near star is lit by its own analyser band, its
//                         hue that band's place on the palette; and the nebula
//                         lays the spectrum along the flight, one band per slab
//                         of depth, so you fly from bass gas into treble gas.
// Hat (SND_HAT_ENV)       twinkles a hashed one star in seven, re-picked on
//                         every onset through floor(SND_HIT_TIME), and the far sky.
// Onset (SND_ONSET_ENV)   stretches near stars along z into streaks.
// Kick (SND_KICK_ENV)     the gas sheets swell thicker (PULSE scales it).
// Snare (SND_SNARE_ENV)   the core flares, rim-lighting the clouds from behind.
// Bass (SND_LOW)          the core glow itself, slew-limited.
// Drop (SND_HELD_SURGE)   the nebula parts (its cover falls) and its palette
//                         swings to the far side; on a hump, so it glides back.
// Mids (SND_MIDDLE)       turn the eye's look aside while the mids climb and
//                         ease it back as they fall, as lib_terra's terraTurn
//                         and lib_truchet's truchetTurn do: 0.1 x sndShaped x
//                         SND_DRIVE screen units (0.15 at most, 0 in silence),
//                         the gFlameTurn band on a structure that holds still.
//                         Mid gain scales it.
// Knobs                   k1 TEXTURE fine filament weight, k3 TONE gas hue,
//                         k4 BUILD gas glow, k5 SPACE thinner gas. All amplitudes,
//                         never rates, so a moving knob never jumps the picture.
// Params                  Warp bends the gas, Turbulence adds fine filaments,
//                         Morph thickens the sheets; Detail is the step budget.
//
// ---- why the wrap is invisible and the estimate safe ---------------------
//
// The rig's z wraps at 8192. Stars tile z with STAR_Z_MASK (8192 * STAR_SCALE
// cells), measured from the eye's own cell so float keeps its fraction. The
// nebula is built only from sines whose z periods are 32, 16 and 8 world
// units, and the eye is rebased by mod(z, 64) for it, so the rebase shifts
// every term by whole periods. The nebula is a density, not a distance: it is
// marched in fixed steps (Detail sets their count over a fixed depth), so no
// Lipschitz bound is needed and nothing can be over-stepped into; a ball
// around the eye is cleared of gas so the eye never sits in a solid fog.

/** Star cells per world unit. A power of two, so the z tiling below stays a power of two too. */
#define STAR_SCALE 4.0
/** The star field repeats along z every 8192 * STAR_SCALE cells (the rig's wrap), minus one, as an AND mask. */
#define STAR_Z_MASK 32767
/** Compile-time ceiling on the cell walk. uSteps scales the runtime budget. */
#define STAR_MAX_CELLS 48
/** Fraction of cells that hold a star. */
#define STAR_DENSITY 0.2
/** Most cell crossings per unit of ray length (|x|+|y|+|z|); fading at budget / this keeps every direction equally deep. */
#define STAR_CROSSINGS 1.75
/** A star's physical radius in cells, before the pixel floor. */
#define STAR_SIZE 0.012
/** Streak half-length in cells at full onset; the z jitter (0.45..0.55) leaves room for it inside the cell. */
#define STAR_STREAK 0.4

/** Compile-time ceiling on the nebula march; uSteps scales the runtime count. */
#define NB_MAX_STEPS 28
/** How deep the nebula march looks, world units. */
#define NB_FAR 26.0
/** The nebula's z rebase: every gas term's z period divides it. */
#define NB_ZWRAP 64.0
/** Large sheets: period 32 world units. */
#define NB_F1 (6.2831853 / 32.0)
/** Fine filaments: period 8. */
#define NB_F2 (6.2831853 / 8.0)
/** Domain warp: period 16. */
#define NB_FW (6.2831853 / 16.0)
/** Extinction per unit of density per world unit. */
#define NB_SIGMA 0.55
/** Gas-free ball around the eye, world units. */
#define NB_BUBBLE 0.7

/** Directional cells per unit of ray direction for the far sky. */
#define SKY_N 70.0
/** Fraction of sky cells that hold a far star. */
#define SKY_DENSITY 0.28

float gWarp;
float gDetail;
float gThick;
float gCover;
vec3 gDrift;
/** The eye with z rebased into 0..64 for the nebula (see main). */
vec3 gEye;

float knob(int i) { return clamp(uKnob[i], 0.0, 1.0) - 0.5; }

/** Gas density at `p` (nebula space), 0..gCover. */
float nebula(vec3 p) {
    // Each warp component reads another axis: z enters only through sin(p.z * NB_FW).
    vec3 q = p + gWarp * sin(p.yzx * NB_FW + gDrift);
    vec3 a = q * NB_F1;
    float g = dot(sin(a), cos(a.yzx));
    vec3 b = q * NB_F2 + vec3(1.7, 0.4, 2.3);
    g += gDetail * dot(sin(b), cos(b.yzx));
    float d = clamp(1.0 - abs(g) / gThick, 0.0, 1.0);
    d *= d;
    d *= smoothstep(NB_BUBBLE, NB_BUBBLE + 1.6, length(p - gEye));
    return d * gCover;
}

void main() {
    // view() first, so zoom, rotation, kaleidoscope and the rest still steer
    // the ray, then the rig turns that screen point into a world ray.
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    // The mids turn the look aside (see the header).
    camFly(uv - vec2(0.1 * sndShaped(SND_MIDDLE) * clamp(SND_DRIVE, 0.0, 1.0), 0.0), ro, rd);

    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float snare = clamp(SND_SNARE_ENV, 0.0, 1.0);
    float hat = clamp(SND_HAT_ENV, 0.0, 1.0);
    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float dropBump = 4.0 * drop * (1.0 - drop);
    float bass = sndShaped(SND_LOW);

    gWarp = 1.4 + 1.1 * clamp(uWarp, 0.0, 2.0);
    gDetail = clamp(0.32 + 0.3 * clamp(uTurbulence, 0.0, 2.0) + 0.4 * knob(1), 0.0, 1.0);
    gThick = (0.42 + 0.25 * clamp(uMorph, 0.0, 2.0)) * (1.0 + 0.55 * kick * (1.0 + 2.0 * knob(0)));
    gCover = clamp(1.0 - 0.9 * knob(5), 0.2, 1.6) * (1.0 - 0.7 * dropBump);
    // Plain multiples of uTime: the drift speeds with Speed and never jumps.
    gDrift = vec3(uTime * 0.05, uTime * 0.037, uTime * 0.029);
    gEye = vec3(ro.xy, mod(ro.z, NB_ZWRAP));

    float pixAngle = 2.0 / (max(uResolution.y, 1.0) * uCamFocal);
    // The core star sits on the world's +z, the direction of travel on the
    // glide and spiral paths: the vanishing point you fly toward, and the light.
    vec3 L = normalize(vec3(0.0, 0.18, 1.0));
    float coreDot = max(dot(rd, L), 0.0);
    float coreLight = 0.55 + 0.8 * bass + 1.3 * snare;
    float gasHue = 0.62 + 0.15 * knob(3) + 0.5 * dropBump + 0.03 * sin(uTime * 0.013);
    float glowGain = 1.0 + 0.8 * knob(4);

    // ---- nebula: emission-absorption march ---------------------------------
    float nb = clamp(uSteps * (float(NB_MAX_STEPS) / 128.0), 6.0, float(NB_MAX_STEPS));
    float dt = NB_FAR / nb;
    // A per-pixel start offset trades step banding for fine grain.
    float t = dt * (0.25 + 0.75 * hash13(vec3(gl_FragCoord.xy, 7.0)));
    float tau = 0.0;
    float tEnd = NB_FAR;
    vec3 gas = vec3(0.0);
    // Forward scattering: gas glows brightest looking into the light.
    float phase = 0.35 + 0.65 * pow(coreDot, 5.0);
    for (int i = 0; i < NB_MAX_STEPS; i++) {
        if (float(i) >= nb) break;
        vec3 p = gEye + rd * t;
        float d = nebula(p);
        if (d > 0.003) {
            // One sample toward the core: the gas between this point and the light.
            float dl = nebula(p + L * 0.9);
            float lit = exp(-2.4 * dl);
            // The spectrum laid along the flight: slab z/32 hears band fract(z/32).
            float band = fract(p.z * (1.0 / 32.0));
            float hears = 0.3 + 0.9 * sndSpectrum(0.04 + 0.92 * abs(2.0 * band - 1.0));
            vec3 emit = pal(gasHue + 0.22 * sin(p.z * (6.2831853 / NB_ZWRAP)) + 0.12 * d);
            vec3 c = emit * (0.10 * hears + lit * coreLight * phase * 0.55) * glowGain;
            float a = d * NB_SIGMA * dt;
            gas += exp(-tau) * c * a;
            tau += a;
            if (tau > 3.5) {
                tEnd = t;
                break;
            }
        }
        t += dt;
    }
    float trans = exp(-tau);

    // ---- near stars: the cell walk -----------------------------------------
    // Relative to the eye's own cell: ids are exact integers in float and
    // every distance is a [0, 1)-sized offset, not a world z in the thousands.
    vec3 sp = ro * STAR_SCALE;
    vec3 base = floor(sp);
    vec3 o = sp - base;
    ivec3 baseId = ivec3(base);

    vec3 stepDir = vec3(rd.x >= 0.0 ? 1.0 : -1.0, rd.y >= 0.0 ? 1.0 : -1.0, rd.z >= 0.0 ? 1.0 : -1.0);
    ivec3 stepId = ivec3(stepDir);
    vec3 tDelta = 1.0 / max(abs(rd), vec3(1e-5));
    vec3 side = (stepDir * (0.5 - o) + 0.5) * tDelta;
    ivec3 cell = ivec3(0);

    float streak = STAR_STREAK * clamp(SND_ONSET_ENV, 0.0, 1.0);
    float twinkleSeed = fract(floor(SND_HIT_TIME) * 0.618034);
    float budget = uSteps * (float(STAR_MAX_CELLS) / 128.0);
    float far = budget / STAR_CROSSINGS;

    vec3 stars = vec3(0.0);
    for (int i = 0; i < STAR_MAX_CELLS; i++) {
        if (float(i) >= budget) break;
        ivec3 id = baseId + cell;
        id.z = id.z & STAR_Z_MASK;
        uint h = uhash3(uvec3(id));
        float hOcc = uhashUnit(h);
        uint h2 = uhash(h);
        uint h3 = uhash(h2);
        uint h4 = uhash(h3);
        uint h5 = uhash(h4);
        // Jittered inside the cell with a margin, so a glow is never cut by a wall the ray did not cross.
        vec3 star = vec3(cell) + vec3(0.25 + 0.5 * uhashUnit(h2), 0.25 + 0.5 * uhashUnit(h3), 0.45 + 0.1 * uhashUnit(h4));
        float hue = uhashUnit(h5);

        vec3 rel = star - o;
        float tc = max(dot(rel, rd), 0.0);
        vec3 dv = rd * tc - rel;
        float size = STAR_SIZE + tc * pixAngle * 1.4;
        // The onset streak: an ellipsoid along z, the travel axis, which on
        // screen is a radial smear from the vanishing point.
        dv.z *= size / (size + streak);
        float glow = exp(-dot(dv, dv) / (size * size));

        float fade = (1.0 - smoothstep(far * 0.35, far, tc)) * smoothstep(0.05, 0.7, tc);
        float twinkle = 1.0 + 1.8 * hat * step(0.86, fract(hue * 7.31 + twinkleSeed));
        float lit = (0.35 + sndSpectrum(hue)) * twinkle;
        // Fogged by the gas in front of it: optical depth taken as linear in
        // depth across the marched span, a cheap stand-in for the true profile.
        float fog = exp(-tau * clamp(tc / (STAR_SCALE * tEnd), 0.0, 1.0));
        stars += pal(hue + tc * 0.01) * glow * fade * lit * fog * step(hOcc, STAR_DENSITY);

        // Step into whichever neighbouring cell the ray reaches first.
        vec3 m = step(side, side.yzx) * step(side, side.zxy);
        side += m * tDelta;
        cell += ivec3(m) * stepId;
    }

    // ---- far sky: stars at infinity ----------------------------------------
    vec3 v = rd * SKY_N;
    vec3 sc = floor(v);
    uint hs = uhash3(uvec3(ivec3(sc) + ivec3(4096)));
    uint hs2 = uhash(hs);
    uint hs3 = uhash(hs2);
    uint hs4 = uhash(hs3);
    vec3 spos = sc + 0.35 + 0.3 * vec3(uhashUnit(hs2), uhashUnit(hs3), uhashUnit(hs4));
    float sd = length(rd - normalize(spos)) * SKY_N;
    float sr = max(0.07, 1.3 * SKY_N * pixAngle);
    float skyTw = 1.0 + 1.2 * hat * step(0.8, fract(uhashUnit(hs4) * 5.3 + twinkleSeed));
    float sky = exp(-sd * sd / (sr * sr)) * min(1.0, 0.07 / sr) * step(uhashUnit(hs), SKY_DENSITY)
              * (0.25 + 0.75 * uhashUnit(hs2)) * skyTw;
    vec3 skyCol = mix(vec3(1.0), pal(uhashUnit(hs3)), 0.4) * sky * 0.9;

    // ---- core glow and composite -------------------------------------------
    float core = rd.z > 0.0 ? exp(-8.0 * length(rd.xy) / rd.z) : 0.0;
    vec3 coreCol = pal(0.5) * core * (0.2 + bass * 0.8 + 0.5 * snare);
    // A faint deep-space haze where the gas thins, so the background is not pure black.
    vec3 haze = pal(gasHue + 0.5) * 0.012;

    vec3 col = gas + stars + (skyCol + coreCol + haze) * trans;
    fragColor = vec4(grade(col), 1.0);
}
