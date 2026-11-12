// A simulation presented in 3D: its living field (written each frame by field_prep_frag into uField)
// is the relief of a surface on the camera rig, and finer copies of the field itself are its fractal
// detail. The first half: the inputs, the setup and the surface's coordinates. lib_field3d_march, the
// second half, marches and shades it after the present defines its colours.
//
// WHAT IS DRAWN. One of three surfaces (core/viz/present/FieldGeometry, uFieldGeom.x): a TERRAIN the
// rig flies over (camFly, the eye held clear of the highest relief), or an object the rig circles
// (camAround, seen from above): a TORUS, or a SPHERE whose relief calms toward the poles. The field's
// long side runs the long way round; every state here wraps at its edges, so the terrain tiles it and
// the objects close it with no seam. The height at a point is the field plus up to two copies of it at
// twice and four times the frequency (whole multiples, so a closed surface still closes), each turned
// a half turn, pushed by what the coarser copy drew and folded toward a ridge by Morph. A material per
// matter (young and old, by the field's age) lights it in the palette's colours; births glow.
//
// COST. One loop of FIELD_STEPS steps (87 at the default Detail, 110 at most), each up to three
// texture reads of a mipmapped field at the state's own size; a hit adds four distances for its
// normal and one for occlusion. Rays that never reach the relief (the sky over the terrain, the void
// beside an object) leave before the loop.
//
// THE CONTROLS, read the way the newest families read them (lib_neon):
//
//   Morph       gFieldForm    folds every finer copy toward a ridge, and lets it push the next
//   Warp        gFieldFlow    bends where the field is read along the surface (and k2)
//   Turbulence  gFieldChaos   how loud the finer copies are
//   Detail      uSteps        the step budget, and with it how many copies (one on a hot phone)
//   Intensity   neonGlow()    the emission
//   Kaleidoscope, Twist, Tile and Ripple bend the SHAPE, not the finished picture: the terrain is
//   folded into wedges and twisted about the eye's place on the ground, an object about its own axis,
//   Tile repeats the field over the surface (a whole number of times round an object, so it closes)
//   and Ripple runs rings through the relief. view() keeps Zoom, Rotation, Drift, Sway, Mirror and
//   Pixelate (VIEW_BENDS_IN_3D), and the composite applies none of these to a scene that draws the rig
//   itself (grade::gateFor).
//   k0 PULSE the emission, k1 TEXTURE the contour lines' width, k2 FLOW the bend, k3 TONE the
//   palette position, k4 BUILD the glow of new matter, k5 SPACE the reach.
//
// THE SOUND, on the motion only, never on a colour, a brightness or a beat (lib_opal's rule):
//
//   gFieldSwell  the low band, sndShaped(SND_LOW), lifts the relief while the lows hold
//
// No sound band turns an object or winds the terrain's twist (owner 2026-11-12, no style spins to
// the music): Twist alone does.
//
// It is times SND_DRIVE (how MUCH follows the music's absolute magnitude). What moves on a hit is
// the reaction matrix, through the params above. uTime stops in silence and SND_DRIVE falls to 0, so
// the present comes to rest with the field.
//
// Every clock phase is whole cycles per uTime wrap (FIELD_RATE), and the terrain's tile divides the
// rig's travel wrap (FieldGeometry).
//
// INCLUDE ORDER: after lib_scene_uniforms (with VIEW_OWNS_SHAPE and VIEW_BENDS_IN_3D),
// lib_scene_motion, lib_camera, lib_palette, lib_scene_grade and lib_neon. The present then defines
// vec3 fieldColour(vec4 f) and vec3 fieldGlow(vec4 f) (f: the field where the eye lands, r the height
// with its fractal detail, g the tint, b the age, a young matter) and includes lib_field3d_march.

#define FIELD_TAU 6.2831853
#define FIELD_PI 3.14159265
#define FIELD_TERRAIN 0
#define FIELD_TORUS 1
#define FIELD_SPHERE 2
#define FIELD_MAX_STEPS 110
/** uSteps (64..128 by Detail, 102 at its default, less on a hot phone) on this loop. */
#define FIELD_STEPS (min(uSteps * 0.85, float(FIELD_MAX_STEPS)))
/** k whole cycles per uTime wrap (7100 s, ShaderScene). */
#define FIELD_RATE(k) (FIELD_TAU * float(k) / 7100.0)
// The eye, mirrored by core/viz/present/FieldGeometry (kTerrainTip, kTerrainBob, kTerrainWander,
// kObjectRise, kObjectSpin) so a finger lands where the eye looks.
#define FIELD_TIP 0.3
#define FIELD_BOB 0.35
#define FIELD_WANDER 1.2
#define FIELD_RISE 0.5
#define FIELD_SPIN (2.0 * CAM_SPIN_UNIT)

