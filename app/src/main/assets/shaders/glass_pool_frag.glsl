#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_glass

// Glass Pool: an endless lane pool at dusk, flown over a little above the
// water. The old style looked straight down at one patch of floor; now the
// pool is a place. Seven lanes run down +z between tiled walls, the lane ropes
// float on the surface as strings of bobbing palette floats, a stone deck
// edges both sides and stops at an infinity edge, past which the dusk world
// (GLASS_ENV_DUSK: the low sun, the aurora over the hills, the sea) fills the
// horizon. Through the moving surface the floor keeps the old style's whole
// identity: a mosaic of small tiles glazed with the FRACTAL world, grout lines,
// a dark stripe down each lane (with a cross bar every 32 m), and the caustic
// net in three wavelengths (glassCausticRGB) splitting into thin rainbows where
// it folds. The surface reflects the dusk sky and the coping, faint face on and
// bright at grazing as water does, with a road of sun glitter toward the sun.
//
// ---- camera -------------------------------------------------------------------
//
// camFly: the eye rides the rig's path down the pool. Every path keeps it
// inside the walls (glide wanders under 3 m, orbit circles under 5.4 m, the pool
// is 7 m to a side), and its height is held at least 0.45 m over the water, so
// a low path skims the surface instead of diving through it. The eye's z is
// rebased by PL_ZWRAP (64); every z term has a period dividing it: the floats
// (0.5), the tiles (a whole number per 64 m), the glaze mirror (8), the lane
// cross bars (32), the depth profile (64), and the waves themselves, whose z
// wavenumbers are snapped to whole turns per 64 m. 64 divides the rig's 8192
// travel wrap, so neither the rebase nor the wrap ever shows.
//
// ---- audio ------------------------------------------------------------------
//
//   kick         drops a ring ripple ahead of the eye, at a spot that hops per
//                onset; it spreads and fades, bending the floor through the
//                water, pulling the caustics into a ring and rocking the floats;
//   snare        a gust: the chop rises and the net tightens;
//   bass         the swell's height;
//   centroid     caustic sharpness: bright mixes cut thin, bright lines;
//   flatness     turbidity: the water clouds, softening the floor and the net;

//   pitch        the sun's height;
//   hat          the glitter sparkles, and glints on the floats.
//
// ---- macros and knobs ----------------------------------------------------------
//
// Speed scales the waves (glassTime). Complexity makes the tiles smaller.
// Chaos stirs a cross-current into the surface. Energy scales every reaction.
// Colour is pal(); Space is the rig. Knobs, offsets from 0.5 so off is the
// tuned look: k0 PULSE the kick ripple, k1 TEXTURE the tile size, k2 FLOW the
// chop, k3 TONE the hue, k4 BUILD the sun and its glitter, k5 SPACE the depth.
//
// ---- why the distance estimate is safe ------------------------------------------
//
// The water is a height field inside the walls: (y - h) times PL_LIP, which
// must be under 1 / sqrt(1 + s^2) for the steepest slope s. The five waves
// slope at most 0.128 x chop (chop under 2.8 with a full gust and FLOW up),
// 0.35; the kick ring at most 0.31 with PULSE up; Chaos's cross-current bends
// the domain by at most 1.3, so s < 0.86 and the bound is 0.76 > 0.7. The
// surface never rises 0.3 above the rest level, so above 0.6 the field is the
// plain plane distance less 0.3. The deck is an intersection of half-spaces
// (max of plane distances, a lower bound). Each float measures only its own
// capsule; a neighbour's is at least 0.075 past the cell edge, so the distance
// is clamped to the edge plus that. Far from the ropes the field is the
// distance to the slab the floats can occupy. A sphere around the eye is carved
// out of everything. Below the surface nothing is marched: the refracted ray
// meets the floor or a wall analytically.
//
// ---- cost ---------------------------------------------------------------------
//
// The march spends the Detail budget (uSteps, capped at PL_MAX_STEPS), one
// five-wave sum a step near the water and one more near a rope. A water hit
// then costs one wave sum for the normal, one for the caustics, one glaze
// lookup and three grout reads (the dispersion); deck and floats a normal.

