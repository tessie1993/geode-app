// The Opal family: clear bodies with an oil-film skin, soap bubbles, shells and
// melting drops, hung in the dark; the camera rig flies through them or turns
// about them.
//
// WHAT IS DRAWN. Each style gives one signed distance, opalMap(p), and a skin
// coordinate beside it. The ray is sphere-traced to the nearest skin, shaded,
// and carried on through: up to OPAL_HITS skins along one ray, each dimmer
// than the last, so a body shows its far wall and the bodies behind it. A skin
// is a thin film: its colour runs through the palette with the angle it is
// seen at, so every body carries bands that crowd toward its rim, a bright rim
// line, one soft highlight, and (where a style asks) fine lines drawn along
// the skin coordinate. The steps spent near a body leave a faint halo.
//
// COST. One loop of min(uSteps, 80) steps, each one opalMap; a hit adds four
// more for its normal, so at most 80 + 4 * OPAL_HITS = 96 opalMap per pixel.
// The maps are smooth unions of a few spheres, tori or one lattice term; no
// noise octaves and no texture. The palette is read five times per pixel
// before the loop (opalSetup) and mixed after.
//
// THE CONTROLS, read normalized through lib_neon:
//
//   Morph       gOpalForm    the bodies' own metamorphosis, named by each style
//   Warp        gOpalFlow    opalBend(): space twists about the way ahead and
//                            swells sideways, so every body is pulled out of true
//   Turbulence  gOpalChaos   a ripple over every skin
//   Intensity   neonGlow()   the emission
//   k0 PULSE the emission, k1 TEXTURE the skin lines, k2 FLOW the bend,
//   k3 TONE the palette position, k4 BUILD the film's bands, k5 SPACE the reach.
//
// No style here reads the sound: what moves on a hit is the reaction matrix,
// through those params. uTime stops in silence, and so does everything here.
//
// The world repeats along z every OPAL_PERIOD (16, dividing the rig's 8192),
// and every clock phase is whole cycles per uTime wrap (OPAL_RATE).
//
// INCLUDE ORDER: last, after lib_scene_uniforms (with VIEW_OWNS_SHAPE),
// lib_scene_motion, lib_camera, lib_palette, lib_scene_grade and lib_neon.
// The style defines opalMap after it.

#define OPAL_TAU 6.2831853
#define OPAL_MAX_STEPS 80
#define OPAL_HITS 4
#define OPAL_PERIOD 16.0
/** k whole cycles per uTime wrap (7100 s, ShaderScene). */
#define OPAL_RATE(k) (OPAL_TAU * float(k) / 7100.0)

float gOpalForm;
float gOpalFlow;
float gOpalChaos;
/** How far the trace reaches, world units. */
float gOpalFar;
/** How many times the film's colour runs through the palette from a skin's middle to its rim. */
float gOpalBands;
/** How strongly the skin coordinate's lines are drawn, 0 for none. */
float gOpalLines;
float gOpalTone;
/** The share of a step taken: under 1 where a style's distance is only a bound. */
float gOpalStride;
vec3 gOpalPal[5];

/** The style's bodies at p: x the signed distance to the nearest skin, y the skin coordinate (a line at every whole number). */
vec2 opalMap(vec3 p);

mat2 opalRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

float opalSmin(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * k * 0.25;
}

float opalHash(vec2 c) {
    return fract(sin(dot(c, vec2(127.1, 311.7))) * 43758.5453);
}

/** The user's palette at t, from the five reads of opalSetup(): there and back, so no seam. */
vec3 opalCol(float t) {
    float x = 4.0 * abs(2.0 * fract(0.5 * t) - 1.0);
    int i = int(min(floor(x), 3.0));
    return mix(gOpalPal[i], gOpalPal[i + 1], smoothstep(0.0, 1.0, x - float(i)));
}

void opalSetup() {
    gOpalForm = neonForm();
    gOpalFlow = clamp(neonFlow() + 0.5 * neonKnob(2), 0.0, 1.0);
    gOpalChaos = neonChaos();
    gOpalFar = 11.0 * (1.0 + 0.4 * neonKnob(5));
    gOpalBands = 1.6 * (1.0 + 0.8 * neonKnob(4));
    gOpalLines = 0.6 * (1.0 + neonKnob(1));
    gOpalTone = 0.6 * neonKnob(3);
    gOpalStride = 0.85;
    for (int i = 0; i < 5; i++) gOpalPal[i] = pal(0.25 * float(i));
}

