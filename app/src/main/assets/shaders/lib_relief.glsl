// The shared "2.5D" look for the simulation scenes (Silk, Fluid, Life, Acid, Cymatics present
// passes): it turns a flat 2D field into a surface that reads as a lit, tilting, living relief.
//
// WHY THIS EXISTS
//
// A simulation's state is a picture of a field, and a field drawn flat reads as a screensaver no
// matter how good the physics is. What makes the raymarched families (Arbor Synapse first) feel
// alive is depth: parallax as the view moves, a light that rakes across the shape, materials that
// answer that light differently, and motion that follows the music. A simulation cannot be
// raymarched in 3D, but its field can be read as a HEIGHT, and a heightfield seen through a
// slightly tilted eye is exactly what parallax occlusion mapping draws: a few height reads along
// the view ray, the first one under the surface is the point seen. That costs a bounded loop of
// texture reads, not a march through a volume, so it holds at full screen on a phone.
//
// HOW A PRESENT PASS USES IT
//
//   //#include lib_sound                  (first: this file reads SND_* names)
//   float reliefHeight(vec2 uv) { return texture(uState, uv).g; }   // 0..1, defined BEFORE the include
//   //#include lib_relief
//   ...
//   Relief r = reliefMarch(vUv, reliefTilt(), 0.05);
//   vec3 albedo = myPalette(texture(uState, r.uv));                   // colour where the eye lands
//   vec3 col = reliefShade(reliefLook(RELIEF_SATIN, RELIEF_GLASS), albedo, r, reliefLight(), sparkle);
//
// The frame is the uv frame: x right, y up, z out of the screen toward the viewer. A height of 1
// is the top surface, 0 the floor `depth` uv units below it.
//
// SILENCE. Every motion here rides a sound clock (curved time, mid time), and clocks stop when the
// music does, so in silence the tilt and the light hold still at a pleasant angle instead of
// collapsing to a flat, head-on view: calm, not dead. A shader that has a wall clock can define
// RELIEF_CLOCK (seconds, e.g. `#define RELIEF_CLOCK uTime`) before the include, and the view then
// keeps a slow idle drift on top of the music.
//
// KNOBS. RELIEF_EPS (the normal's tap distance in uv, about one or two state texels) and
// RELIEF_BUMP (how strongly slopes bend the normal relative to the parallax depth) can be defined
// before the include; a coarse sim grid wants a larger EPS, a smooth field a larger BUMP.
//
// NAMES. Every helper is prefixed `rl` and every public name `relief`/`Relief`, so a present pass
// can include lib_palette (or anything else) beside this without a clash. This file includes
// nothing itself; the family libraries it borrows ideas from (lib_glass, lib_terra, lib_nebula,
// lib_arbor) are written for 3D marches and are not pasted in.

#define RELIEF_SATIN 0     // silk sheen, anisotropic
#define RELIEF_GLASS 1     // fresnel, chromatic refraction, caustic sparkle (from lib_glass)
#define RELIEF_CHROME 2    // mirror of a procedural sky
#define RELIEF_NEON 3      // emissive rims and glow on black
#define RELIEF_TERRA 4     // contour lines, lambert, AO (from lib_terra)
#define RELIEF_MEMBRANE 5  // translucent: dark face, glowing rim (Arbor Synapse)
#define RELIEF_NEBULA 6    // soft volumetric glow plus stars (from lib_nebula)
#define RELIEF_INK 7       // flat paper and ink, no light

/** Compile-time ceiling on the parallax march; the march itself takes 10 steps head on, up to this when tilted. */
#define RELIEF_MAX_STEPS 24

#ifndef RELIEF_CLOCK
#define RELIEF_CLOCK 0.0
#endif
#ifndef RELIEF_EPS
#define RELIEF_EPS 0.0025
#endif
#ifndef RELIEF_BUMP
#define RELIEF_BUMP 4.0
#endif

/**
 * Where the eye landed on the heightfield: the uv to read colour at (shifted by parallax), the
 * surface normal in the uv frame, the height there (0..1) and an ambient occlusion term (0..1,
 * low in valleys and creases), which is what gives the relief its weight.
 */
struct Relief {
    vec2 uv;
    vec3 n;
    float h;
    float ao;
};

float rlSat(float x) { return clamp(x, 0.0, 1.0); }

float rlHash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

/** Hue rotation by `turns` around the grey axis (Rodrigues): a complementary accent is +0.5. */
vec3 rlHue(vec3 c, float turns) {
    const vec3 k = vec3(0.57735);
    float a = turns * 6.2831853;
    float ca = cos(a);
    return c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - ca);
}