/** The field: r height 0..1, g tint, b age, a young matter; x the state's long side; wrapped and mipmapped. */
uniform sampler2D uField;
/** The field's size in texels: long, short. */
uniform vec2 uFieldRes;
/** The surface (FIELD_*), the eye (terrain height, or an object's distance), the ring or sphere radius, the tube. */
uniform vec4 uFieldGeom;
/** World length of one field period along s and t, the relief of a full field, the most it ever rises (top). */
uniform vec4 uFieldShape;
/** Young matter's material (a, b), old matter's (a, b), then the style's palette offset and span (core/viz/present/FieldPresent). */
uniform vec4 uFieldLook[5];

int gFieldSurface;
float gFieldForm;
float gFieldFlow;
float gFieldChaos;
/** How many copies of the field make its height, 1..3 (fractional: the last one fades in). */
float gFieldOctaves;
float gFieldGain;
/** The low band's lift (see the header). */
float gFieldSwell;
float gFieldRelief;
float gFieldRipple;
float gFieldFolds;
float gFieldTwist;
vec2 gFieldRepeat;
float gFieldFar;
float gFieldLines;
float gFieldTone;
float gFieldGlow;
/** The share of a step taken: the distance is a bound once the field bends the surface. */
float gFieldStride;
/** Field texels under one pixel per world unit along the ray; and one texel's world size. */
float gFieldLod;
float gFieldTexel;
/** The eye, for the terrain's bends about it. */
vec3 gFieldEye;

mat2 fieldRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

/** The angle of v about its origin; 0 at the origin, where atan has none. */
float fieldAngle(vec2 v) {
    return abs(v.x) + abs(v.y) > 1.0e-12 ? atan(v.y, v.x) : 0.0;
}

void fieldSetup() {
    gFieldSurface = int(uFieldGeom.x + 0.5);
    bool terrain = gFieldSurface == FIELD_TERRAIN;
    gFieldForm = neonForm();
    gFieldFlow = clamp(neonFlow() + 0.5 * neonKnob(2), 0.0, 1.0);
    gFieldChaos = neonChaos();
    // Detail and the hot-phone cut reach the copies through the step budget: one at its floor, all three from 96 steps.
    gFieldOctaves = 1.0 + 2.0 * clamp((uSteps - 32.0) / 64.0, 0.0, 1.0);
    gFieldGain = 0.3 + 0.4 * gFieldChaos;
    // The sound (see the header): the absolute drive sets how much, the band only the shape.
    float drive = clamp(SND_DRIVE, 0.0, 1.0);
    gFieldSwell = sndShaped(SND_LOW) * drive;
    // Up to 1.45 of the relief, and the ripple a quarter more: within the ceiling FieldGeometry keeps the eye above.
    gFieldRelief = uFieldShape.z * (1.0 + 0.3 * gFieldSwell);
    gFieldRipple = 0.25 * uFieldShape.z * clamp(uRipple, 0.0, 1.0);
    gFieldFolds = (uKaleido > 0.5 && uSymmetry >= 2.0) ? uSymmetry : 0.0;
    gFieldTwist = clamp(uTwist, -1.0, 1.0) * (terrain ? 0.12 : 1.2);
    float tile = clamp(uTile, 1.0, 6.0);
    gFieldRepeat = terrain ? vec2(tile) : vec2(floor(tile + 0.5));
    // An object is reached whatever SPACE says; on the terrain SPACE is how far the land runs.
    float bound = uFieldGeom.z + uFieldGeom.w + uFieldShape.w;
    gFieldFar = terrain ? 30.0 * (1.0 + 0.4 * neonKnob(5)) : uFieldGeom.y + 2.0 * bound;
    gFieldLines = 0.05 * (1.0 + 1.2 * neonKnob(1));
    gFieldTone = 0.6 * neonKnob(3) + uFieldLook[4].x;
    gFieldGlow = 0.8 * (1.0 + neonKnob(4));
    float bend = 1.0 + 1.6 * gFieldFlow + 0.6 * gFieldChaos + (terrain ? 4.0 : 0.6) * abs(gFieldTwist) + 0.5 * (gFieldRepeat.x - 1.0);
    gFieldStride = (terrain ? 0.45 : 0.7) / bend;
    gFieldTexel = uFieldShape.y / max(uFieldRes.y, 1.0);
    // view() spans 2 over the screen's height; a pixel's spread per unit of distance, in field texels.
    float pixel = 2.0 / (max(uCamFocal, 0.1) * max(uResolution.y, 1.0));
    gFieldLod = pixel / max(gFieldTexel, 1.0e-6) * max(gFieldRepeat.x, gFieldRepeat.y);
}

/** The object seen from FIELD_RISE above the rig's horizon: the whole view turned about the world's x axis. */
vec3 fieldRise(vec3 v) {
    float c = cos(FIELD_RISE);
    float s = sin(FIELD_RISE);
    return vec3(v.x, c * v.y - s * v.z, s * v.y + c * v.z);
}

/** The ray through view() point uv: the terrain flown over, an object circled. */
void fieldEye(vec2 uv, out vec3 ro, out vec3 rd) {
    if (gFieldSurface == FIELD_TERRAIN) {
        camFly(uv - vec2(0.0, FIELD_TIP), ro, rd);
        ro.y = uFieldGeom.y + FIELD_BOB * clamp(ro.y, -FIELD_WANDER, FIELD_WANDER);
    } else {
        camAround(uv, uFieldGeom.y, FIELD_SPIN, ro, rd);
        ro = fieldRise(ro);
        rd = fieldRise(rd);
    }
    gFieldEye = ro;
}