#define PL_MAX_STEPS 112
#define PL_FAR 56.0
#define PL_TAU 6.2831853
/** Rebase period of the eye's z; divides 8192. */
#define PL_ZWRAP 64.0
/** Rest level of the water, world y. */
#define PL_WATER -1.9
/** Half-width of the pool: inner wall to the axis. */
#define PL_HALF 7.0
/** Lane width; seven lanes, six ropes. */
#define PL_LANE 2.0
/** Coping height above the rest level. */
#define PL_DECK 0.35
/** Deck width out to the infinity edge. */
#define PL_DECKW 4.0
/** Wave height scale and base wavenumber, world units. */
#define PL_HS 0.12
#define PL_WK 1.2
/** Height-field Lipschitz factor (see the header). */
#define PL_LIP 0.7
/** World units between floats on a rope; divides 64. */
#define PL_PITCH 0.5
/** Caustic focus per metre of water. */
#define PL_FOCUS 7.0
#define PL_BUBBLE 0.3

GlassLook gLk;
vec3 gEye;
float gT;
float gChop;
float gCross;
vec2 gAt;
float gAge;
float gAmp;
float gHue;
float gTile;
float gDepthGain;
/** What the field holds at a point (lib_glass_field): the distance; 0 water, 1 deck, 2 float; the float's rope x and cell z. */
struct GlassHit {
    float d;
    float mat;
    float rope;
    float cell;
};

/**
 * The surface at world point xz: height in .x, slope in .yz, its Hessian
 * (xx, zz, xz) in `hess`. The five waves of the family (golden-angle
 * directions, deep-water speeds), with z wavenumbers snapped to whole turns
 * per PL_ZWRAP so the field tiles with the rebase, plus the kick's ring.
 */
vec3 surface(vec2 xz, out vec3 hess) {
    vec2 p = xz + gCross * vec2(sin(xz.y * (PL_TAU / 16.0) + gT), cos(xz.x * 1.1 - gT));
    float h = 0.0;
    vec2 g = vec2(0.0);
    vec3 hs = vec3(0.0);
    float kr = 1.0;
    for (int i = 0; i < 5; i++) {
        float fi = float(i);
        float a = 2.39996 * fi + 0.6;
        float kw = PL_WK * kr;
        vec2 kv = vec2(kw * cos(a), floor(kw * sin(a) * (PL_ZWRAP / PL_TAU) + 0.5) * (PL_TAU / PL_ZWRAP));
        float amp = PL_HS * gChop * 0.32 / (kr * kr);
        float ph = dot(kv, p) + sqrt(kr) * 1.6 * gT + fi * 1.7;
        float s = sin(ph);
        float c = cos(ph);
        h += amp * s;
        g += amp * c * kv;
        hs -= amp * s * vec3(kv.x * kv.x, kv.y * kv.y, kv.x * kv.y);
        kr *= 1.43;
    }
    // The kick's ring: a front leaving the drop point as the kick envelope falls.
    vec2 d = xz - gAt;
    float r = length(d) + 1e-4;
    float front = gAge * 2.6;
    float env = exp(-(r - front) * (r - front) * 4.0) * exp(-gAge * 1.4) * gAmp;
    float k = 10.8;
    float rph = k * (r - front);
    float A = 0.012;
    vec2 n = d / r;
    h += env * sin(rph) * A;
    g += n * env * cos(rph) * A * k;
    hs += -env * sin(rph) * A * k * k * vec3(n.x * n.x, n.y * n.y, n.x * n.y);
    hess = hs;
    return vec3(h, g);
}

/** Water depth under the rest level at z: shallow and deep ends, period 64. */
float poolDepth(float z) {
    return mix(1.4, 2.6, 0.5 + 0.5 * sin(z * (PL_TAU / 64.0))) * gDepthGain;
}

