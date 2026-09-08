// The look the raymarched styles share: what makes a marched surface read as
// a jewel rather than a grey render, and what makes the empty space around it
// read as a place rather than a black rectangle.
//
// WHY THIS EXISTS
//
// lib_sdf3 is geometry only - it deliberately reads no uniform - so every
// marched style shaded its own hit with its own diffuse-plus-fresnel block,
// and eight styles had eight slightly different flat looks. The reference
// material this family is built from (the visionary-art rendering of the DMT
// visual: Grey, Jones, the many "chrysanthemum" replications) is consistent
// about what a surface there looks like, and none of it is a matte diffuse:
//
//   - COLOUR THAT MOVES WITH THE VIEW. A thin-film sheen whose hue depends on
//     the angle of incidence, so a body turning through the frame changes
//     colour across its own curvature - the beetle-shell / oil-slick tell.
//   - A CHROMATIC RIM. The silhouette is not one bright line but a dispersed
//     one, warm inside and cold at the very edge, which is what a Fresnel
//     term with a different exponent per channel produces.
//   - A REFLECTED ENVIRONMENT. The highlights are shaped - a horizontal
//     softbox band, not a point - which is the difference between chrome and
//     plastic.
//   - NESTED SHELLS OF HUE. Every body is banded into concentric layers of
//     related colour (the orbit-trap look), never one flat tone.
//   - GLOW. Everything emits a little, and thin parts are translucent.
//
// dmtShade() is all of that in one call, and the argument list is the whole
// contract: a normal, a view direction, a hue, a banding coordinate, an
// occlusion term, a thinness term and the treble. A style that has those
// gets the material; a style that has more (its own orbit trap, its own
// heat) adds it on top.
//
// The other half is the SKY. A marched frame is mostly miss, and the misses
// were a flat ramp. In the references the background is never flat: it is
// the chrysanthemum, the rotating, infinitely detailed mandala that every
// account of the opening seconds describes, and which the removed hyperspace
// style drew as "an infinitely detailed filigree evaluated on the ray
// direction, so it has no edges and turns with the view". dmtChrysanthemum()
// is that: a kaleidoscopic fold of the ray direction with two orbit traps
// (knots and threads), dim enough to sit behind geometry and never black.
//
// Then the life of the frame - the references are never one object turning:
//
//   dmtLife()        a body's birth-hold-dissolve envelope on its own clock
//   dmtMorphBody()   a closed ring of primitives one body walks around
//   dmtSatellites()  a bank of such bodies orbiting a host, each alive
//   dmtTunnel*()     a flight path that bends in every direction, and the
//                    space warp that bends a straight tunnel along it
//
// INCLUDE ORDER: after lib_scene_uniforms, lib_scene_motion, lib_palette and
// lib_sdf3 - it reads pal(), the motion uniforms and the sdf primitives.
//
// LIPSCHITZ. Every distance helper below states its bound, in the form
// lib_sdf3 requires. The material and the sky read no distance and need none.

#define DMT_TAU 6.2831853

// ===========================================================================
//  the material
// ===========================================================================

/**
 * Key and fill, fixed in WORLD space. Fixed rather than view-locked so a body
 * turning through them is what reveals its shape; a headlight flattens every
 * fold into the same grey. The two are nearly opposed so that no orientation
 * is unlit.
 */
const vec3 DMT_KEY = vec3(0.42, 0.74, -0.52);
const vec3 DMT_FILL = vec3(-0.64, -0.10, 0.76);

/**
 * How far the thin-film sheen walks the palette across the incidence range.
 *
 * 0.2 of a turn from face-on to grazing: enough that the sheen is a
 * DIFFERENT colour from the body and reads as a film on it. 0.45 was tried
 * and is too far - on the Spectrum palette it sweeps most of the wheel across
 * one hemisphere, which draws concentric rainbow rings on every sphere and
 * whitens the body under them.
 */
const float DMT_FILM_SPAN = 0.20;

