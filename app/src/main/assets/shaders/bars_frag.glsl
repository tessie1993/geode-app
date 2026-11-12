#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uSoundTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Morph, Warp and Turbulence are spent on the towers themselves (see below),
// not on the rays.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_shell
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3

// Bars: the spectrum analyser as a city. An avenue runs straight down +z, and
// on both sides of it stand rows of square towers, one analyser bar per lane:
// 24 lanes a side, so the 48 bars of the flat analyser, mirrored about the
// road. The lane by the kerb is the top of the spectrum and the outermost lane
// the bass, so the loud low end rises into walls at the edges of the view and
// the treble glitters low along the road. Every row down z is the same
// spectrum, each tower a little taller or shorter by its own hash, so the
// city reads as a skyline rather than a copied strip. The towers are lit from
// inside: rows of window bands in the bar's palette colour, a glowing roof,
// and the glow above each bar the old analyser had, gathered along the ray.
// The street is black glass and reflects the whole city, dimmed and fading
// with distance, as the old row was reflected in its floor.
//
// The camera is the shared rig (camFly): the eye rides the rig's path down
// the avenue, whose half-width of 4 holds the glide path's +-3 wander. Every z
// term has a power-of-two period (cells hash with period 64, the snare's wave
// has period 16), so the eye is rebased by 64 and the rig's 8192 wrap is
// invisible. The orbit path can carry the eye into the first lane; a sphere
// around the eye is carved out of everything, so it never sits in a tower.
//
// ---- audio ------------------------------------------------------------------
//
// Each tower's height is its lane's analyser band (sndEq), as each bar's was.
// Every tower wears a peak cap. On each beat (SND_KICK_TOGGLE) the caps are
// thrown up above their towers, by as much as their band region (the seven
// regions of viz/Reactions) is loud, and fall back onto them as the beat
// envelope decays, accelerating, like something dropped.
// The snare (SND_SNARE_ENV) rolls a travelling wave through the city: tower
// heights ride up and down it, across the lanes and down the avenue.
// Overall level rides SND_ENERGY_TREND; nothing else brightens on a hit.
//
// ---- the controls ------------------------------------------------------------
//
// Morph rounds the towers' corners, from square blocks to cylinders. Warp
// turns each tower about its own axis by a hashed angle. Turbulence widens
// the hashed height scatter, from an even row to a ragged skyline.
// The knobs (lib_scene_uniforms' uKnob, read as offsets from 0.5):
//   k0 PULSE    how far the beat throws the caps
//   k1 TEXTURE  how many window bands a tower has
//   k2 FLOW     a slow standing swell of the snare's wave, with or without snare
//   k3 TONE     the skyline's tilt across the spectrum: a high melody lifts
//               the treble towers by the kerb and lowers the bass walls at the
//               edges, a low one the reverse, so the city leans with the tune
//               (and a small hue offset, as before)
//   k4 BUILD    the tower height per unit of band level
//   k5 SPACE    the fog: the far end of the avenue clears or closes in, the
//               road's width and how much of the city the street reflects
// Every variable above, and the tower width, the halo and the wave's spacing,
// is one normalized input (lib_shell) set in one block at the top of main from
// these knobs and params and mapped there to its range: at rest (knobs 0.5,
// Morph, Warp and Turbulence 0) each is the constant the city was tuned with.
// The city itself is the field the grow hooks act on (SHELL_FIELD3): Spawn
// slides a second city half a row down the avenue, Fractal grows towers a
// third the size on the towers, the layers open the near and far blocks.
//
// ---- why the estimate is safe ----------------------------------------------
//
// Each cell measures only its own tower and cap. Every footprint, at any
// Warp angle, lies inside a circle of radius 0.46 about its cell centre, so a
// neighbour's tower is at least the distance to the cell boundary plus 0.04
// away; and every top is clamped below BARS_HMAX, so above that height no
// tower is nearer than the height difference. On the road and past the
// outermost lane the nearest tower is at least the distance to the first or
// last lane. The street is a plane. All are lower bounds, and so is their max.

#define BARS_MAX_STEPS 96
#define BARS_REFL_STEPS 40
#define BARS_FAR 48.0
#define BARS_TAU 6.2831853
/** The street plane. */
#define BARS_FLOOR -2.0
/** The road's half-width at rest: the first lane's cells start here (gRoad, SPACE). */
#define BARS_ROAD 4.0
/** Lanes a side: 24, so 48 bars across, as before. */
#define BARS_LANES 24.0
/** Footprint half-size at rest; at most 0.319 (gWidth), so its diagonal, at any angle, stays inside 0.46. */
#define BARS_R 0.28
/** A neighbour's footprint is at least this far past the shared cell edge (0.5 - 0.46). */
#define BARS_MARGIN 0.04
/** Tower height above the street per unit of band level. */
#define BARS_GAIN 3.2
/** Every tower top and cap stays below this height (world y). */
#define BARS_HMAX 6.5
/** How far a beat throws a cap above its tower, at full region level. */
#define BARS_CAP_THROW 1.4
/** Cap thickness and its rest gap above the roof. */
#define BARS_CAP_H 0.08
#define BARS_CAP_GAP 0.06
/** Reflection strength in the street at rest (SPACE moves it). */
#define BARS_REFLECT 0.35
#define BARS_BUBBLE 0.8
/** Cells along z before the hash repeats: 64 world units, which divides 8192. */
#define BARS_ZWRAP 64.0