/**
 * The view's lean, as the uv shift per unit of depth (length ~0.3 calm, up to ~0.8 on a surge).
 *
 * A slow Lissajous on the curved clock (which barely moves in quiet passages and races in loud
 * ones), so the relief rocks with the music's energy and never loops visibly (the 1 : 1.37 ratio is
 * irrational enough). Stereo pan leans the view toward the side the sound is on. A held surge leans
 * in harder, the first half of the dolly (reliefMarch widens the perspective for the second half).
 * The base lean never goes to zero, so silence still shows a lit, tilted surface.
 */
vec2 reliefTilt() {
    float c = SND_CURVED_TIME * 0.11 + RELIEF_CLOCK * 0.05;
    vec2 lis = vec2(sin(c + 0.4), sin(c * 1.37 + 1.9));
    float lean = 0.28 + 0.18 * rlSat(SND_PRESENCE) + 0.35 * rlSat(SND_HELD_SURGE);
    vec2 t = lis * lean;
    t.x += clamp(SND_STEREO_PAN, -1.0, 1.0) * 0.18;
    return t;
}

/**
 * Parallax occlusion march: from the top surface at `uv`, step the view ray down through the
 * heightfield until it is under the surface, then interpolate between the last two steps.
 * `depth` is the relief's thickness in uv units (0.03 subtle .. 0.1 deep); 0 returns a flat
 * relief with no march.
 *
 * The view direction per pixel is the tilt plus a perspective spread from the screen centre (uv
 * 0.5), so the edges see more of the slopes than the middle, as a real lens close to a surface
 * does; a held surge widens that spread, which reads as the camera dollying in.
 */
Relief reliefMarch(vec2 uv, vec2 tilt, float depth) {
    vec2 view = tilt + (uv - 0.5) * (0.25 + 0.5 * rlSat(SND_HELD_SURGE));
    vec2 shift = view * depth;
    float steps = floor(mix(10.0, float(RELIEF_MAX_STEPS), rlSat(length(view) * 1.6)));
    vec2 dUv = shift / steps;
    float dLayer = 1.0 / steps;

    vec2 p = uv;
    float layer = 0.0;               // how far below the top the ray is, 0..1
    float surf = 1.0 - reliefHeight(p);
    vec2 pPrev = p;
    float layerPrev = 0.0;
    float surfPrev = surf;
    if (depth > 0.0) {
        for (int i = 0; i < RELIEF_MAX_STEPS; i++) {
            if (float(i) >= steps || layer >= surf) break;
            pPrev = p;
            layerPrev = layer;
            surfPrev = surf;
            p -= dUv;
            layer += dLayer;
            surf = 1.0 - reliefHeight(p);
        }
        // The crossing lies between the last step above the surface and the first below it.
        float after = surf - layer;
        float before = surfPrev - layerPrev;
        float w = after / min(after - before, -1e-5);
        p = mix(p, pPrev, rlSat(w));
    }

    Relief r;
    r.uv = p;
    vec2 e = vec2(RELIEF_EPS, 0.0);
    float hc = reliefHeight(p);
    float hl = reliefHeight(p - e.xy);
    float hr = reliefHeight(p + e.xy);
    float hd = reliefHeight(p - e.yx);
    float hu = reliefHeight(p + e.yx);
    float k = max(depth, 0.02) * RELIEF_BUMP / (2.0 * RELIEF_EPS);
    r.n = normalize(vec3((hl - hr) * k, (hd - hu) * k, 1.0));
    r.h = hc;
    // Cheap occlusion from what is already read: low ground is darker, and a crease (the centre
    // below its neighbours, a positive Laplacian) darker still.
    float crease = (hl + hr + hd + hu) * 0.25 - hc;
    r.ao = rlSat(mix(0.5, 1.0, hc) * (1.0 - crease * 6.0));
    return r;
}

/**
 * The key light, in the uv frame (toward the light). It orbits on the mid clock, about a turn
 * every 25 s of typical music and still in silence, and rides higher as the melody climbs
 * (pitch height), so a rising line literally lights the relief from above.
 */
vec3 reliefLight() {
    float a = SND_MID_TIME * 0.25 + RELIEF_CLOCK * 0.04 + 0.8;
    float z = mix(0.38, 0.78, rlSat(SND_PITCH_HEIGHT));
    float r = sqrt(1.0 - z * z);
    return vec3(cos(a) * r, sin(a) * r, z);
}