/**
 * Per-channel Fresnel exponents. Red falls off slowest so it survives furthest
 * from the edge, blue fastest so it lives only at the silhouette: the rim is
 * warm inside and cold outside, which is dispersion in one line.
 */
const vec3 DMT_FRESNEL_POW = vec3(2.4, 3.4, 4.8);

/**
 * The reflected environment: a dark floor, a lit ceiling and a bright band
 * between them. The band is the softbox - a horizontal stripe of light is
 * what a polished surface reflects in a studio, and it is what makes the
 * highlight read as chrome. Its height and width are in the reflected ray's
 * y, so it sits at the same latitude on every body.
 */
const float DMT_ENV_BAND_Y = 0.22;
const float DMT_ENV_BAND_W = 0.30;

/**
 * Shell banding frequency and depth. Three bands per unit of the banding
 * coordinate, and the dark ring between them takes the body down to 70%:
 * enough to read as a layer boundary, not enough to read as a stripe painted
 * on.
 */
const float DMT_BAND_FREQ = 3.0;
const float DMT_BAND_DEPTH = 0.22;

/**
 * The jewel material.
 *
 *   n     surface normal (world)
 *   rd    view ray direction, pointing INTO the surface
 *   hue   the body's palette coordinate
 *   band  a banding coordinate: an orbit trap, a shell index, a height -
 *         anything that varies smoothly across the body. 0 is fine.
 *   ao    occlusion 0..1
 *   thin  how translucent this point is, 0..1 (thin filigree 1, thick core 0)
 *   treb  the treble envelope, 0..1.5: sharpens the specular, lifts the rim
 *
 * Returns linear colour, HDR: a full rim on a full specular can reach about
 * 2.5. Callers that tone-map (noneuclid, morphogen) can take it as is; the
 * others sit inside their own exposure, which this was tuned against.
 */
vec3 dmtShade(vec3 n, vec3 rd, float hue, float band, float ao, float thin, float treb) {
    float ndv = clamp(dot(n, -rd), 0.0, 1.0);
    float grazing = 1.0 - ndv;
    vec3 key = normalize(DMT_KEY);
    vec3 fill = normalize(DMT_FILL);
    float dif = clamp(dot(n, key), 0.0, 1.0);
    float bnc = clamp(dot(n, fill), 0.0, 1.0);
    float back = clamp(dot(n, -key), 0.0, 1.0);

    // Nested shells: the band coordinate modulates both the hue and the
    // level, so a layer boundary is a colour change AND a dark seam.
    float ring = 0.5 + 0.5 * cos(band * DMT_TAU * DMT_BAND_FREQ);
    vec3 body = pal(hue + 0.10 * sin(band * DMT_TAU)) * (1.0 - DMT_BAND_DEPTH * ring);

    // The thin film. Its hue walks with incidence; its WEIGHT peaks at 45
    // degrees (ndv * grazing * 4 is 1 there and 0 at both extremes), so the
    // sheen is a band across the middle of every curve rather than a tint on
    // the whole body - which is how interference colours actually sit.
    vec3 film = pal(hue + 0.55 + DMT_FILM_SPAN * ndv) * (ndv * grazing * 4.0);

    // The dispersed rim. Three exponents, three widths.
    vec3 fres = pow(vec3(grazing), DMT_FRESNEL_POW);
    vec3 rimCol = mix(pal(hue + 0.33), vec3(1.0), 0.35);

    // The environment, reflected.
    vec3 r = reflect(rd, n);
    float envUp = smoothstep(-0.6, 0.8, r.y);
    vec3 env = mix(pal(hue + 0.62) * 0.15, pal(hue + 0.08) * 0.55, envUp);
    float softbox = pow(max(1.0 - abs(r.y - DMT_ENV_BAND_Y) / DMT_ENV_BAND_W, 0.0), 3.0);
    // The softbox is tinted, not white: a white band on a coloured body is a
    // sticker, a tinted one is a reflection of a lit room.
    env += mix(vec3(1.0), pal(hue + 0.15), 0.4) * softbox * 0.35;
    // The reflection only shows on the glancing half of the body: face-on a
    // dielectric reflects a few percent, at the edge nearly everything.
    float reflectW = 0.05 + 0.42 * grazing * grazing;

    // The specular lobe. Treble sharpens it rather than brightening it: a
    // tighter lobe reads as a harder material, and an exponent moves no area.
    float spec = pow(clamp(dot(r, key), 0.0, 1.0), 28.0 + 100.0 * treb);

    // The body carries the frame. Every term after it is an accent kept
    // small enough that their SUM on a fully grazing, fully lit pixel stays
    // near the body's own level: the first tuning had them adding to about
    // three, which whitened every satellite into a pearl.
    vec3 col = body * (0.22 + 0.85 * dif + 0.30 * bnc);
    col += film * 0.18;
    col += env * reflectW;
    col += rimCol * fres * (0.30 + 0.35 * treb);
    col += mix(vec3(1.0), pal(hue + 0.2), 0.3) * spec * (0.30 + 0.25 * treb);
    // Subsurface: light through the thin parts, from the far side of the key.
    col += pal(hue + 0.50) * back * thin * 0.40;
    return col * ao;
}