/** Warp on space: a twist about z that comes and goes along the way, and a sideways swell. */
vec3 opalBend(vec3 p) {
    float zc = p.z * (OPAL_TAU / OPAL_PERIOD);
    p.xy = opalRot(gOpalFlow * 0.9 * sin(zc + 0.6 * p.y + uTime * OPAL_RATE(150))) * p.xy;
    p.x += gOpalFlow * 0.5 * sin(2.0 * zc + 1.1 * p.y + uTime * OPAL_RATE(210));
    p.y += gOpalFlow * 0.4 * sin(zc + 1.3 * p.x);
    return p;
}

vec2 opalField(vec3 p) {
    vec3 q = opalBend(p);
    vec2 m = opalMap(q);
    vec3 s = sin(q * 5.0 + uTime * OPAL_RATE(1700) + vec3(0.0, 2.1, 4.2));
    m.x += gOpalChaos * 0.035 * (s.x * s.y + s.z);
    return m;
}

float opalDither() {
    vec2 f = gl_FragCoord.xy;
    return fract(52.9829189 * fract(dot(f, vec2(0.06711056, 0.00583715))));
}

/** One skin, seen along rd at p. */
vec3 opalSkin(vec3 p, vec3 rd, float skin, float eps) {
    vec2 e = vec2(1.0, -1.0) * eps;
    vec3 n = normalize(
        e.xyy * opalField(p + e.xyy).x + e.yyx * opalField(p + e.yyx).x +
        e.yxy * opalField(p + e.yxy).x + e.xxx * opalField(p + e.xxx).x
    );
    float facing = dot(n, rd);
    n *= -sign(facing);
    float ndv = abs(facing);
    float rim = 1.0 - ndv;
    vec3 r = reflect(rd, n);
    float film = gOpalTone + gOpalBands * rim * rim + 0.3 * r.y + 0.15 * skin;
    vec3 col = opalCol(film) * (0.1 + 1.3 * rim * rim);
    col += opalCol(film + 0.45) * 1.6 * pow(rim, 6.0);
    float spec = pow(max(dot(r, normalize(vec3(0.45, 0.8, -0.35))), 0.0), 60.0);
    col += mix(vec3(1.0), opalCol(film + 0.2), 0.35) * spec;
    float c = fract(skin) - 0.5;
    col += opalCol(film + 0.3) * gOpalLines * 0.0025 / (c * c + 0.0025) * (0.25 + 0.75 * rim);
    return col;
}

/** The gathered light along one ray. */
vec3 opalTrace(vec3 ro, vec3 rd) {
    float n = min(uSteps, float(OPAL_MAX_STEPS));
    vec3 acc = vec3(0.0);
    float through = 1.0;
    float halo = 0.0;
    float t = 0.05 + 0.1 * opalDither();
    int hits = 0;
    for (int i = 0; i < OPAL_MAX_STEPS; i++) {
        if (float(i) >= n || t > gOpalFar) break;
        vec3 p = ro + rd * t;
        vec2 m = opalField(p);
        float d = abs(m.x);
        float eps = 0.0025 * (1.0 + t);
        float air = exp(-2.2 * t / gOpalFar);
        halo += air * 0.012 / (1.0 + 60.0 * d * d);
        if (d < eps) {
            // Nothing lights right at the eye: flying through a skin must not fill the frame.
            acc += opalSkin(p, rd, m.y, eps) * through * air * smoothstep(0.1, 0.8, t);
            through *= 0.6;
            hits++;
            if (hits >= OPAL_HITS) break;
            t += 6.0 * eps;
        } else {
            t += d * gOpalStride;
        }
    }
    return acc + opalCol(gOpalTone + 0.5) * halo;
}

/** `exposure` is the style's own; Intensity and PULSE scale the emission. */
vec4 opalFinish(vec3 col, float exposure) {
    col *= exposure * neonGlow() * (1.0 + 0.5 * neonKnob(0));
    col = max(mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, 1.2), 0.0);
    return vec4(grade(neonAces(col)), 1.0);
}
