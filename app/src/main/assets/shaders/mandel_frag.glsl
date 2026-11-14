#version 300 es
// The shape controls are spent on the landscape (slope, terraces, the c-plane
// wave), not on the rays, so view() must not bend the picture a second time.
#define VIEW_OWNS_SHAPE
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uSoundTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_camera
//#include lib_shell
//#include lib_palette
//#include lib_scene_grade

// Mandel: the Mandelbrot set as a landscape, flown over.
//
// The c-plane is laid flat: world x is Im(c), world z is Re(c), one world unit
// is MB_CS of c. The set itself is a black obsidian mesa (its interior never
// escapes, so it is a flat plateau at MB_TOP), and everything outside falls
// away from it in cliffs whose height is the Mandelbrot distance estimate:
// the further a point is from the set, the deeper it sits, down to the floor
// of the abyss MB_DEPTH below. The filaments and minibrots become ridges, the
// escape bands of the old flat picture are painted on the cliffs (and, with
// Turbulence, cut into them as terraces). Islands repeat head to tail every
// MB_TILE = 32 world units along z, so the flight goes antenna, the
// period-2 bulb, Seahorse Valley, the cardioid, out over the abyss, and the
// next island's antenna, the cliffs between them sloping into the abyss. 32 divides 8192, so the rig's travel wrap is
// invisible. The island is centred on the origin, so the orbit and hover
// paths circle Seahorse Valley.
//
// ---- camera -------------------------------------------------------------------
//
// The shared rig, camFly. Every path keeps the eye within |y| <= 2 (glide and
// spiral wander, orbit bobs, hover drifts), and the whole landscape lies
// below MB_TOP = -2.8, so the eye can never be inside it and nothing has to
// be carved around it. The dive is the rig's own travel; there is no UV zoom.
//
// ---- audio ------------------------------------------------------------------
//
// The same moves the flat style made, in depth:
//   - a pitch change (SND_HELD_NOTE_CHANGE) glides the islands out to a
//     hashed spot in c and back, re-picked per onset (pitchGlide).
//   - an onset (SND_ONSET_ENV) steps the palette's hue phase.
//   - the kick (SND_KICK_ENV) steepens the cliffs (geometry, not light): the
//     whole landscape breathes in toward the mesa and out. k0 PULSE scales it.
//   - sub bass (SND_SUB_ATT) shifts the escape bands, and each band is lit by
//     its own analyser band (sndSpectrum of the band's iteration fraction).
//   - overall level rides SND_ENERGY_TREND.
// Controls: Morph eases the cliffs from steep to gentle, Turbulence (+ k1
// TEXTURE) cuts the escape-band terraces, Warp waves the c-plane. Knobs:
// k2 FLOW band scroll, k3 TONE the relief (a high melody raises the cliffs
// into steep spires and gorges, a low one lays them down as soft dunes; plus a
// small hue offset), k4 BUILD glow, k5 SPACE fog.
// Every one of these, and the c-plane's scale (SPACE: how much of the set one
// world unit holds) and the terraces' frequency (TEXTURE), is one normalized
// input (lib_shell) set in one block at the top of main and mapped there to
// its range; at rest each is the constant the landscape was tuned with. The
// landscape is the field the grow hooks act on (its shell wrapper): Spawn slides a
// second chain of islands down the flight, Fractal grows islands a third the
// size on the cliffs, the layers open the near and far landscape. A hook that
// is on starts the march at the eye instead of the slab above the mesa.
//
// ---- why the estimate is safe ----------------------------------------------
//
// G = 0.5 |z| log|z| / |z'| is a lower bound of the distance to the set in
// the c-plane (Koebe 1/4). The solid is y < h(x, z) with
// h = MB_TOP - K dw + A sin(MB_TERR dw), dw = G / MB_CS; its slope is at most
// L = K + A MB_TERR, so (y - h) / sqrt(1 + L^2) is 1-Lipschitz and a lower
// bound, and the march still steps at 0.8 of it for the warp's stretch and the
// estimate's own wobble. A <= K / MB_TERR (A is scaled down with K when TONE
// takes K under 2) keeps h <= MB_TOP, so y - MB_TOP is
// a second lower bound; above the mesa map() returns it without iterating.
// The floor is a plane. A tile only measures its own island, so each tile
// also ramps its cliffs down below the floor within MB_SEAM of its seams
// (a second 1-Lipschitz bound, joined by max: the solid is the
// intersection). By symmetry the neighbour holds nothing above the floor
// within MB_SEAM of the seam either, so a point at seam distance s is at
// least s + MB_SEAM from it (and never closer than y - MB_TOP): that is dNbr.