/**
 * The silhouette glow. `near` is the closest a MISSED ray came to the surface
 * (track min(d) in the march), so this is a true halo around the body rather
 * than a radial gradient pasted behind it. `tight` in inverse world units.
 */
vec3 dmtHalo(float near, float hue, float tight, float gain) {
    return pal(hue) * exp(-max(near, 0.0) * tight) * gain;
}

// ===========================================================================
//  the sky
// ===========================================================================

/**
 * The chrysanthemum: fold rounds, symmetry and rotation rate.
 *
 * Seven folds put the finest thread well under a pixel at the framing every
 * style in this family uses, and past that a fold only adds cost. Twelve-fold
 * symmetry is what the mandala accounts converge on; the rate is a tenth of
 * a turn a minute, slow enough to be noticed as having moved.
 */
#define DMT_CHRYS_FOLDS 7
const float DMT_CHRYS_SYM = 12.0;
const float DMT_CHRYS_SPIN = 0.011;

/**
 * The mandala, evaluated on a ray direction.
 *
 * `rdLocal` is the direction in a frame where +z is the view axis the mandala
 * should be centred on: pass the camera-relative direction, or the world one
 * for a style that looks down +z. The direction is taken to angular
 * coordinates (elevation from the axis, azimuth), the azimuth is folded into
 * DMT_CHRYS_SYM wedges, and the resulting plane is put through a
 * fold-rotate-scale loop with two orbit traps: closest approach to the origin
 * draws the KNOTS, closest approach to the x axis draws the THREADS between
 * them. Two traps because the fabric has two scales; summing the orbit
 * instead washes it to a fog, which was the lesson the hyperspace sky taught.
 *
 * `hue` places it on the palette, `energy` (0..1.5) lifts it. Levels are low
 * on purpose - this is a background - and it is never black: the floor keeps
 * a silent, geometry-free frame alive and oriented.
 */