/** A procedural studio sky in direction d (uv frame, z toward the viewer), for chrome and glass reflections. */
vec3 rlSky(vec3 d, vec3 tint) {
    float up = d.y * 0.5 + 0.5;
    vec3 sky = mix(vec3(0.02, 0.025, 0.04), mix(tint, vec3(0.9), 0.5) * 0.9, smoothstep(0.1, 1.0, up));
    // Two soft light panels: the long bright streaks a mirror needs to read as a mirror.
    float panelA = smoothstep(0.82, 0.97, dot(d, normalize(vec3(0.5, 0.7, 0.5))));
    float panelB = smoothstep(0.9, 0.99, dot(d, normalize(vec3(-0.7, 0.2, 0.7))));
    // A horizon band, so curved chrome shows a dark line bending through it.
    float band = 1.0 - smoothstep(0.0, 0.12, abs(d.y + 0.05));
    return sky * (1.0 - 0.7 * band) + vec3(1.0, 0.97, 0.92) * (panelA * 1.6 + panelB * 0.9);
}

/** Glitter: a sparse set of fixed points on the surface that flash where the light catches them. */
float rlGlint(vec2 uv, vec3 n, vec3 l) {
    vec2 cell = floor(uv * 180.0);
    float g = step(0.965, rlHash(cell));
    float tw = 0.5 + 0.5 * sin(SND_TREBLE_TIME * 6.0 + rlHash(cell + 7.1) * 6.2831853);
    float face = pow(max(dot(reflect(-l, n), vec3(0.0, 0.0, 1.0)), 0.0), 8.0);
    return g * tw * face;
}

/**
 * Shades the relief in one of the eight looks. `albedo` is the field's own colour at r.uv,
 * `light` is reliefLight() (or any direction toward a light), `sparkle` 0..1 adds glitter (pass
 * treble or an onset envelope). The eye is head on (0, 0, 1): the tilt is small enough that the
 * error does not show, and it keeps the shading stable while the view rocks. Every specular term
 * flashes on the kick envelope, so the light seems to pulse with the drum without the colours
 * moving.
 */