#define MB_MAX_STEPS 96
#define MB_ITERS 64
/** c per world unit at rest (gCs: SPACE); 0.085..0.12 keeps the island inside its tile. */
#define MB_CS 0.1
/** Re(c) at the island's centre (world z = 0): between the period-2 bulb and the cardioid. */
#define MB_CX -0.85
/** One island per this many world units along z. A power of two: it divides the 8192 wrap. */
#define MB_TILE 32.0
#define MB_TOP -2.8
#define MB_DEPTH 6.0
#define MB_FLOOR (MB_TOP - MB_DEPTH)
/** Terrace frequency in world units of distance from the set, at rest (gTerrF: TEXTURE). */
#define MB_TERR 8.0
#define MB_FAR 64.0
/** Nothing stands above the floor this close to a tile seam. */
#define MB_SEAM 1.0
/** Slope of the ramp that takes the cliffs down into the floor before a seam. */
#define MB_RAMP 2.0

/** The escape picture of the last map() call: smooth iteration fraction, interior flag, which surface. */
float gIter;
float gInside;
float gMat;
/** Per frame. */
float gK;
float gTerr;
float gWarp;
vec2 gGlide;
float gCs;
float gTerrF;

/**
 * A hashed point in the unit disc for event number `n` (PCG integer hash, so
 * it is exact on every device). Used to pick where the next glide goes.
 */
vec2 hashSpot(float n) {
    uint h = uint(max(n, 0.0)) * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    h = (h >> 22u) ^ h;
    float a = float(h & 0xffffu) * (6.2831853 / 65536.0);
    float r = sqrt(float(h >> 16u) * (1.0 / 65536.0));
    return vec2(cos(a), sin(a)) * r;
}

/**
 * The pitch-change glide, without frame-to-frame state.
 *
 * The SPOT is chosen by the onset count and eased from the previous count's
 * spot to the new one by the onset envelope: when an onset lands,
 * floor(SND_HIT_TIME) steps and SND_ONSET_ENV jumps to at least 0.4 on the
 * same frame, so the mix, measured from 0.4, starts exactly at the old spot
 * and glides to the new one as the envelope falls. HOW FAR toward the spot is
 * the pitch-change envelope shaped into a hump (4e(1-e)), 0 before the event
 * and at its instant rise, so the islands ease out and back, never snap.
 */
vec2 pitchGlide() {
    float g = (1.0 - smoothstep(0.05, 0.4, clamp(SND_ONSET_ENV, 0.0, 1.0)));
    vec2 spot = mix(hashSpot(mod(floor(SND_HIT_TIME) - 1.0, 4096.0)), hashSpot(floor(SND_HIT_TIME)), g);
    float e = clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0);
    return spot * (4.0 * e * (1.0 - e));
}

/** Distance to the set in world units (0 inside), and the smooth escape fraction in gIter. */
float setDistance(vec2 c) {
    gIter = 1.0;
    gInside = 1.0;
    // Exact interior tests for the main cardioid and the period-2 bulb: the
    // whole mesa top skips its 64 iterations.
    vec2 q = c - vec2(0.25, 0.0);
    float qq = dot(q, q);
    if (qq * (qq + q.x) < 0.25 * c.y * c.y) return 0.0;
    vec2 b = c + vec2(1.0, 0.0);
    if (dot(b, b) < 0.0625) return 0.0;

    vec2 z = vec2(0.0);
    vec2 dz = vec2(0.0);
    float n = 0.0;
    float r2 = 0.0;
    for (int i = 0; i < MB_ITERS; i++) {
        // z' = 2 z z' + 1, then z = z^2 + c.
        dz = 2.0 * vec2(z.x * dz.x - z.y * dz.y, z.x * dz.y + z.y * dz.x) + vec2(1.0, 0.0);
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        r2 = dot(z, z);
        if (r2 > 256.0) break;
        n += 1.0;
    }
    if (r2 <= 256.0) return 0.0;
    gInside = 0.0;
    float r = sqrt(r2);
    // Smooth count: log2(log r) is continuous across the bailout.
    float sm = n + 1.0 - log2(max(log(r), 1e-3));
    gIter = clamp(sm / float(MB_ITERS), 0.0, 1.0);
    return 0.5 * r * log(r) / max(length(dz), 1e-6) / gCs;
}