vec3 dmtChrysanthemum(vec3 rdLocal, float hue, float energy) {
    vec3 d = normalize(rdLocal);
    float elev = acos(clamp(d.z, -1.0, 1.0));
    float az = atan(d.y, d.x) + uTime * DMT_CHRYS_SPIN * DMT_TAU;
    float seg = DMT_TAU / DMT_CHRYS_SYM;
    az = abs(mod(az, seg) - seg * 0.5);
    // Breathes with the passage: the petals open on a swell.
    vec2 q = vec2(cos(az), sin(az)) * elev * (2.2 - 0.35 * clamp(uSwell, 0.0, 1.5));

    float knot = 1e9;
    float thread = 1e9;
    float scale = 1.0;
    mat2 turn = rot2(0.37 + 0.08 * sin(uTime * 0.0071));
    for (int i = 0; i < DMT_CHRYS_FOLDS; i++) {
        q = abs(q) - vec2(0.62, 0.41);
        q = turn * q;
        q *= 1.32;
        scale *= 1.32;
        knot = min(knot, dot(q, q) / (scale * scale));
        thread = min(thread, abs(q.y) / scale);
    }
    float knots = exp(-knot * 22.0);
    float threads = exp(-thread * 60.0);
    float lift = 0.7 + 0.5 * clamp(energy, 0.0, 1.5);
    vec3 col = pal(hue + 0.08 * log(max(knot, 1e-5))) * knots * 0.16;
    col += pal(hue + 0.36) * threads * 0.09;
    // A dim radial wash so the mandala has a centre, plus the floor.
    col += pal(hue + 0.62) * (0.028 + 0.030 * exp(-elev * 2.5));
    return col * lift;
}

// ===========================================================================
//  life
// ===========================================================================

/**
 * A body's life envelope: 0 before it is born, easing to 1, holding, easing
 * back to 0 as it dissolves, on a clock that is its own.
 *
 * `seed` in 0..1 offsets the clock so no two bodies share a phase; `period`
 * is the whole cycle in seconds. The clock is uTime plus half the travel
 * phase, so a loud passage turns bodies over faster without ever stepping
 * one - both terms only advance.
 *
 * Smoothstep on both ends: the derivative is zero at birth and at death, so a
 * body scaled by this grows out of nothing and shrinks into nothing rather
 * than popping at either end.
 */
float dmtLife(float seed, float period) {
    float phase = fract((uTime + uFlowPhase * 0.5) / max(period, 0.1) + seed);
    return smoothstep(0.0, 0.24, phase) * (1.0 - smoothstep(0.68, 1.0, phase));
}

/** Where in its cycle a body is, 0..1, for colouring it by age. */
float dmtLifePhase(float seed, float period) {
    return fract((uTime + uFlowPhase * 0.5) / max(period, 0.1) + seed);
}

// ===========================================================================
//  the morph ring
// ===========================================================================

/** Bodies on the ring. */
#define DMT_MORPH_SHAPES 5.0

/**
 * One body that is continuously changing what it is: sphere, gem, torus,
 * box, octahedron, and round to the sphere again.
 *
 * `phase` walks the ring, one unit per full circuit; the fractional position
 * blends the two neighbouring primitives with mix(), which is a convex
 * combination of two 1-Lipschitz fields and so is itself 1-Lipschitz - every
 * intermediate shape is exactly as marchable as its neighbours. The gem is
 * the intersection of an octahedron and a sphere, taken with max(): the max
 * of two lower bounds is a lower bound, so it too is safe to march.
 *
 * All five are rounded by 6% of the radius. opRound is a constant offset, so
 * it is free, and it keeps the octahedron's points and the box's corners from
 * aliasing into fireflies on the rim.
 *
 * 1-Lipschitz in p; `r` scales every primitive exactly (no division).
 */
float dmtMorphBody(vec3 p, float r, float phase) {
    float t = fract(phase) * DMT_MORPH_SHAPES;
    float i = floor(t);
    float f = smoothstep(0.0, 1.0, fract(t));

    float sphere = sdSphere(p, r);
    float gem = max(sdOctahedron(p, r * 1.42), sdSphere(p, r * 1.02));
    float torus = sdTorus(p, vec2(r * 0.76, r * 0.34));
    float box = sdBox(p, vec3(r * 0.70));
    float octa = sdOctahedron(p, r * 1.30);

    float a = i < 0.5 ? sphere : (i < 1.5 ? gem : (i < 2.5 ? torus : (i < 3.5 ? box : octa)));
    float b = i < 0.5 ? gem : (i < 1.5 ? torus : (i < 2.5 ? box : (i < 3.5 ? octa : sphere)));
    return opRound(mix(a, b, f), r * 0.06);
}

