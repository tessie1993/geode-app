#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP; uSoundTex is R32F.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_glass

// Glass Drops: rain at night, flown through. The old style was a misted
// window with drops on the pane and the city out of focus behind it. Now the
// drops hang in the air of a night street and the camera drifts among them:
// each one a small water sphere, a real ball lens, so the city it holds comes
// out inverted and wide-angle by plain Snell refraction (in through the near
// face, across the chord, out through the far face, three wavelengths at the
// exit so the rim is fringed). The focus is pulled to the rain: the city
// itself is bokeh, six-bladed aperture discs on the building fronts either side
// of the street (warm sodium low down, the palette higher up, fringed red
// outside and blue inside), the family's FRACTAL world glowing in the sky over
// the skyline, and a wet street underfoot that mirrors the lights through
// little ripple rings where the rain lands. Each drop leaves a faint lit wake
// above it, the 3D form of the old clear streak, and a mist of fine droplets
// veils the far drops into the city's glow.
//
// ---- camera ------------------------------------------------------------------
//
// camFly: the eye rides the rig down the street (+z). The eye's z is rebased
// by GD_ZWRAP = 64. Every z term has a period that divides it: the drop cells
// are 1 unit (index wrapped at 64), the facade light cells 2 units (index
// wrapped at 32), the street ripples 0.5 units (wrapped at 128); and 64
// divides the rig's 8192 travel wrap, so neither the rebase nor the wrap shows.
// The facades are cylinders around the street axis, so the lights stream past
// with real parallax: the near row faster than the far one.
//
// ---- audio -------------------------------------------------------------------
//
//   onsets     slide the drops a notch down (glassGlide), each column of the
//              rain at its own pace, on top of a slow fall;
//   snare      a fresh drop swells in a few cells and settles (glassBump);
//   hat        the city twinkles, lights flickering one by one, and the
//              drops' crowns glint;
//   flatness   the mist: noise fogs the street and grains the veil, tones clear it;
//   centroid   the drops' dispersion and the bokeh fringes;
//   bass       swells the drops;

//   pitch      raises the key light, which sets the crown highlights and the
//              foot caustics (GlassLook, as in every glass style).
//
// Nothing here drives the frame's overall brightness from loudness.
//
// ---- macros and knobs -----------------------------------------------------------
//
// Speed scales the fall and the flicker (uTime). Complexity fills more cells
// with drops. Chaos wobbles the drops' refraction, as if they shivered. Energy
// scales every reaction (GlassEar). Colour is pal(); the camera is the rig.
// Knobs, offsets from 0.5 so off is the tuned look: k0 PULSE the bass swell,
// k1 TEXTURE the mist's grain, k2 FLOW the drops' wakes, k3 TONE the hue,
// k4 BUILD the city's lights, k5 SPACE the width of the street.
//
// ---- why the distance estimate is safe ------------------------------------------
//
// The rain is a grid of unit cells; a column of cells (one x, z) slides down
// as a rigid body, so inside a column the cells are still boxes. Each cell
// holds at most one sphere, and jitter plus the largest radius (0.1 + 0.38)
// keeps it inside its own cell. The field is the distance to this cell's
// sphere, clamped to the distance to the cell's walls (plus 0.02, less than any
// sphere's clearance from its walls), so a step never jumps past a neighbour's
// sphere. Hits test the sphere distance alone, never the wall clamp. Outside
// the rain's slab the field is the distance to the slab. A drop shrinks to
// nothing as it nears the eye (its radius is always under its distance from
// the eye), so the eye is never inside one and no carve is needed: the spheres
// stay exact, and the shading uses their analytic normals and chords.
//
// ---- cost ------------------------------------------------------------------------
//
// The march steps through unit cells to GD_FAR (about 20 to 40 steps), with no
// normal taps. A drop pixel reads the facades three times (one per wavelength),
// the fractal sky once through the drop and once behind it; a street pixel
// reads the facades once more for its reflection, without the fractal.