/** The hit cell's band (0 bass .. 1 treble), level and roof, set by map(). */
float gSeg;
float gLvl;
float gTop;
/** 0 street, 1 tower, 2 cap. */
float gMat;
/** The eye, z rebased into 0..64 (see main). */
vec3 gEye;

// Per-frame values, set once in main so map() does not recompute them.
float gBeatFall;
float gThrow;
float gWaveAmp;
float gGain;
float gScatter;
float gRound;
float gTurn;
float gTilt;
float gRoad;
float gWidth;
float gWaveK;

/**
 * The band region a bar position 0..1 falls in, read from the sound frame:
 * viz/Reactions' seven regions over the analyser's 64 bands (boundaries at
 * bands 8, 16, 26, 36, 46 and 55).
 */
float barsRegion(float x) {
    if (x < 0.125) return sndSpectrum(0.10);
    if (x < 0.25) return sndLevel(SND_BASS_ATT);
    if (x < 0.40625) return sndSpectrum(0.41);
    if (x < 0.5625) return sndLevel(SND_MID_ATT);
    if (x < 0.71875) return sndSpectrum(0.70);
    if (x < 0.859375) return sndShaped(SND_HIGH);
    return sndSpectrum(0.93);
}

/** A prism of footprint distance s standing between y0 and y1. */
float barsPrism(float s, float y, float y0, float y1) {
    vec2 w = vec2(s, max(y - y1, y0 - y));
    return min(max(w.x, w.y), 0.0) + length(max(w, 0.0));
}

/** The city: the field the grow hooks act on (map() hands it to SHELL_FIELD3). */
float barsField(vec3 p) {
    // SPACE widens the road by sliding both sides' lanes out (or in) whole: a shift of x, exact
    // outside the road; inside it the shifted |x| is the smaller, so every bound below stays a bound.
    p.x = sign(p.x) * (abs(p.x) - (gRoad - BARS_ROAD));
    vec2 cell = floor(p.xz);
    vec2 cc = cell + 0.5;
    vec2 f = p.xz - cc;
    float ax = abs(cc.x);
    float lane = ax - (BARS_ROAD + 0.5);

    gSeg = 0.0;
    gLvl = 0.0;
    gTop = BARS_FLOOR;
    gMat = 0.0;
    float dOwn = 1e9;
    if (lane >= 0.0 && lane < BARS_LANES) {
        uint u = uhash3(uvec3(ivec3(int(cell.x), 0, int(mod(cell.y, BARS_ZWRAP)))));
        float h = uhashUnit(u);
        float h2 = uhashUnit(uhash(u));
        // The kerb lane is the top of the spectrum, the outer lane the bass.
        float seg = 1.0 - (lane + 0.5) / BARS_LANES;
        float lvl = clamp(0.5 * sndEq(seg), 0.0, 1.5);
        float wave = gWaveAmp * sin(lane * gWaveK + cc.y * (BARS_TAU / 16.0) - uTime * 3.0);
        // TONE leans the skyline: seg 1 is the kerb (treble), 0 the outer bass lane.
        float lean = max(1.0 + gTilt * (2.0 * seg - 1.0), 0.15);
        float top = BARS_FLOOR + 0.15 + gGain * lvl * lean * (1.0 + gScatter * (2.0 * h - 1.0)) + wave;
        top = clamp(top, BARS_FLOOR + 0.05, BARS_HMAX - BARS_CAP_GAP - BARS_CAP_H - 0.3);
        // The cap: thrown up on the beat and falling back with gravity's shape.
        float capY = min(top + BARS_CAP_GAP + gThrow * gBeatFall * barsRegion(seg), BARS_HMAX - BARS_CAP_H);

        // Rounded-rect footprint, turned by Warp. Half-size <= 0.319, so the
        // corner never leaves the 0.46 circle the neighbour bound assumes.
        float r = gWidth * (0.92 + 0.14 * h2);
        vec2 q = abs(rot2(gTurn * (h2 - 0.5)) * f);
        float rr = r * gRound;
        float s = length(max(q - (r - rr), 0.0)) + min(max(q.x, q.y) - (r - rr), 0.0) - rr;

        float dT = barsPrism(s, p.y, BARS_FLOOR - 1.0, top);
        float dC = barsPrism(s, p.y, capY, capY + BARS_CAP_H);
        dOwn = min(dT, dC);
        gSeg = seg;
        gLvl = lvl;
        gTop = top;
        gMat = dC < dT ? 2.0 : 1.0;
    }

    // Lower bounds on every other cell's tower, combined by max.
    vec2 af = abs(f);
    float other = 0.5 - max(af.x, af.y) + BARS_MARGIN;
    other = max(other, BARS_ROAD + BARS_MARGIN - abs(p.x));
    other = max(other, abs(p.x) - (BARS_ROAD + BARS_LANES) + BARS_MARGIN);
    other = max(other, p.y - BARS_HMAX);
    float d = min(dOwn, other);

    float df = p.y - BARS_FLOOR;
    if (df < d) gMat = 0.0;
    return min(d, df);
}