// ===========================================================================
//  the satellite bank
// ===========================================================================

/** Compile-time ceiling on the bank. See lib_sdf3's RAYMARCH note. */
#define DMT_MAX_SATELLITES 6

/**
 * Cheap containment. A body of radius r never reaches past DMT_SAT_EXTENT_K r
 * from its centre: the octahedron's vertices are the furthest thing on the
 * ring, at 1.30 r, plus the 0.06 r rounding. Outside a ball of
 * DMT_SAT_BOUND_K r the body is never evaluated, and the distance returned is
 * the distance to that ball PLUS the slack between the ball and the extent.
 *
 * The slack is not decoration. A bound that returns exactly 0 on its own
 * surface passes the march's hit test, so every ray stopped on an invisible
 * sphere around each body and shaded the inside of it - the same failure
 * kifs_frag documents for its escape ball. Adding the slack keeps the value
 * at 0.34 r on the ball, well above any hit epsilon, and it is still a lower
 * bound on the true distance because nothing lives inside the slack.
 */
const float DMT_SAT_EXTENT_K = 1.36;
const float DMT_SAT_BOUND_K = 1.70;

/** Reported by dmtSatellites(): which body was nearest, as a 0..1 hue seed. */
float gDmtSatHue;
/** Reported by dmtSatellites(): that body's life, for fading its light in. */
float gDmtSatLife;
/**
 * Reported by dmtSatellites(): the hit's latitude in the body's OWN tumbling
 * frame, -1..1. A banding coordinate has to vary across the visible surface,
 * and the radius does not (it is 1 everywhere on a sphere); latitude does,
 * and because the frame tumbles with the body the bands turn with it.
 */
float gDmtSatBand;

/**
 * A bank of up to DMT_MAX_SATELLITES morphing bodies orbiting the origin,
 * each on its own tilted, precessing orbit, its own spin, its own point on
 * the morph ring and its own life. Bodies bud out of nothing, tumble past
 * each other on unrelated clocks and dissolve - the "many things living next
 * to each other" every reference insists on, as opposed to one object
 * turning.
 *
 *   count    how many are alive at all (a runtime budget; bodies past it are
 *            never evaluated - tie it to uSteps so Detail buys population)
 *   orbit    mean orbital radius, world units
 *   radius   body radius at full life
 *   period   life cycle in seconds
 *
 * Identity is by INDEX, not by uSpawnSeed: a spike re-rolling the seed would
 * swap every body for a different one in one frame, which is a pop. A spike
 * instead re-aims the whole bank through uMoveDir (the orbits' precession
 * leans toward it) and moves every body's morph target through uFormPhase.
 *
 * 1-Lipschitz: a rotation and a translation per body, an exact radius scale,
 * and a min() over bodies and their bounding balls.
 */
float dmtSatellites(vec3 p, float count, float orbit, float radius, float period) {
    float d = 1e9;
    gDmtSatHue = 0.0;
    gDmtSatLife = 0.0;
    gDmtSatBand = 0.0;
    float clock = uTime * 0.21 + uFlowPhase * 0.35;
    for (int i = 0; i < DMT_MAX_SATELLITES; i++) {
        if (float(i) >= count) break;
        float fi = float(i);
        float seed = hash11(fi * 7.31 + 2.17);
        float seed2 = hash11(fi * 3.79 + 9.41);
        float life = dmtLife(seed, period);
        // Dead bodies cost one hash and nothing else.
        if (life < 0.02) continue;

        // The orbit: a plane tilted by the seed, precessing slowly, leaning
        // toward the current travel direction so a spike swings the whole
        // constellation rather than restarting it.
        vec3 axis = normalize(vec3(sin(seed * DMT_TAU) + 0.6 * uMoveDir.x, 0.75 + 0.5 * seed2, cos(seed * DMT_TAU) + 0.6 * uMoveDir.y));
        float ang = clock * (0.35 + 0.65 * seed2) + seed * DMT_TAU;
        vec3 c = rotAxis(axis, ang) * vec3(orbit * (0.85 + 0.3 * seed), 0.0, 0.0);
        vec3 q = p - c;

        float r = radius * (0.7 + 0.5 * seed2) * life;
        float bound = length(q) - r * DMT_SAT_BOUND_K;
        if (bound > 0.0) {
            d = min(d, bound + r * (DMT_SAT_BOUND_K - DMT_SAT_EXTENT_K));
            continue;
        }
        // The body's own tumble, on a rate that is its own.
        q = rotAxis(vec3(seed2 - 0.5, 0.8, seed - 0.5), clock * (0.8 + 1.4 * seed)) * q;
        float body = dmtMorphBody(q, r, uFormPhase + seed);
        if (body < d) {
            d = body;
            gDmtSatHue = seed;
            gDmtSatLife = life;
            gDmtSatBand = clamp(q.y / max(r, 1e-4), -1.0, 1.0);
        }
    }
    return d;
}