#define GD_MAX_STEPS 96
#define GD_TAU 6.2831853
/** Rebase period of the eye's z; divides 8192. */
#define GD_ZWRAP 64.0
/** Rain is marched to here; beyond it the mist has taken the drops. */
#define GD_FAR 20.0
/** The wet street. */
#define GD_GROUND -2.6
/** The rain's slab: drop centres sit between these heights. */
#define GD_RAIN_LO -2.1
#define GD_RAIN_HI 5.5
/** Largest drop radius in its unit cell (centre jitter 0.1 + 0.38 < 0.5). */
#define GD_RMAX 0.38
/** Water. */
#define GD_IOR 1.333

GlassLook gLk;
vec3 gEye;
float gFall;
float gSwell;
float gSplash;
float gThresh;

float gHue;
float gFringe;
float gTwinkle;
float gRadius;
float gGlow;
/**
 * What the field holds at a point (lib_glass_field): the distance; the cell's drop (rebased world
 * centre, radius; radius 0 when empty) and the distance to its sphere; the wake's distance and weight.
 */
struct GlassHit {
    float d;
    vec3 c;
    float r;
    float ds;
    float wake;
    float wakeW;
};

GlassHit glassMap(vec3 p) {
    // Outside the slab no drop can be nearer than the slab itself.
    float slab = max((GD_RAIN_LO - GD_RMAX - 0.12) - p.y, p.y - (GD_RAIN_HI + GD_RMAX + 0.12));
    GlassHit res;
    res.c = vec3(0.0);
    res.r = 0.0;
    res.ds = 1e3;
    res.wakeW = 0.0;
    res.wake = 1e3;
    res.d = slab + 0.02;
    if (slab > 0.0) return res;

    float colX = floor(p.x);
    float colZ = floor(p.z);
    float cz = mod(colZ, GD_ZWRAP);
    // A column slides as one rigid body: its own rate, its own phase.
    float rate = 0.35 + 0.65 * glassHash(vec2(colX, cz) + 7.7);
    float shift = gFall * rate + 3.0 * glassHash(vec2(cz, colX) + 1.3);
    float y = p.y + shift;
    float cy = floor(y);
    vec3 f = vec3(p.x - colX, y - cy, p.z - colZ) - 0.5;
    vec3 id = vec3(colX, cy, cz);

    vec3 a = abs(f);
    float toEdge = 0.5 - max(a.x, max(a.y, a.z));

    float h = glassHash3(id + 3.3);
    vec3 jit = (vec3(glassHash3(id + 8.1), glassHash3(id + 2.9), glassHash3(id + 5.7)) - 0.5) * 0.2;
    vec3 c = vec3(colX, cy - shift, colZ) + 0.5 + jit;
    // A fresh drop on the snare: a few cells, a new few at every onset; the bump is 0 on the hit, so the set changes unseen.
    float fresh = gSplash * step(0.8, glassHash3(id + floor(SND_ONSET_COUNT) * 0.13));
    float r = (0.10 + 0.13 * fract(h * 7.31)) * (1.0 + gSwell) * (1.0 + 0.35 * fresh);
    // Nearing the eye a drop shrinks to nothing: r < distance, so the eye is never inside one.
    r *= smoothstep(0.35, 1.2, length(c - gEye));
    r = min(r, GD_RMAX);
    if (h < gThresh || c.y < GD_RAIN_LO || c.y > GD_RAIN_HI) r = 0.0;

    float d = toEdge + 0.02;
    if (r > 0.0) {
        vec3 v = f - jit;
        float ds = length(v) - r;
        res.ds = ds;
        d = min(d, ds);
        res.c = c;
        res.r = r;
        // The wake: a thin lit thread above the drop, fading upward to the cell's top.
        res.wake = length(v.xz);
        res.wakeW = step(0.0, v.y) * (1.0 - smoothstep(0.0, 0.5 - jit.y, v.y)) * (r / GD_RMAX);
    }
    res.d = d;
    return res;
}

//#include lib_glass_field

/**
 * One row of city lights: bokeh discs on the inside of a cylinder of radius R
 * around the street axis, N cells round it and 2 units along it, seen from `o`
 * along `d`. `facade` says the ray met a building front (below this column's
 * skyline), where the sky behind should darken.
 */