/** The landscape: the field the grow hooks act on (map() hands it to its shell wrapper). */
float mandelField(vec3 p) {
    gMat = 0.0;
    float above = p.y - MB_TOP;
    // Nothing reaches above the mesa: a valid bound, and no iteration at all.
    if (above > 1.0) {
        gInside = 1.0;
        return above;
    }

    // Seam-centred tile coordinate: the island's centre is at u = 0.
    float u = p.z - MB_TILE * floor(p.z / MB_TILE + 0.5);
    vec2 c = vec2(MB_CX + u * gCs, p.x * gCs) + gGlide;
    if (gWarp > 0.001) c += gWarp * 0.03 * vec2(sin(c.y * 5.0 + uTime * 0.4), cos(c.x * 5.0 + uTime * 0.33));

    float dw = setDistance(c);
    float h = MB_TOP - gK * dw + gTerr * sin(gTerrF * dw);
    float L = gK + gTerr * gTerrF;
    float dCliff = max((p.y - h) / sqrt(1.0 + L * L), above);
    float seam = 0.5 * MB_TILE - abs(u);
    // The flanks off the cusp reach the seam (the estimate there is still
    // under 2 world units), so they slope into the floor before it.
    float dRamp = (p.y - MB_FLOOR - MB_RAMP * (seam - MB_SEAM)) / sqrt(1.0 + MB_RAMP * MB_RAMP);
    dCliff = max(dCliff, dRamp);
    float dFloor = p.y - MB_FLOOR;
    float dNbr = max(seam + MB_SEAM, above);
    if (dFloor < dCliff) gMat = 1.0;
    return min(min(dCliff, dFloor), dNbr);
}

/** mandelField through lib_shell's hooks: exactly mandelField(p) at rest, and mandelField read once, in a loop the compiler cannot unroll. */
float mandelFieldGrown(vec3 p) {
    shellStart3(p, SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK);
    float r = 0.0;
    for (int i = SHELL_FROM; i < 7; i++) {
        if (shellPass3(i)) r = shellKeep3(i, mandelField(shellAt3(i)));
    }
    return r;
}