// ---- marching the bank on its own --------------------------------------------
//
// A style whose own march is bounded - by an escape ball, a support sphere or
// a cull radius - cannot see a satellite outside that bound by folding the
// bank into its map(): the loop exits before it gets there. And a style whose
// map() carries a Lipschitz division, a dissolve clip or a volume integral
// would have to special-case the bank at every one of them.
//
// So the bank is marched ON ITS OWN. dmtMarchSatellites() sphere-traces the
// satellite field alone (a handful of length() calls per step - a miss on the
// containment ball is all most steps cost), returns the hit distance, and the
// style takes whichever of its own hit and the bank's is nearer. Occlusion
// between the two comes out right by construction, nothing about the style's
// own march changes, and the cost is bounded by DMT_SAT_MAX_STEPS cheap
// steps per pixel.

/** Compile-time ceiling on the bank's own march. See lib_sdf3's RAYMARCH note. */
#define DMT_SAT_MAX_STEPS 96

/**
 * Sphere-traces the bank alone from `ro` along `rd`, out to `tMax`, spending
 * at most `steps` iterations. Returns the hit distance, or -1 for a miss.
 * The bank is centred on the origin; pass `ro - centre` for a bank around
 * another point.
 *
 * The step is the full estimate: every term in dmtSatellites() is
 * 1-Lipschitz, so it cannot overestimate. The epsilon is the same slope-plus-
 * floor the surface styles use.
 */
float dmtMarchSatellites(vec3 ro, vec3 rd, float tMax, float steps, float count, float orbit, float radius, float period) {
    float t = 0.0;
    for (int i = 0; i < DMT_SAT_MAX_STEPS; i++) {
        if (float(i) >= steps) break;
        vec3 p = ro + rd * t;
        float d = dmtSatellites(p, count, orbit, radius, period);
        float eps = 0.0009 * t + 0.0004;
        if (d < eps) return t;
        t += d;
        if (t > tMax) break;
    }
    return -1.0;
}

/**
 * The colour of a satellite hit at `p` (bank-relative), seen along `rd`.
 *
 * Re-evaluates the bank at `p` to learn which body it was, takes a
 * tetrahedral normal off the bank field, and shades with dmtShade(): the hue
 * is `hueBase` spread by the body's seed, the banding is the body's own
 * latitude, it is thin while arriving or leaving, and it glows a little then
 * too so a birth reads as light before it reads as mass.
 */