vec3 cityLights(vec3 o, vec3 d, float R, float N, float seed, float blur, out float facade) {
    facade = 0.0;
    float a = dot(d.xy, d.xy);
    if (a < 1e-5) return vec3(0.0);
    float b = dot(o.xy, d.xy);
    float cc = dot(o.xy, o.xy) - R * R;
    float t = (-b + sqrt(max(b * b - a * cc, 0.0))) / a;
    float fade = 1.0 - smoothstep(60.0, 140.0, t);
    if (fade <= 0.0) return vec3(0.0);
    vec3 hp = o + d * t;
    // N is whole, so the fract seam of the angle falls on a cell wall.
    float u = fract(atan(hp.y, hp.x) / GD_TAU) * N;
    vec2 x = vec2(u, hp.z * 0.5);
    vec2 id = floor(x);
    id.y = mod(id.y, 32.0);
    float sky = GD_GROUND + 5.0 + 11.0 * glassHash(vec2(mod(floor(u / 3.0), N), seed + 0.7));
    facade = (1.0 - smoothstep(sky - 0.5, sky + 0.5, hp.y)) * fade;
    if (hp.y > sky || hp.y < GD_GROUND) return vec3(0.0);

    vec2 f = fract(x) - 0.5;
    float h = glassHash(id + seed);
    if (h < 0.35) return vec3(0.0);
    vec2 off = (glassHash2(id + seed * 1.7) - 0.5) * 0.2;
    float low = 1.0 - smoothstep(GD_GROUND, GD_GROUND + 6.0, hp.y);
    vec3 col = mix(pal(gHue + h * 0.6), vec3(1.0, 0.62, 0.25), low * 0.75);
    float r = 0.06 + 0.24 * blur;
    vec2 q = abs(f - off);
    // Hexagonal distance: a six-bladed aperture's outline.
    float dd = max(dot(q, vec2(0.8660254, 0.5)), q.y) - r;
    float w = 0.012 + 0.07 * blur;
    float flick = 1.0 - gTwinkle * step(0.6, fract(h * 13.0 + glassTime() * 1.7)) * 0.8;
    float body = 1.0 - smoothstep(-w, w, dd);
    float rimR = 1.0 - smoothstep(0.0, w * 2.0, abs(dd + gFringe * 0.04));
    float rimB = 1.0 - smoothstep(0.0, w * 2.0, abs(dd + w * 2.0 + gFringe * 0.02));
    vec3 c = col * body * (0.5 + 0.5 * (h - 0.35) / 0.65) * 0.06 / (r * r + 0.02);
    c += (vec3(1.0, 0.3, 0.2) * rimR + vec3(0.2, 0.4, 1.0) * rimB) * gFringe * body * 0.35;
    return c * flick * fade * gGlow;
}

/**
 * The city from `o` along `d`, defocused by `blur`: both rows of lights, the
 * fractal sky over the skyline (skipped when `skyAmt` is 0) and the glow down
 * the street at both ends.
 */
vec3 cityBack(vec3 o, vec3 d, float blur, float skyAmt) {
    float f1;
    float f2;
    vec3 col = 0.5 * cityLights(o, d, gRadius, 64.0, 1.0, blur, f1);
    col += 0.35 * cityLights(o, d, gRadius * 1.6, 96.0, 5.0, blur, f2);
    float facade = max(f1, f2);
    if (skyAmt > 0.0) {
        GlassLook r = gLk;
        r.rough = gLk.rough + 0.18 * blur;
        col += glassFractal(d, r) * skyAmt * (1.0 - 0.45 * blur) * (1.0 - 0.7 * facade);
    } else {
        col += pal(gHue + 0.6) * 0.02 * (1.0 - facade);
    }
    float along = pow(abs(d.z), 14.0) * (1.0 - smoothstep(0.0, 0.25, abs(d.y - 0.03)));
    col += mix(pal(gHue + 0.08), vec3(1.0, 0.62, 0.25), 0.5) * 0.22 * along * gGlow;
    return col;
}