GlassHit glassMap(vec3 p) {
    vec3 hh;
    float dy = p.y - PL_WATER;
    float inPool = abs(p.x) - PL_HALF;
    // Water, only inside the walls; above 0.6 the waves cannot reach, so the plane distance less their height.
    float dw = dy > 0.6 ? dy - 0.3 : (dy - surface(p.xz, hh).x) * PL_LIP;
    dw = max(dw, inPool);
    // The deck: outside the walls, under the coping, out to the infinity edge.
    float dd = max(max(-inPool, abs(p.x) - PL_HALF - PL_DECKW), dy - PL_DECK);
    // The lane ropes: the nearest of the six, a string of capsule floats riding the surface.
    float lx = clamp(floor((p.x + PL_HALF) / PL_LANE + 0.5), 1.0, 6.0) * PL_LANE - PL_HALF;
    float qx = p.x - lx;
    float df = max(abs(qx) - 0.08, abs(dy) - 0.32);
    float cz = (floor(p.z / PL_PITCH) + 0.5) * PL_PITCH;
    if (df < 0.3) {
        float bob = 0.8 * surface(vec2(lx, cz), hh).x;
        vec3 r = vec3(qx, dy - bob - 0.02, p.z - cz);
        r.z -= clamp(r.z, -0.1, 0.1);
        df = min(length(r) - 0.075, 0.5 * PL_PITCH - abs(p.z - cz) + 0.075);
    }
    float d = min(dw, min(dd, df));
    GlassHit h;
    h.mat = d == df ? 2.0 : (d == dd ? 1.0 : 0.0);
    h.rope = lx;
    h.cell = cz;
    h.d = max(d, PL_BUBBLE - length(p - gEye));
    return h;
}

//#include lib_glass_field


/** Grout at tile coordinates f: 1 on the line. */
float grout(vec2 f) {
    vec2 e = abs(fract(f / gTile) - 0.5);
    return smoothstep(0.455, 0.48, max(e.x, e.y));
}

/**
 * The tiles at `f` (floor xz, or wall z and height), glazed with the FRACTAL
 * world, mirrored every 8 m so the glaze has no seam; the grout read once per
 * wavelength at offsets `dsp`, so tile edges fringe where the water bends.
 */
vec3 poolTiles(vec2 f, vec2 dsp, bool wall) {
    vec2 m = (abs(fract(f / 8.0) * 2.0 - 1.0) - 0.5) * 2.4;
    vec3 glaze = glassFractal2(m, gLk, 0.0) * 2.0;
    float h = glassHash(floor(f / gTile));
    vec3 c = glaze * (0.75 + 0.5 * h) + pal(gHue + 0.5) * 0.05 * (0.5 + h);
    if (!wall) {
        // The lane stripe, and its cross bar every 32 m.
        float lx = abs(mod(f.x + PL_HALF, PL_LANE) - 0.5 * PL_LANE);
        float stripe = 1.0 - smoothstep(0.12, 0.14, lx);
        float bar = (1.0 - smoothstep(0.45, 0.5, abs(mod(f.y, 32.0) - 16.0))) * (1.0 - smoothstep(0.5, 0.52, lx));
        c = mix(c, pal(gHue + 0.62) * 0.03, max(stripe, bar) * 0.85);
    }
    vec3 g = vec3(grout(f + dsp), grout(f), grout(f - dsp));
    return mix(c * 0.6, c * 0.3 + 0.05, g);
}

/** The coping face above the water at (z, height over the rest level): pale tiles, a dark band at the waterline. */
vec3 copingFace(vec2 f) {
    vec2 e = abs(fract(f / 0.125) - 0.5);
    float gr = smoothstep(0.43, 0.47, max(e.x, e.y));
    vec3 c = mix(vec3(0.62, 0.66, 0.68), pal(gHue + 0.1), 0.2);
    c = mix(c, pal(gHue + 0.6) * 0.08, smoothstep(0.08, 0.1, f.y) * (1.0 - smoothstep(0.17, 0.19, f.y)));
    return mix(c, c * 0.4, gr);
}

