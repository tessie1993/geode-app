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

// Spectrum Terrain: low flight down a valley whose flanks are ridges built
// from the spectrum. Distance from the valley floor along x picks a band (sub
// at the floor, air out on the far ridges), each band raises its own crest,
// and a slow noise along z stands in for older spectra, so the land ahead
// reads as history rolling toward the eye. Glowing contour lines trace the
// height; a sun or moon sits on the horizon ahead; fog closes the distance.
// Flown with camFly; the eye is clamped above the local ground.
//
// Every z term is a value noise whose lattice wraps along z with a period
// that divides 1024 cells of its own scale, so the eye is rebased into one
// 1024 tile (exact, power of two) and the rig's 8192 wrap is invisible.
//
// ---- audio ------------------------------------------------------------------
//
// Each sound moves a different property:
//   bands (uBandsLo.xyzw, uBandsHi.xyz)  ridge heights across x
//   kick (uHits.x)        a shock ring that rises out of the land ahead and
//                         spreads as the kick decays (geometry, the eye rides it)
//   snare (uHits.y)       the contour lines flip toward the complementary hue
//   hat (uHits.z)         sparkles on the ridge tops only
//   flux (uBandsHi.w)     thicker contour lines
//   drop (uEvents.y)      swings the sky half a day round (dusk <-> dawn,
//                         night <-> day) and back over the drop's envelope
//   section (uSectionCount, uEvents.z) steps the time of day, cross-faded
//   pitch change (uEvents.w) with the key (uKeyHue)  rotates the land's hue
//   air (uBandsHi.z)      star twinkle; bass (uBassSmooth) valley mist
// Overall level follows uSwell.

#define ST_MAX_STEPS 112
#define ST_FAR 70.0
#define ST_BASE_Y -3.0
/** Above every possible ridge: the march gives up on a rising ray past this. */
#define ST_TOP 4.6
#define ST_ZWRAP 1024.0
#define ST_TAU 6.2831853
/** Valley half-width over which the bands spread, world units. */
#define ST_SPAN 18.0

vec4 gLo;
vec3 gHi;
float gSpecAmp;
vec2 gRipC;
float gRipR;
float gRipA;

/** Value noise whose lattice wraps along y every `period` cells (a whole number). */
float tnoise(vec2 p, float period, float seed) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float z0 = mod(i.y, period);
    float z1 = mod(i.y + 1.0, period);
    float a = motionHash(vec2(i.x, z0) + seed);
    float b = motionHash(vec2(i.x + 1.0, z0) + seed);
    float c = motionHash(vec2(i.x, z1) + seed);
    float d = motionHash(vec2(i.x + 1.0, z1) + seed);
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

/** The seven band regions as one piecewise-linear curve over u in 0..1. */
float bandAt(float u) {
    float s = clamp(u, 0.0, 1.0) * 6.0;
    vec4 wl = max(1.0 - abs(s - vec4(0.0, 1.0, 2.0, 3.0)), 0.0);
    vec3 wh = max(1.0 - abs(s - vec3(4.0, 5.0, 6.0)), 0.0);
    return dot(wl, gLo) + dot(wh, gHi);
}

/** Ground height above ST_BASE_Y. */
float terrain(vec2 xz) {
    float ax = abs(xz.x);
    float valley = smoothstep(1.2, 9.0, ax);
    // Base hills: three octaves, each period 1024 world units along z.
    float n = 0.55 * tnoise(xz * 0.125, 128.0, 0.0)
            + 0.30 * tnoise(xz * 0.25 + vec2(17.0, 0.0), 256.0, 3.0)
            + 0.15 * tnoise(xz * 0.5 + vec2(-9.0, 0.0), 512.0, 5.0);
    float h = n * (0.5 + 2.0 * valley);
    // Spectrum ridges: one crest per band across x, scaled along z by the
    // "history" noise so the land ahead is not a copy of now.
    float u = clamp((ax - 1.0) / ST_SPAN, 0.0, 1.0);
    float crest = 0.7 + 0.3 * cos(ST_TAU * u * 6.0);
    float hist = tnoise(vec2(xz.x * 0.5, xz.y * 0.25), 256.0, 9.0);
    h += (0.1 + bandAt(u)) * crest * (0.35 + 0.65 * hist) * (0.3 + 2.4 * valley) * gSpecAmp;
    // The kick's shock ring.
    float x = (length(xz - gRipC) - gRipR) * 1.4;
    h += gRipA * exp(-x * x);
    return h;
}