/** Rain landing on the street: expanding ripple rings in half-unit cells, as a tilt of the street's normal. */
vec2 streetRipple(vec2 xz, float t) {
    vec2 g = xz * 2.0;
    vec2 id = floor(g);
    id.y = mod(id.y, 128.0);
    vec2 f = fract(g) - 0.5;
    float h = glassHash(id + 9.1);
    if (h < 0.5) return vec2(0.0);
    vec2 o = (glassHash2(id + 2.7) - 0.5) * 0.3;
    float ph = fract(t * 0.6 * (0.6 + 0.8 * h) + h * 7.0);
    vec2 dv = f - o;
    float r = length(dv);
    float e = r - ph * 0.45;
    float ring = sin(e * 40.0) * exp(-abs(e) * 30.0) * (1.0 - ph);
    return dv / max(r, 1e-3) * ring * 0.25;
}

void main() {
    vec2 uv = view();
    glassSetup();
    float t = glassTime();

    float kPulse = gGlassIn.pulse - 0.5;
    float kTexture = gGlassIn.texture - 0.5;
    float kFlow = gGlassIn.flow - 0.5;
    float kTone = gGlassIn.tone - 0.5;
    float kBuild = gGlassIn.build - 0.5;
    float kSpace = gGlassIn.space - 0.5;

    GlassEar ear = glassListen();
    gHue = 0.05 * sin(t * 0.02) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue) + 0.12 * kTone;
    gLk = glassLook(GLASS_ENV_FRACTAL, gHue, ear);
    // The slow fall plus a notch per onset. Energy is not on the count: a change to it would jump every column.
    float slide = glassGlide(SND_ONSET_COUNT, SND_ONSET_ENV) * 0.35;
    gFall = t * 0.12 + slide;
    gSwell = 0.2 * ear.bass * clamp(1.0 + 2.0 * kPulse, 0.0, 2.0);
    gSplash = glassBump(SND_SNARE_ENV) * glassEnergy();
    gThresh = 0.55 - 0.25 * glassComplexity();

    gFringe = 0.4 + 0.8 * ear.sharp;
    gTwinkle = ear.hat;
    gRadius = 22.0 * (1.0 + 0.6 * kSpace);
    gGlow = clamp(1.0 + 1.2 * kBuild, 0.2, 2.2);
    float mist = 0.55 + 0.45 * ear.frost;
    float spread = glassSpread(0.006 + 0.02 * ear.sharp);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 64 (see the header): exact, invisible, and z keeps its precision.
    ro.z = mod(ro.z, GD_ZWRAP);
    gEye = ro;

    float tGround = (rd.y < -1e-4 && ro.y > GD_GROUND) ? (GD_GROUND - ro.y) / rd.y : 1e4;
    float tEnd = min(GD_FAR, tGround);

    float tm = 0.02;
    float hitT = -1.0;
    float wake = 0.0;
    // The field where the ray met a drop: its centre and radius, for the lens.
    GlassHit hit;
    hit.c = vec3(0.0);
    hit.r = 0.0;
    for (int i = 0; i < GD_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (tm > tEnd) break;
        GlassHit at = glassField(ro + rd * tm);
        float d = at.d;
        float eps = glassEps(tm);
        if (at.ds < eps) {
            hitT = tm;
            hit = at;
            break;
        }
        wake += at.wakeW * exp(-at.wake * 30.0 - tm * 0.12) * min(d, 0.3);
        tm += max(d, eps);
    }

    // What lies behind, as the defocused city; below the horizon, the horizon itself (the mist's colour there).
    vec3 bgDir = normalize(vec3(rd.x, max(rd.y, 0.02), rd.z));
    vec3 back = cityBack(ro, bgDir, 1.0, 0.6);
    float grain = glassNoise(uv * 140.0) * 0.5 + 0.5;
    vec3 veil = back * 0.6 + pal(gHue + 0.6) * 0.012;
    float density = 0.035 + 0.06 * ear.frost;
    vec3 col = back;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalize(p - hit.c);
        n = glassWobble(n, p, glassChaos());
        float cosI = clamp(dot(n, -rd), 0.0, 1.0);
        // The ball lens: in, across the chord, out per wavelength. The far wall's exit inverts the city.
        vec3 d1 = glassRefract(rd, n, 1.0 / glassIor(GD_IOR));
        vec3 oc = p - hit.c;
        float b = dot(oc, d1);
        float chord = max(-b + sqrt(max(b * b - (dot(oc, oc) - hit.r * hit.r), 0.0)), 0.0);
        vec3 q = p + d1 * chord;
        vec3 n2 = normalize(q - hit.c);
        vec3 iors = glassIors(glassIor(GD_IOR), spread);
        vec3 er = glassRefract(d1, -n2, iors.x);
        vec3 eg = glassRefract(d1, -n2, iors.y);
        vec3 eb = glassRefract(d1, -n2, iors.z);
        float fx;
        vec3 img = vec3(
            0.5 * cityLights(q, er, gRadius, 64.0, 1.0, 0.55, fx).r + 0.35 * cityLights(q, er, gRadius * 1.6, 96.0, 5.0, 0.55, fx).r,
            0.5 * cityLights(q, eg, gRadius, 64.0, 1.0, 0.55, fx).g + 0.35 * cityLights(q, eg, gRadius * 1.6, 96.0, 5.0, 0.55, fx).g,
            0.5 * cityLights(q, eb, gRadius, 64.0, 1.0, 0.55, fx).b + 0.35 * cityLights(q, eb, gRadius * 1.6, 96.0, 5.0, 0.55, fx).b);
        GlassLook sharp = gLk;
        sharp.rough = gLk.rough + 0.1;
        // An exit ray pointing down sees the wet street: dim it rather than read the sky there.
        img += glassFractal(eg, sharp) * 0.6 * smoothstep(-0.25, 0.1, eg.y);
        img = img * 1.5 + pal(gHue + 0.6) * 0.05;
        float rim = 1.0 - cosI;
        img *= 1.0 - 0.7 * smoothstep(0.6, 1.0, rim);
        float fres = glassFresnel(cosI, glassIor(GD_IOR));
        float fy;
        vec3 rf = reflect(rd, n);
        img += cityLights(p, rf, gRadius, 64.0, 1.0, 1.0, fy) * 0.5 * fres * 3.0;
        // Crown highlight from the key light, caustic crescent at the foot.
        vec3 key = glassKeyDir(gLk);
        img += vec3(1.0, 0.97, 0.92) * 1.6 * pow(max(dot(rf, key), 0.0), 90.0);
        float foot = smoothstep(0.1, 0.7, -dot(n, uCamBasis[1]));
        img += mix(vec3(1.0, 0.8, 0.5), pal(gHue), 0.3) * 0.5 * smoothstep(0.45, 0.75, rim) * (1.0 - smoothstep(0.8, 0.97, rim)) * foot;
        img += glassRim(cosI, gHue, 0.25);
        img += vec3(1.0) * glassGlint(n * 3.0 + hit.c * 9.0, 1.0, ear.hat * 0.4);
        // The mist takes the far drops back into the city's glow, gone before the march's far end so none pops.
        col = mix(back, img, exp(-hitT * density) * (1.0 - smoothstep(GD_FAR * 0.6, GD_FAR, hitT)));
    } else if (tGround < 1e3) {
        vec3 p = ro + rd * tGround;
        vec2 rip = streetRipple(p.xz, t);
        vec3 n = normalize(vec3(-rip.x, 1.0, -rip.y));
        vec3 rf = reflect(rd, n);
        rf.y = max(rf.y, 0.01);
        float cosI = clamp(-rd.y, 0.0, 1.0);
        float m = 1.0 - cosI;
        float fres = mix(0.25, 1.0, m * m * m * m * m);
        // Wet asphalt: the facades smeared into the street, a little rougher than the air above it.
        vec3 street = cityBack(p, normalize(rf), 1.0, 0.0) * fres * 0.8 + pal(gHue + 0.6) * 0.006;
        float fog = 1.0 - exp(-tGround * density);
        col = mix(street, back * 0.8, fog);
    }

    // The wakes: thin threads of the city's light hanging above the drops.
    col += mix(pal(gHue + 0.1), vec3(1.0, 0.7, 0.4), 0.5) * wake * 0.5 * clamp(1.0 + 1.6 * kFlow, 0.0, 2.0);
    // The mist: micro-droplets in the air scatter the city into a glow, with a fine grain.
    float grainK = clamp(1.0 + 1.6 * kTexture, 0.0, 2.0);
    col = col * (1.0 - 0.25 * mist) + veil * mist * 0.4 * (1.0 + 0.4 * grainK * (grain - 0.5));

    col *= 0.85 + 0.15 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(glassTone(col)), 1.0);
}