void main() {
    vec2 uv = view();
    glassSetup();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Rebase z: every z term tiles with a period dividing 64 (see the header).
    ro.z = mod(ro.z, PL_ZWRAP);
    // Skim, never dive: the eye stays over the water and the floats.
    ro.y = max(ro.y, PL_WATER + 0.45);
    gEye = ro;

    float kPulse = gGlassIn.pulse - 0.5;
    float kTexture = gGlassIn.texture - 0.5;
    float kFlow = gGlassIn.flow - 0.5;
    float kTone = gGlassIn.tone - 0.5;
    float kBuild = gGlassIn.build - 0.5;
    float kSpace = gGlassIn.space - 0.5;

    gT = glassTime() * 0.8;
    GlassEar ear = glassListen();
    gHue = 0.05 * sin(gT * 0.02) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue) + 0.3 * kTone;
    GlassLook lk = glassLook(GLASS_ENV_DUSK, gHue, ear);
    gLk = glassLook(GLASS_ENV_FRACTAL, gHue, ear);
    gChop = (0.55 + 0.35 * clamp(ear.bass, 0.0, 1.2) + 0.5 * ear.snare) * (1.0 + 0.8 * kFlow);
    gCross = glassChaos() * 0.33;
    float sharp = mix(0.05, 0.012, ear.sharp) * (1.0 + 2.0 * ear.frost);
    // A whole number of tiles per 64 m, so the mosaic survives the rebase.
    gTile = PL_ZWRAP / floor(PL_ZWRAP / (mix(0.25, 0.125, glassComplexity()) * (1.0 + 0.6 * kTexture)) + 0.5);
    gDepthGain = 1.0 + 0.6 * kSpace;

    // The kick's ring: dropped ahead of the eye at a spot that hops with each onset, ageing as the envelope falls.
    vec2 fwd = uCamBasis[2].xz;
    fwd = dot(fwd, fwd) > 1e-4 ? normalize(fwd) : vec2(0.0, 1.0);
    float onset = floor(SND_ONSET_COUNT);
    vec2 hop = vec2(glassHash1(onset), glassHash1(onset + 9.0));
    gAt = ro.xz + fwd * (5.0 + 6.0 * hop.y) + vec2(fwd.y, -fwd.x) * (hop.x - 0.5) * 6.0;
    gAt.x = clamp(gAt.x, -PL_HALF + 1.0, PL_HALF - 1.0);
    gAge = (1.0 - clamp(SND_KICK_ENV, 0.0, 1.0)) * 1.6;
    gAmp = clamp(glassEnergy(), 0.0, 1.5) * (1.0 + 1.2 * kPulse);

    vec3 key = glassKeyDir(lk);
    // The sun as the dusk world draws it: low, on the key's bearing.
    vec3 sunDir = normalize(vec3(key.x, mix(0.04, 0.5, lk.height), key.z));
    vec3 sunCol = mix(vec3(1.0, 0.92, 0.75), pal(gHue + 0.1), 0.2) * (1.0 + 0.8 * kBuild);

    // ---- the march ----
    float t = 0.02;
    float hitT = -1.0;
    for (int i = 0; i < PL_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * t;
        // Past the infinity edge and below the deck: the dusk sea, drawn by the world.
        if (p.y < PL_WATER - 2.5) break;
        float d = glassField(p).d;
        if (d < glassEps(t)) {
            hitT = t;
            break;
        }
        t += d;
        if (t > PL_FAR) break;
    }

    vec3 sky = glassEnv(rd, lk);
    vec3 col = sky;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        GlassHit hit = glassField(p);
        float mat = hit.mat;
        float rope = hit.rope;
        float cell = hit.cell;
        if (mat < 0.5) {
            // ---- water ----
            vec3 hs;
            vec3 w = surface(p.xz, hs);
            vec3 n = normalize(vec3(-w.y, 1.0, -w.z));
            float fr = glassFresnel(max(dot(n, -rd), 0.0), glassIor(1.33));
            vec3 rf = refract(rd, n, 1.0 / glassIor(1.33));
            rf = normalize(vec3(rf.x, min(rf.y, -0.05), rf.z));
            // The floor, met analytically, refined once for the depth profile; or a wall first.
            float tF = (p.y - (PL_WATER - poolDepth(p.z))) / -rf.y;
            tF = (p.y - (PL_WATER - poolDepth(p.z + rf.z * tF))) / -rf.y;
            float tW = abs(rf.x) > 1e-4 ? (sign(rf.x) * PL_HALF - p.x) / rf.x : 1e5;
            bool wall = tW < tF;
            float tU = min(tF, tW);
            vec3 q = p + rf * tU;
            vec2 f = wall ? vec2(q.z, q.y - PL_WATER) : q.xz;
            vec2 dsp = (wall ? vec2(rf.z - rd.z, 0.0) : rf.xz - rd.xz) * tU * 0.05;
            vec3 under = poolTiles(f, dsp, wall);
            // The caustic net where q is, from the waves above it, leaned toward the sun.
            float below = max(PL_WATER - q.y, 0.0);
            vec3 ch;
            surface(q.xz - sunDir.xz / max(sunDir.y, 0.25) * below * 0.35, ch);
            vec3 caus = glassCausticRGB(ch, PL_FOCUS * below, 0.06 + 0.06 * ear.sharp, sharp);
            under *= 0.35 + 2.4 * caus * sunCol * (wall ? 0.6 : 1.0);
            // Absorption toward the palette's deep colour over the path, clouded by noise.
            vec3 water = mix(vec3(0.05, 0.35, 0.45), pal(gHue + 0.5), 0.35);
            float murk = 1.0 - exp(-tU * (0.18 + 0.35 * ear.frost));
            under = mix(under, water * 0.35, murk);
            // The reflection: the dusk world, or the coping where the bounced ray meets a wall.
            vec3 r = reflect(rd, n);
            r.y = abs(r.y);
            vec3 refl = glassEnv(r, lk);
            if (abs(r.x) > 1e-4) {
                float tr = (sign(r.x) * PL_HALF - p.x) / r.x;
                float yr = p.y + r.y * tr - PL_WATER;
                if (tr > 0.0 && yr < PL_DECK) refl = copingFace(vec2(p.z + r.z * tr, yr)) * (0.25 + 0.6 * sunCol * max(-sign(r.x) * sunDir.x, 0.2));
            }
            // The sun's road and its glitter on the crests.
            float road = pow(max(dot(r, sunDir), 0.0), 40.0) * 0.25;
            float glit = pow(max(dot(r, sunDir), 0.0), 700.0);
            glit *= (0.3 + 2.5 * ear.hat) * step(0.8, glassHash(floor(p.xz * 40.0) + floor(gT * 8.0)));
            col = under * (1.0 - fr) + refl * fr * 2.0 + sunCol * (road + glit * 3.0) * (1.0 + 0.6 * kBuild);
        } else {
            vec3 n = glassNormal(p, max(glassEps(hitT), 0.0015));
            float dif = max(dot(n, sunDir), 0.0);
            float fres = glassFresnel(max(dot(n, -rd), 0.0), 1.5);
            if (mat < 1.5) {
                // ---- the deck and the coping ----
                vec3 base;
                if (n.y > 0.6) {
                    vec2 e = abs(fract(p.xz) - 0.5);
                    float gr = smoothstep(0.46, 0.49, max(e.x, e.y));
                    base = mix(vec3(0.5, 0.52, 0.55), pal(gHue + 0.1), 0.2) * 0.4 * (0.85 + 0.3 * glassHash(floor(p.xz)));
                    base *= 1.0 - 0.5 * gr;
                } else {
                    base = copingFace(vec2(p.z, p.y - PL_WATER)) * 0.5;
                    // Light thrown up off the water dances on the coping face.
                    vec3 ch;
                    surface(vec2(sign(p.x) * (PL_HALF - 0.4), p.z), ch);
                    base += 0.35 * glassCausticRGB(ch, PL_FOCUS * (0.4 + p.y - PL_WATER), 0.06, sharp) * sunCol;
                }
                col = base * (0.25 + 1.1 * dif * sunCol) + glassEnv(reflect(rd, n), lk) * fres * 0.35;
            } else {
                // ---- a float ----
                float grp = mod(floor(cell / (PL_PITCH * 8.0)), 2.0);
                float li = (rope + PL_HALF) / PL_LANE;
                vec3 base = glassVivid(pal(gHue + 0.33 * grp + 0.07 * li), 0.6);
                float wrap = 0.5 + 0.5 * dot(n, sunDir);
                float spec = pow(max(dot(reflect(rd, n), sunDir), 0.0), 40.0);
                col = base * (0.12 + 0.8 * wrap) * sunCol + sunCol * spec * 0.8
                    + glassRim(max(dot(n, -rd), 0.0), gHue, 0.35)
                    + sunCol * glassGlint(p, 18.0, ear.hat * 0.5);
            }
        }
        // Distance haze into the dusk world along the ray.
        col = mix(col, sky, 1.0 - exp(-max(hitT - 6.0, 0.0) * 0.03));
    }

    col *= 0.85 + 0.15 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(glassTone(col)), 1.0);
}