void main() {
    vec2 uv = view();

    float swell = clamp(uSwell, 0.0, 1.5);
    float bass = clamp(uBassSmooth, 0.0, 1.5);
    float kick = clamp(uHits.x, 0.0, 1.0);
    float snare = clamp(uHits.y, 0.0, 1.0);
    float hats = clamp(uHits.z, 0.0, 1.0);
    float drop = clamp(uEvents.y, 0.0, 1.0);
    float secEnv = clamp(uEvents.z, 0.0, 1.0);
    float pitch = clamp(uEvents.w, 0.0, 1.0);
    float air = clamp(uBandsHi.z, 0.0, 1.0);
    float flux = clamp(uBandsHi.w, 0.0, 1.0);
    gLo = clamp(uBandsLo, 0.0, 1.0);
    gHi = clamp(uBandsHi.xyz, 0.0, 1.0);
    gSpecAmp = 0.75 + 0.35 * min(swell, 1.0);

    // ---- time of day ----
    // 0 dusk, 1 night, 2 dawn, 3 day, on a circle of 4. A section steps it by
    // one; subtracting the section envelope (1 at the change, falling to 0)
    // starts the step at the previous section's value, so it glides rather
    // than cuts. A drop swings it half a day further and back.
    float tod = uSectionCount - secEnv + 2.0 * 4.0 * drop * (1.0 - drop);
    float night = 0.5 + 0.5 * cos((tod - 1.0) * (ST_TAU / 4.0));
    float day = 0.5 + 0.5 * cos((tod - 3.0) * (ST_TAU / 4.0));
    float skyHue = tod * 0.25;
    vec3 horizonCol = pal(skyHue) * mix(0.30, 0.05, night) + pal(skyHue + 0.5) * 0.04 * day;
    vec3 zenithCol = pal(skyHue + 0.6) * mix(0.06, 0.012, night) + vec3(0.002, 0.003, 0.008);
    vec3 sunDir = normalize(vec3(0.18, mix(0.05, 0.14, day) + 0.04 * night, 1.0));
    vec3 sunCol = mix(pal(skyHue + 0.05) * 1.3 + 0.25, vec3(0.75, 0.82, 1.0), night);
    float sunR = mix(0.055, 0.035, night);

    // Land hue: the key (eased, gated by its confidence) plus a pitch nudge.
    float landHue = 0.3 * uKeyHue * clamp(uKeyStrength, 0.0, 1.0) + 0.18 * pitch;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, ST_ZWRAP);
    // The ring rises a few units ahead and spreads as the kick decays.
    gRipC = ro.xz + vec2(0.0, 4.0);
    gRipR = 1.5 + 18.0 * (1.0 - kick);
    gRipA = 0.8 * kick;
    ro.y = max(ro.y, ST_BASE_Y + terrain(ro.xz) + 0.6);

    float t = 0.05;
    float tPrev = t;
    float hPrev = 1.0;
    float hitT = -1.0;
    for (int i = 0; i < ST_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * t;
        if (p.y > ST_BASE_Y + ST_TOP && rd.y > 0.0) break;
        float h = p.y - (ST_BASE_Y + terrain(p.xz));
        if (h < 0.002 * t) {
            hitT = mix(tPrev, t, clamp(hPrev / max(hPrev - h, 1e-4), 0.0, 1.0));
            break;
        }
        tPrev = t;
        hPrev = h;
        t += max(h * 0.45, 0.02 + 0.006 * t);
        if (t > ST_FAR) break;
    }

    vec3 col;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        float e = 0.02 + 0.002 * hitT;
        float h0 = terrain(p.xz);
        vec3 n = normalize(vec3(h0 - terrain(p.xz + vec2(e, 0.0)), e, h0 - terrain(p.xz + vec2(0.0, e))));
        float ax = abs(p.x);
        float u = clamp((ax - 1.0) / ST_SPAN, 0.0, 1.0);
        float valley = smoothstep(1.2, 9.0, ax);
        float fade = exp(-hitT * 0.035);

        float dif = max(dot(n, sunDir), 0.0);
        col = pal(landHue + 0.6) * (0.025 + 0.09 * dif * (1.0 - 0.7 * night)) + horizonCol * 0.15 * (1.0 - n.y);

        // Contours every 1/2.2 of height. Width in height units grows with
        // distance so far lines stay a pixel or so; flux thickens them.
        float dc = abs(fract(h0 * 2.2 + 0.5) - 0.5) / 2.2;
        float w = (0.012 + 0.0022 * hitT) * (0.7 + 0.8 * flux);
        float line = smoothstep(w, w * 0.3, dc) * fade;
        float ch = landHue + h0 * 0.06 + 0.1 * u;
        vec3 lineCol = mix(pal(ch), pal(ch + 0.5), snare) + 0.25 * snare;
        col += lineCol * line * (0.35 + 0.9 * bandAt(u) * valley + 0.15 * swell);

        // The shock ring glows faintly on its crest.
        float rx = (length(p.xz - gRipC) - gRipR) * 1.4;
        col += pal(ch + 0.25) * gRipA * exp(-rx * rx) * 0.35 * fade;

        // Hat sparkles: hashed glints on the ridge tops, z wrapped like the
        // land (1024 * 3 is whole) so they do not re-hash at the rebase.
        vec2 g = vec2(p.x, mod(p.z, ST_ZWRAP)) * 3.0;
        float sh = motionHash(floor(g) + 41.0);
        float glint = step(0.88, sh) * smoothstep(0.32, 0.0, length(fract(g) - 0.5));
        float top = smoothstep(2.4, 4.0, h0);
        col += mix(pal(ch + 0.15), vec3(1.0), 0.5) * glint * top * hats * 1.6 * fade;

        // Fog, plus a bass mist pooled on the valley floor.
        col = mix(col, horizonCol, 1.0 - exp(-hitT * (0.028 + 0.02 * night)));
        float mist = exp(-(p.y - ST_BASE_Y) * 1.3) * bass * 0.5 * (1.0 - exp(-hitT * 0.12));
        col = mix(col, horizonCol * 1.4, clamp(mist, 0.0, 0.8));
    } else {
        float up = clamp(rd.y, -1.0, 1.0);
        col = mix(horizonCol, zenithCol, smoothstep(-0.02, 0.5, up));
        // Sun or moon: a2 is the squared angle to its centre.
        float a2 = 2.0 * (1.0 - dot(rd, sunDir));
        float disc = smoothstep(sunR * sunR, sunR * sunR * 0.75, a2);
        col += sunCol * disc + sunCol * (0.12 + 0.1 * day) * exp(-a2 * 60.0);
        // Stars at night; air makes them twinkle.
        vec3 sc = rd * 150.0;
        float s = hashCell(ivec3(floor(sc)));
        float star = step(0.993, s) * smoothstep(0.35, 0.0, length(fract(sc) - 0.5));
        float tw = mix(1.0, 0.5 + 0.5 * sin(uTime * 11.0 + s * 500.0), 0.2 + 0.8 * air);
        col += vec3(0.7) * star * tw * night * smoothstep(0.03, 0.25, up);
    }

    col *= 0.8 + 0.25 * swell;
    col *= 0.75 + 0.25 * smoothstep(2.2, 0.4, length(uv));
    // Exposure shoulder before grade(): the scene is mostly dark water or sky, and
    // grade()'s contrast pivots on 0.5, so lift the darks ~1.9x and roll the
    // highlights off rather than clip them.
    col = vec3(1.0) - exp(-max(col, 0.0) * 1.9);
    fragColor = vec4(grade(col), 1.0);
}