float map(vec3 p) {
    return max(SHELL_FIELD3(p, barsField), BARS_BUBBLE - length(p - gEye));
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

vec3 skyColour(vec3 rd, float hue) {
    float up = clamp(rd.y, -1.0, 1.0);
    vec3 col = mix(pal(hue + 0.55) * 0.07, pal(hue + 0.70) * 0.015, smoothstep(0.0, 0.6, up));
    // The glow at the end of the avenue, straight down the travel axis.
    float ahead = max(dot(rd, normalize(vec3(0.0, 0.08, 1.0))), 0.0);
    col += pal(hue + 0.5) * (0.22 * pow(ahead, 14.0) + 0.05 * ahead * ahead);
    return col;
}

/**
 * The light a tower or cap gives off at p, from the globals map() left: the
 * window bands and roof of the old bar's cells, in its palette colour. The
 * reflection reads this alone; the direct view adds the lit facade.
 */
vec3 emissive(vec3 p, vec3 n, float hue, float bands) {
    if (gMat > 1.5) return pal(gSeg + hue + 0.12) * 0.9;
    float y = p.y - BARS_FLOOR;
    float cells = step(fract(y * bands), 0.72);
    float roof = smoothstep(0.6, 0.9, n.y);
    // Brightest just under the roof, like the old bar's glow at its top.
    float crown = exp(-max(gTop - p.y, 0.0) * 1.2);
    vec3 c = pal(gSeg + hue);
    return c * (cells * (0.25 + 0.75 * gLvl) * (0.45 + 0.55 * crown) * (1.0 - roof) + roof * (0.5 + gLvl));
}

void main() {
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 64, so this shift is invisible
    // and exact (power of two). Far down the 8192 travel a highp z otherwise
    // keeps ~0.001 of fraction, and the cells and normals quantise.
    ro.z = mod(ro.z, BARS_ZWRAP);
    gEye = ro;
    // The layers are measured from the eye; a spawned city slides half a row down the avenue.
    shellSetup3(ro, 3.0, 0.5, false);

    // ---- the shape inputs (lib_shell): each 0..1, 0.5 the tuned look --------------------
    float nThrow = shellKnob(0);                              // PULSE    the caps' throw
    float nBands = shellKnob(1);                              // TEXTURE  window bands, tower width
    float nSwell = shellKnob(2);                              // FLOW     the wave's standing swell, its spacing
    float nTilt = shellKnob(3);                               // TONE     the skyline's lean, the hue
    float nGain = shellKnob(4);                               // BUILD    height per level, the halo
    float nSpace = shellKnob(5);                              // SPACE    fog, road width, reflection
    float nRound = 0.5 + 0.5 * clamp(uMorph, 0.0, 1.0);       // Morph    square blocks to cylinders
    float nTurn = 0.5 + 0.25 * clamp(uWarp, 0.0, 2.0);        // Warp     each tower's turn
    float nScatter = 0.5 + 0.5 * clamp(uTurbulence, 0.0, 1.0); // Turbulence the height scatter
    gThrow = shellMap(nThrow, 0.4 * BARS_CAP_THROW, BARS_CAP_THROW, 1.6 * BARS_CAP_THROW);
    float swellAmp = shellMap(nSwell, 0.0, 0.0, 0.25);
    gWaveK = shellMap(nSwell, 0.5, 0.7, 0.95);
    gGain = shellMap(nGain, 0.6 * BARS_GAIN, BARS_GAIN, 1.4 * BARS_GAIN);
    float haloK = shellMap(nGain, 0.012, 0.02, 0.03);
    gScatter = shellMap(nScatter, 0.12, 0.12, 0.47);
    gRound = shellMap(nRound, 0.0, 0.0, 1.0);
    gTurn = shellMap(nTurn, 0.0, 0.0, 3.2);
    gTilt = shellMap(nTilt, -0.8, 0.0, 0.8);
    float hue = shellMap(nTilt, -0.075, 0.0, 0.075);
    float bands = shellMap(nBands, 1.65, 3.0, 4.35);
    gWidth = shellMap(1.0 - nBands, 0.2, BARS_R, 0.315);
    float fog = shellMap(nSpace, 0.0595, 0.035, 0.0105);
    gRoad = shellMap(nSpace, 3.6, BARS_ROAD, 4.8);
    float reflectK = shellMap(nSpace, 0.22, BARS_REFLECT, 0.5);

    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float beat = clamp(SND_KICK_TOGGLE, 0.0, 1.0);
    // 1 - (1 - e)^2 holds near the top and then drops faster and faster.
    gBeatFall = 1.0 - (1.0 - beat) * (1.0 - beat);
    gWaveAmp = 0.7 * clamp(SND_SNARE_ENV, 0.0, 1.0) + swellAmp;

    float t = 0.05;
    float hitT = -1.0;
    float glow = 0.0;
    float glowSeg = 0.0;
    // The halo's depth, weighted like the halo, to colour it by how far off it hangs.
    float glowT = 0.0;
    for (int i = 0; i < BARS_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * t;
        float d = map(p);
        if (d < 0.0015 + 0.0008 * t) {
            hitT = t;
            break;
        }
        // The halo around each lit tower, gathered as the ray passes it; one
        // palette read at the end, weighted by where the light came from.
        float w = exp(-max(d, 0.0) * 5.0) * gLvl * haloK;
        glow += w;
        glowSeg += w * gSeg;
        glowT += w * t;
        t += d * 0.9;
        if (t > BARS_FAR) break;
    }

    vec3 sky = skyColour(rd, hue);
    vec3 col = sky;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.001 * hitT));
        // Restore the hit cell's globals after the normal taps.
        map(p);
        vec3 L = normalize(vec3(-0.4, 0.8, -0.3));
        float dif = max(dot(n, L), 0.0);
        if (gMat < 0.5) {
            // The street: black glass with faint lane lines, reflecting the city.
            // On the lanes' own x (slid by SPACE, as barsField slides them).
            float lx = abs(p.x) - (gRoad - BARS_ROAD);
            vec2 g = abs(fract(vec2(lx, p.z)) - 0.5);
            float lines = (1.0 - smoothstep(0.0, 0.02, 0.5 - max(g.x, g.y))) * step(BARS_ROAD, lx);
            col = pal(hue + 0.6) * (0.012 + 0.05 * lines);
            float fres = 0.25 + 0.75 * pow(1.0 - clamp(-rd.y, 0.0, 1.0), 4.0);
            vec3 rr = reflect(rd, vec3(0.0, 1.0, 0.0));
            vec3 ro2 = p + vec3(0.0, 0.01, 0.0);
            float t2 = 0.02;
            float hit2 = -1.0;
            for (int i = 0; i < BARS_REFL_STEPS; i++) {
                if (float(i) >= uSteps * 0.4) break;
                float d2 = map(ro2 + rr * t2);
                if (d2 < 0.003 + 0.002 * t2) {
                    hit2 = t2;
                    break;
                }
                t2 += d2 * 0.9;
                if (t2 > BARS_FAR * 0.5) break;
            }
            vec3 refl = skyColour(rr, hue);
            if (hit2 > 0.0 && gMat > 0.5) {
                vec3 p2 = ro2 + rr * hit2;
                // The roof faces down the reflected ray never show, so a flat
                // side normal is enough for the bands.
                refl = emissive(p2, vec3(0.0), hue, bands) * exp(-hit2 * 0.25);
            }
            col += refl * reflectK * fres;
        } else {
            vec3 e = emissive(p, n, hue, bands);
            vec3 facade = pal(gSeg + hue + 0.5) * (0.02 + 0.05 * dif);
            float spec = pow(max(dot(reflect(rd, n), L), 0.0), 32.0);
            // A rim light on every edge that turns away from the eye, so each
            // tower stands off the one behind it (the Hollows' edge light).
            float rim = pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 3.0);
            col = e + facade + pal(gSeg + hue) * spec * 0.2 + pal(gSeg + hue + 0.5) * rim * 0.22;
        }
        // Depth haze: the air's colour runs on round the palette with distance,
        // so the near blocks and the far end of the avenue read as layers.
        col = mix(col, skyColour(rd, hue + 0.3 * hitT / BARS_FAR), 1.0 - exp(-hitT * max(fog, 0.004)));
    }
    float glowFar = glowT / max(glow * BARS_FAR, 1e-4);
    col += pal(glowSeg / max(glow, 1e-4) + hue + 0.25 * glowFar) * glow * (0.6 + 0.3 * swell);

    col *= 0.8 + 0.25 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