vec3 reliefShade(int look, vec3 albedo, Relief r, vec3 light, float sparkle) {
    vec3 n = r.n;
    vec3 v = vec3(0.0, 0.0, 1.0);
    vec3 l = normalize(light);
    vec3 hv = normalize(l + v);
    float ndl = dot(n, l);
    float ndv = rlSat(dot(n, v));
    float kick = 1.0 + 1.6 * rlSat(SND_KICK_ENV);
    float glint = rlGlint(r.uv, n, l) * rlSat(sparkle) * kick;
    vec3 col;

    if (look == RELIEF_SATIN) {
        // Silk: wrapped diffuse and an anisotropic sheen along the contours (Kajiya-Kay), so
        // highlights run as long streaks across the folds rather than sitting as dots.
        float diff = rlSat((ndl + 0.4) / 1.4);
        vec3 grad = vec3(n.xy, 0.0);
        vec3 t = length(grad) > 1e-4 ? normalize(vec3(-n.y, n.x, 0.0)) : vec3(1.0, 0.0, 0.0);
        float th = dot(t, hv);
        float sheen = pow(sqrt(max(1.0 - th * th, 0.0)), 48.0);
        col = albedo * (0.18 + 0.82 * diff) * r.ao + mix(albedo, vec3(1.0), 0.6) * sheen * 0.4 * kick;
        col += vec3(glint) * 0.6;
    } else if (look == RELIEF_GLASS) {
        // Glass: Schlick Fresnel against a studio sky; the body shows the field through it with a
        // channel-split shift along the slope (dispersion), and the slopes' convergence (the ao
        // crease term inverted) gathers caustic light.
        float f = 0.04 + 0.96 * pow(1.0 - ndv, 5.0);
        vec3 refl = rlSky(reflect(-v, n), albedo);
        float slope = length(n.xy);
        vec3 disp = vec3(rlHue(albedo, 0.06 * slope).r, albedo.g, rlHue(albedo, -0.06 * slope).b);
        vec3 body = disp * (0.35 + 0.65 * r.h) * mix(0.6, 1.0, r.ao);
        float caustic = pow(rlSat(1.2 - r.ao), 2.0) * 0.6 + pow(max(dot(n, hv), 0.0), 24.0) * 0.4;
        float spec = pow(max(dot(n, hv), 0.0), 120.0) * kick;
        col = mix(body + albedo * caustic, refl, f) + vec3(spec) * 1.4 + vec3(glint);
    } else if (look == RELIEF_CHROME) {
        // Chrome: nothing but the reflected sky, tinted by the field; the relief reads entirely
        // through how the panels and horizon band bend across it.
        vec3 refl = rlSky(reflect(-v, n), albedo);
        float spec = pow(max(dot(n, hv), 0.0), 200.0) * kick;
        col = refl * mix(vec3(1.0), albedo * 1.6 + 0.1, 0.45) * mix(0.7, 1.0, r.ao) + vec3(spec) * 2.0;
        col += vec3(glint) * 1.2;
    } else if (look == RELIEF_NEON) {
        // Neon: black ground, the slopes glow (the rim term) and the ridges burn; the light only
        // adds a moving hot edge on the side it faces.
        float rim = pow(1.0 - ndv, 1.4);
        float ridge = smoothstep(0.55, 0.95, r.h);
        float edge = rlSat(ndl) * rim;
        col = albedo * (rim * 2.2 + ridge * 0.7 + 0.03) + rlHue(albedo, 0.5) * edge * 0.6 * kick;
        col += albedo * glint;
    } else if (look == RELIEF_TERRA) {
        // Terrain: Lambert plus sky fill, occlusion, hypsometric shading (high ground lighter) and
        // antialiased contour lines every 1/14 of the height, the map look of lib_terra.
        float diff = rlSat(ndl);
        vec3 ground = albedo * mix(0.55, 1.15, r.h);
        col = ground * (0.22 + 0.78 * diff) * r.ao + ground * 0.12 * n.z;
        float ch = r.h * 14.0;
        float w = max(fwidth(ch), 1e-4);
        float line = 1.0 - smoothstep(0.0, w * 1.2, min(fract(ch), 1.0 - fract(ch)));
        col *= 1.0 - 0.45 * line;
        col += vec3(pow(max(dot(n, hv), 0.0), 40.0) * 0.25 * kick) + vec3(glint) * 0.4;
    } else if (look == RELIEF_MEMBRANE) {
        // Membrane (Arbor Synapse): a dark translucent face, a rim that glows in the field's hue,
        // and a solid core where the field is high, a second material with its own shading. A
        // hot complementary spark rides the specular.
        float diff = rlSat((ndl + 0.6) / 1.6);
        float rim = pow(1.0 - ndv, 3.0);
        float spec = pow(max(dot(n, hv), 0.0), 32.0);
        float core = smoothstep(0.7, 0.95, r.h);
        vec3 face = albedo * 0.12 * diff;
        vec3 glow = mix(albedo, vec3(1.0), 0.25) * rim * 1.8;
        vec3 soma = albedo * (0.5 + 0.7 * diff) * core;
        col = face + glow + soma + rlHue(albedo, 0.45) * spec * 0.5 * kick;
        col += rlHue(albedo, 0.45) * glint;
    } else if (look == RELIEF_NEBULA) {
        // Nebula: the field as glowing gas, brightest and whitest where it is densest, soft light
        // with no hard specular, over a sparse twinkling star field that shows through the thin
        // parts.
        float dens = r.h * r.h;
        vec3 gas = albedo * (0.12 + 1.3 * dens) * (0.75 + 0.25 * rlSat(ndl));
        gas += mix(albedo, vec3(1.0), 0.5) * pow(r.h, 6.0) * 0.6 * kick;
        vec2 cell = floor(r.uv * 220.0);
        float star = step(0.993, rlHash(cell));
        float tw = 0.6 + 0.4 * sin(SND_TREBLE_TIME * 3.0 + RELIEF_CLOCK * 1.5 + rlHash(cell + 3.3) * 6.2831853);
        col = gas + vec3(star * tw * (1.0 - dens)) * 0.8 + albedo * glint;
    } else {
        // Ink: paper and ink, no light at all; the relief shows as isolines and a slope hatch.
        vec3 paper = vec3(0.93, 0.9, 0.84);
        vec3 ink = albedo * 0.35;
        float ch = r.h * 10.0;
        float w = max(fwidth(ch), 1e-4);
        float line = 1.0 - smoothstep(0.0, w * 1.5, min(fract(ch), 1.0 - fract(ch)));
        float slope = length(r.n.xy);
        float hatch = step(0.5, fract((r.uv.x + r.uv.y) * 140.0)) * smoothstep(0.35, 0.8, slope);
        float fill = smoothstep(0.75, 0.9, r.h);
        col = mix(paper, ink, rlSat(line * 0.9 + hatch * 0.35 + fill * 0.8));
    }
    return col;
}

/**
 * Alternates between two looks on each hard cut (the engine's "big moment"): the material snaps
 * when the music does, so a long track does not sit in one look. The cut count wraps at 4096, an
 * even number, so the alternation never skips across the wrap.
 */
int reliefLook(int base, int alt) {
    return mod(SND_CUT_COUNT, 2.0) < 0.5 ? base : alt;
}