/** Kaleidoscope: a plane folded into n mirrored wedges about its origin. */
vec2 fieldFold(vec2 c, float n) {
    float r = length(c);
    float seg = FIELD_TAU / n;
    float a = abs(mod(fieldAngle(c), seg) - 0.5 * seg);
    return vec2(cos(a), sin(a)) * r;
}

/** Kaleidoscope and Twist on space: the terrain about the eye's place on the ground, an object about its axis. */
vec3 fieldBend(vec3 p) {
    if (gFieldSurface == FIELD_TERRAIN) {
        vec2 c = p.xz - gFieldEye.xz;
        if (gFieldFolds >= 2.0) c = fieldFold(c, gFieldFolds);
        c = fieldRot(gFieldTwist * length(c)) * c;
        p.xz = gFieldEye.xz + c;
    } else {
        p.xz = fieldRot(gFieldTwist * p.y) * p.xz;
        if (gFieldFolds >= 2.0) p.xz = fieldFold(p.xz, gFieldFolds);
    }
    return p;
}

/**
 * Where p lies on the surface: the field coordinates (s, t) in periods, the ripple's phase there, and
 * how much relief the place takes (the sphere's poles, where every longitude meets, take none).
 */
vec4 fieldPlace(vec3 p) {
    if (gFieldSurface == FIELD_TERRAIN) {
        return vec4(p.z / uFieldShape.x, p.x / uFieldShape.y, 2.4 * length(p.xz - gFieldEye.xz), 1.0);
    }
    float lon = fieldAngle(p.xz) / FIELD_TAU + 0.5;
    if (gFieldSurface == FIELD_TORUS) {
        vec2 m = vec2(length(p.xz) - uFieldGeom.z, p.y);
        return vec4(lon, fieldAngle(m) / FIELD_TAU + 0.5, FIELD_TAU * 6.0 * lon, 1.0);
    }
    float r = length(p);
    float colat = acos(clamp(p.y / max(r, 1.0e-6), -1.0, 1.0));
    return vec4(lon, colat / FIELD_PI, 8.0 * colat, smoothstep(0.0, 0.45, sin(colat)));
}

/** Tile and Warp on the field's coordinates: repeated over the surface, then bent along it (three whole waves a period, so it still closes). */
vec2 fieldWarp(vec2 st) {
    st *= gFieldRepeat;
    float w = 0.05 * gFieldFlow;
    if (w > 0.0) {
        st += w * vec2(sin(FIELD_TAU * 3.0 * st.y + uTime * FIELD_RATE(170)), cos(FIELD_TAU * 3.0 * st.x + uTime * FIELD_RATE(230)));
    }
    return st;
}

/**
 * The height at field coordinates st, 0..1, with its fractal detail: the field, then up to two copies at
 * twice and four times the frequency, each turned a half turn, pushed by the height so far and folded
 * toward a ridge by Morph. `fp` is the ray's footprint in field texels, so each copy is read at its own.
 * f is the field there, its r replaced by the height.
 */
float fieldHeight(vec2 st, float fp, out vec4 f) {
    f = textureLod(uField, st, log2(max(fp, 1.0)));
    float h = f.r;
    float sum = 1.0;
    float amp = 1.0;
    float scale = 1.0;
    vec2 q = st;
    for (int k = 1; k < 3; k++) {
        float w = clamp(gFieldOctaves - float(k), 0.0, 1.0);
        if (w <= 0.0) break;
        amp *= gFieldGain;
        scale *= 2.0;
        q = -2.0 * q + vec2(0.37, 0.61) + 0.05 * gFieldForm * (h / sum - 0.5);
        float v = textureLod(uField, q, log2(max(fp * scale, 1.0))).r;
        v = mix(v, 1.0 - abs(2.0 * v - 1.0), gFieldForm);
        h += amp * w * v;
        sum += amp * w;
    }
    h /= sum;
    f.r = h;
    return h;
}

float fieldDither() {
    vec2 f = gl_FragCoord.xy;
    return fract(52.9829189 * fract(dot(f, vec2(0.06711056, 0.00583715))));
}

/** The palette's dark void behind and around the field, brighter toward the top; reflections show it. */
vec3 fieldSky(vec3 rd) {
    float up = clamp(rd.y * 0.5 + 0.5, 0.0, 1.0);
    return pal(gFieldTone + 0.5 + 0.15 * up) * (0.015 + 0.08 * up * up);
}

/** `exposure` is the present's own; Intensity and PULSE scale the emission. */
vec4 fieldFinish(vec3 col, float exposure) {
    col *= exposure * neonGlow() * (1.0 + 0.5 * neonKnob(0));
    col = max(mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, 1.2), 0.0);
    col = grade(neonAces(col));
    // No pixel leaves as a NaN or an infinity: the feedback pass would carry it for good.
    if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);
    return vec4(col, 1.0);
}