vec3 dmtSatelliteColor(vec3 p, vec3 rd, float hueBase, float treb, float count, float orbit, float radius, float period) {
    dmtSatellites(p, count, orbit, radius, period);
    float hue = hueBase + 0.45 * gDmtSatHue;
    float life = gDmtSatLife;
    float band = gDmtSatBand * 0.5;
    float e = max(0.0012 * length(p), 0.0006);
    vec2 k = vec2(1.0, -1.0);
    vec3 n = normalize(k.xyy * dmtSatellites(p + k.xyy * e, count, orbit, radius, period)
                     + k.yyx * dmtSatellites(p + k.yyx * e, count, orbit, radius, period)
                     + k.yxy * dmtSatellites(p + k.yxy * e, count, orbit, radius, period)
                     + k.xxx * dmtSatellites(p + k.xxx * e, count, orbit, radius, period));
    vec3 col = dmtShade(n, rd, hue, band, 1.0, (1.0 - life) * 0.5, treb);
    return col + pal(hue + 0.45) * (1.0 - life) * 0.22;
}

/** How many satellites Detail buys: three at the floor of the march budget, the full bank at the top. */
float dmtSatelliteCount() {
    return mix(3.0, float(DMT_MAX_SATELLITES), clamp((uSteps - 64.0) / 64.0, 0.0, 1.0));
}

// ===========================================================================
//  the tunnel that bends in every direction
// ===========================================================================

/**
 * The flight path's lateral offset at depth z: two incommensurate sines per
 * axis (a tight weave inside a long sweep), plus a lean toward the current
 * travel direction. Because the lean is on uMoveDir, a spike banks the whole
 * tunnel ahead toward the new heading and the turn glides in on the CPU.
 *
 * The camera rides this path and the tunnel is warped onto it, so the tube
 * curves up, down, left and right ahead of the viewer and the view is always
 * along it - the wormhole of the references rather than a straight pipe.
 */
vec2 dmtTunnelPath(float z) {
    return vec2(1.4 * sin(z * 0.21) + 2.6 * sin(z * 0.043 + 1.3),
                1.2 * cos(z * 0.17 + 0.7) + 2.2 * cos(z * 0.031))
         + uMoveDir * z * 0.12;
}

/**
 * Upper bound on the path's slope, |d path / dz|, from the sum of each term's
 * amplitude times frequency (1.4*0.21 + 2.6*0.043 + 0.12 on x, the same
 * arithmetic on y). The warp below shears space by at most this much, so its
 * Jacobian norm is at most 1 + slope: divide a distance measured in the
 * straightened frame by DMT_TUNNEL_LIP before stepping.
 */
const float DMT_TUNNEL_LIP = 1.0 + 0.62;

/** Straightens the bent tunnel: a point in world space to the straight tube's frame. */
vec3 dmtTunnelWarp(vec3 p) {
    return vec3(p.xy - dmtTunnelPath(p.z), p.z);
}

/**
 * The camera basis on the path: columns are right, up and forward, with
 * forward along the path's tangent so the horizon banks through the turns.
 *
 *   dist   how far down the path the camera is (integrate it on uFlowPhase -
 *          never on uTime * rate, which teleports when the rate changes)
 *   roll   camera roll in radians
 *
 * The tangent is a central difference of the path, which is exact enough for
 * a frame: the path is smooth and the step is a hundredth of a unit. The
 * transpose takes a world direction back into this frame, which is what a
 * style hands dmtChrysanthemum() so the mandala sits on the flight axis.
 */
mat3 dmtFlightBasis(float dist, float roll) {
    vec2 ahead = dmtTunnelPath(dist + 0.01);
    vec2 behind = dmtTunnelPath(dist - 0.01);
    vec3 fwd = normalize(vec3((ahead - behind) * 50.0, 1.0));
    vec3 upRef = vec3(sin(roll), cos(roll), 0.0);
    vec3 right = normalize(cross(upRef, fwd));
    vec3 up = cross(fwd, right);
    return mat3(right, up, fwd);
}

/** The camera on the path, looking along it: origin and the ray through `uv` at `focal`. */
void dmtFlightRay(vec2 uv, float focal, float dist, float roll, out vec3 ro, out vec3 rd) {
    ro = vec3(dmtTunnelPath(dist), dist);
    rd = dmtFlightBasis(dist, roll) * normalize(vec3(uv, focal));
}