float map(vec3 p) {
    return mandelFieldGrown(p);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

vec3 skyColour(vec3 rd, float hue) {
    float up = clamp(rd.y, -1.0, 1.0);
    vec3 col = mix(pal(hue + 0.6) * 0.08, pal(hue + 0.75) * 0.015, smoothstep(-0.1, 0.6, up));
    // The glow down the chain of islands, where the flight is going.
    float ahead = max(dot(rd, normalize(vec3(0.0, -0.08, 1.0))), 0.0);
    col += pal(hue + 0.1) * (0.22 * pow(ahead, 12.0) + 0.05 * ahead * ahead);
    return col;
}

void main() {
    vec2 uv = view();

    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float sub = sndLevel(SND_SUB_ATT);
    float hueStep = 0.12 * clamp(SND_ONSET_ENV, 0.0, 1.0);

    // ---- the shape inputs (lib_shell): each 0..1, 0.5 the tuned look --------------------
    float nPulse = shellKnob(0);                                   // PULSE    the kick's steepening
    float nTexture = shellKnob(1);                                 // TEXTURE  the terraces: depth and frequency
    float nFlow = shellKnob(2);                                    // FLOW     the bands' scroll
    float nTone = shellKnob(3);                                    // TONE     the relief, the hue
    float nBuild = shellKnob(4);                                   // BUILD    the glow
    float nSpace = shellKnob(5);                                   // SPACE    the fog, the c-plane's scale
    float nMorph = 0.5 + 0.5 * clamp(uMorph, 0.0, 1.0);            // Morph    steep cliffs to gentle
    float nWarp = 0.5 + 0.5 * clamp(uWarp, 0.0, 1.0);              // Warp     the c-plane's wave
    float nTurb = 0.5 + 0.5 * clamp(uTurbulence / 1.5, 0.0, 1.0);  // Turbulence the terraces
    float kickK = shellMap(nPulse, 0.0, 1.0, 2.0);
    float terrTex = shellMap(nTexture, 0.0, 0.25, 0.5);
    gTerrF = shellMap(nTexture, 6.0, MB_TERR, 10.0);
    float flowK = shellMap(nFlow, 0.25, 1.0, 1.75);
    float relief = shellMap(nTone, 0.55, 1.0, 1.45);
    float hueTone = shellMap(nTone, -0.125, 0.0, 0.125);
    float glowK = shellMap(nBuild, 0.5, 0.8, 1.1);
    float fogK = shellMap(nSpace, 0.049, 0.035, 0.021);
    gCs = shellMap(nSpace, 0.12, MB_CS, 0.085);
    float steep = shellMap(nMorph, 3.4, 3.4, 2.0);
    gWarp = shellMap(nWarp, 0.0, 0.0, 1.0);
    float terrTurb = shellMap(nTurb, 0.0, 0.0, 0.6);

    // Morph eases the cliffs from steep to gentle; K stays >= 2, which the
    // terrace limit relies on. The kick only steepens.
    gK = steep * (1.0 + 0.16 * kick * kickK) * relief;
    // A <= 2 / MB_TERR <= K / MB_TERR: the terraces never rise above the mesa. A faster terrace
    // (TEXTURE) is shallower by the same factor, so A x frequency, the bound's term, never grows.
    gTerr = 0.25 * clamp(terrTurb + terrTex, 0.0, 1.0) * min(gK * 0.5, 1.0) * (MB_TERR / gTerrF);
    // 0.03 of c is 0.3 world units: a nudge on screen, as the flat glide was.
    gGlide = pitchGlide() * 0.03;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // The landscape tiles every 32 along z, so this shift is invisible and
    // exact (power of two). Without it, far down the 8192 travel a highp z
    // has ~0.001 of fraction left and every sample quantises.
    ro.z = mod(ro.z, MB_TILE);
    // The layers are measured from the eye; a spawned chain slides 4 down the flight.
    shellSetup3(ro, 3.0, 4.0, false);

    // Start the march where the ray meets the slab the landscape can occupy (a hook that is on can
    // raise a copy above it, so the march then starts at the eye).
    float t = 0.02;
    if (ro.y > MB_TOP + 1.0 && shellIdle(SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK)) {
        if (rd.y >= -1e-4) t = MB_FAR + 1.0;
        else t = max(t, (ro.y - MB_TOP - 1.0) / -rd.y);
    }
    float hitT = -1.0;
    float glow = 0.0;
    // The glow's depth, weighted like the glow, so it can be coloured by how far off it hangs.
    float glowT = 0.0;
    float used = 0.0;
    for (int i = 0; i < MB_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (t > MB_FAR) break;
        used = float(i);
        float d = map(ro + rd * t);
        float eps = 0.0012 * t + 0.0006;
        if (d < eps) {
            hitT = t;
            break;
        }
        // The filaments' rims shine through the gaps between the cliffs: light
        // gathered along the ray by step length (so Detail does not change it)
        // and thinned by the air, the way the Flame family hangs its veils in depth.
        float stepLen = max(d * 0.8, eps);
        if (gInside < 0.5 && gMat < 0.5) {
            float w = exp(-d * 6.0) * gIter * stepLen * exp(-t * 0.025);
            glow += w;
            glowT += w * t;
        }
        t += stepLen;
    }
    // Out of budget on a grazing ray over the mesa or the cliffs: the ray was
    // still closing in, so it lands where it stopped instead of showing sky
    // through the ground.
    if (hitT < 0.0 && t < MB_FAR && rd.y < 0.0) hitT = t;

    float hue = 0.03 * sin(uTime * 0.05) + hueTone + hueStep;
    vec3 sky = skyColour(rd, hue);
    vec3 col = sky;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, 0.0015 * hitT + 0.0008);
        // Restore the hit's escape picture after the normal taps.
        map(p);
        vec3 L = normalize(vec3(0.35, 0.8, 0.45));
        float dif = max(dot(n, L), 0.0);
        float fres = pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 4.0);
        float spec = pow(max(dot(reflect(rd, n), L), 0.0), 40.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 0.9, 0.3, 1.0);

        if (gMat > 0.5) {
            // The abyss floor: dark, faintly tinted by the bands above it.
            col = pal(hue + 0.55) * (0.03 + 0.05 * dif) * ao;
        } else if (gInside > 0.5) {
            // The set: black obsidian, as the flat style drew it, with the sky
            // and the glow ahead mirrored in its polish.
            vec3 refl = skyColour(reflect(rd, n), hue);
            col = pal(hue + 0.5) * 0.02 + refl * (0.15 + 0.85 * fres) + vec3(spec) * 0.25;
        } else {
            // The escape bands, coloured as before: band phase by the smooth
            // count, sub bass shifting it, each band lit by its own spectrum bin.
            float f = gIter;
            float smoothF = f + sub * 0.1;
            float flow = uTime * 0.02 * flowK;
            vec3 base = pal(smoothF * 2.0 + flow + hue) * (0.2 + f * 1.4 + sndSpectrum(f) * 0.7);
            col = base * (0.25 + 0.75 * dif) * ao + pal(hue + 0.3) * fres * 0.12 + vec3(spec) * 0.2 * ao;
        }
        // Depth haze: the air's colour runs on round the palette with distance,
        // as the Hollows' does, so near cliffs and far islands read apart.
        col = mix(col, skyColour(rd, hue + 0.3 * hitT / MB_FAR), 1.0 - exp(-hitT * fogK));
    }
    float glowFar = glow > 1e-5 ? glowT / (glow * MB_FAR) : 0.0;
    col += pal(hue + 0.15 + 0.4 * glowFar) * glow * 0.12 * glowK * (0.7 + 0.3 * swell);

    col *= 0.8 + 0.25 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
