#version 300 es
precision highp float;

// The CYMATICS style: the standing-wave field of the sound, evaluated PER
// PIXEL and filling the whole screen. No plate, no camera, no black surround
// - the wave field IS the picture, edge to edge, and it flows because its
// modes keep moving.
//
// Two geometries, both driven by the same ringing modes:
//   uGeometry 0  a water dish: circular-membrane modes, J_m(beta*r)cos(m*a)
//                - concentric rings crossed by petals, the CymaScope look;
//   uGeometry 1  a Chladni plate: the square-plate formula, a nodal lattice.
//
// Eleven substyles (uStyle) recompose that one resonator into different
// APPARATUS: sand on a plate, a struck drumhead, a pendulum trace, a Faraday
// pool, a nacre shell, sunlight through ripples, an acoustic levitator, a
// room of standing modes, a ferrofluid, a Kundt tube. Each owns a domain
// warp, a height recomposition and a material signature - topology first,
// sheen second - while the audio, phase and palette plumbing stay shared.
//
// The nodal rendering - a narrow Gaussian on |h| for the bright filigree plus
// a wide one for its halo - follows the approach taken by Naadara, the MIT
// licensed open cymatics laboratory (see THIRD_PARTY_NOTICES). The modal
// field, the travelling-wave flow, the iridescent dispersion and the caustic
// shading are this app's own.
//
// SAFETY: a degenerate FLAT field (nothing ringing - Audio drive zeroed by a
// hostile preset, or every mode decayed out) must render near BLACK, never a
// bright wash. Two gates enforce it: uFieldLive arrives from the scene as the
// summed mode amplitude, and lineLive additionally requires a real gradient
// under the pixel before the nodal/halo Gaussians (whose widths divide by
// fwidth(h), and blow up on a constant field) are allowed to emit light.
//
// CLOCKS: this shader never sees a raw rate x wall-clock product. uTime is a
// scene clock wrapped at 200*pi seconds and only ever read as sin/cos of
// uTime times a TWO-DECIMAL constant, which is a whole number of turns per
// wrap; swirl, travel and the plate scroll arrive as integrated, wrapped
// phases so a Speed or Swirl change bends the motion instead of teleporting
// the field. CymaticsClockSafetyTest pins all of it.
//
// TOUCH: a finger is a second DRIVER on the plate, radiating at the plate's
// own dominant wavenumber from wherever it is held, so the nodal lines
// reorganize around it the way a real Chladni plate answers its exciter. It is
// the only input here that is not the music. With nothing touched
// (uTouchCount == 0) not one instruction of it runs.

in vec2 vUv;
out vec4 fragColor;

uniform vec2 uResolution;
uniform float uTime;
/** Family substyle: 0 original, 1..10 authored variants. */
uniform int uStyle;

/** (order a, order b, amplitude, phase) per ringing mode. */
uniform vec4 uModes[8];
uniform int uModeCount;

uniform float uGeometry;
uniform float uScale;
/** 1 / peak displacement, so the shading works in normalized height. */
uniform float uHeightNorm;
uniform float uLine;
uniform float uGlow;
uniform float uIridescence;
uniform float uCaustic;
/** 0 = bare filigree on dark cells .. 1 = the whole surface filled in. */
uniform float uFill;
/** Integrated whole-field rotation, radians, wrapped to one turn. */
uniform float uSwirlPhase;
/** Integrated travelling-wave phase, radians, wrapped to one turn. The dish
 *  multiplies it by an INTEGER per-mode harmonic, so the wrap is seamless. */
uniform float uTravelPhase;
/** Integrated plate scroll, in plate units, wrapped at 2.0 - an exact period
 *  of every cos(n*PI*x) term in the plate formula. */
uniform float uDriftShift;
/** Faraday droplets: xy centre in field units, z phase, w amplitude (0 = free
 *  slot). Spawned by CymaticsDrops on beats; amplitude decays to zero. */
uniform vec4 uDrops[6];
/** Summed rendered-mode amplitude, 0 silent .. 1 driven: the flat-field gate. */
uniform float uFieldLive;
uniform float uBaseHue;
uniform float uHueSpan;
uniform float uEnergy;
uniform float uTreble;
uniform float uBeat;
uniform float uExposure;

// ---- the finger as a driver ------------------------------------------------
//
// A Chladni plate has a driver bolted to it, and where you put the driver is
// what decides the figure. Everything above is the resonator's own answer to
// the music; these four uniforms are the second driver, the one the user's
// finger holds. See SceneTouch.kt for the packing; xy is y-up NDC with aspect
// NOT applied, the same convention the fragment styles read.
#define TOUCH_MAX_POINTS 5
/** Per finger: xy = position, z = strength 0..1, w = age in seconds. */
uniform vec4 uTouchPoints[TOUCH_MAX_POINTS];
/** Occupied slots, including ones still fading after release. 0 = nothing touched. */
uniform int uTouchCount;
/** Ripple wavenumber, radians per field unit: the plate's own dominant mode. */
uniform float uTouchK;
/** Integrated ripple phase, radians, wrapped at 2*pi - same clock discipline as uTravelPhase. */
uniform float uTouchPhase;

const float PI = 3.14159265359;

vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

/**
 * Three plane waves at 120 degrees: the interference lattice a ferrofluid's
 * spikes relax into. 1.0 exactly at the spike sites, smooth everywhere - no
 * sign() or pow-of-abs kinks, so nodal shading over it stays seam-free.
 * Mirrored by CymaticsMath.hexLattice.
 */
float hexLattice(vec2 p) {
    return (cos(dot(p, vec2(1.0, 0.0)))
        + cos(dot(p, vec2(-0.5, 0.8660254)))
        + cos(dot(p, vec2(-0.5, -0.8660254)))) / 3.0;
}

/**
 * The shared resonator remains the source of every variant. This transform is
 * the family-composition layer: it bends the same modal field into membranes,
 * shells, chambers and tubes without forking the audio or phase logic.
 */
vec2 styleCoordinates(vec2 p, vec2 uv) {
    if (uStyle == 2) { // Drumhead: the skin breathes, and dips when struck.
        p *= 0.96 + 0.05 * sin(uTime * 0.7) - 0.06 * clamp(uBeat, 0.0, 1.0);
    } else if (uStyle == 3) { // Harmonograph: two slowly precessing pendulums.
        p += 0.24 * vec2(sin(p.y * 0.72 + uTime * 0.31), cos(p.x * 0.66 - uTime * 0.27));
    } else if (uStyle == 4) { // Faraday: subharmonic surface buckling.
        p += 0.11 * vec2(sin(p.y * 2.1 - uTime * 0.8), sin(p.x * 1.8 + uTime * 0.72));
    } else if (uStyle == 5) { // Shell: DOUBLE-POLAR fold. abs() of the angle
        // mirrors the spiral about the x axis, so the atan branch cut on the
        // negative-x axis lands where both halves agree - the radial seam the
        // raw angle painted there is gone, and the fold is the shell's look.
        float r = length(p);
        float a = abs(atan(p.y, p.x));
        p = vec2(a * 0.92 + r * 0.62, r * 2.0 - 1.9 + 0.12 * sin(a * 5.0 + uTime * 0.2));
    } else if (uStyle == 6) { // Caustic sheet: refraction through a second layer.
        p += 0.16 * sin(p.yx * 1.65 + vec2(0.0, 1.7) + uTime * 0.26);
    } else if (uStyle == 7) { // Levitator: a tall pressure column, gently
        // swaying. Continuous everywhere - the old sign(p.y) mirror put a
        // fwidth blow-up (a permanent bright band) along y = 0.
        p = vec2(p.x + 0.05 * sin(p.y * 2.4 + uTime * 0.25), p.y * 1.15);
    } else if (uStyle == 8) { // Chamber: perspective toward a deep back wall.
        // 1.25 + uv.y * 0.52 spans 0.73..1.77 for uv.y in -1..1, so no guard
        // is needed (the old max(0.62, ...) could never bind).
        float z = 1.0 / (1.25 + uv.y * 0.52);
        p = vec2(p.x * z, p.y * 0.72 + 0.22 / z);
    } else if (uStyle == 9) { // Rosensweig: cells pull toward a hex-like lattice.
        p += 0.07 * vec2(sin(p.y * 3.4), sin(p.x * 3.4 + 2.094));
    } else if (uStyle == 10) { // Kundt tube: a long horizontal bore.
        p = vec2(p.x * 0.46, p.y * 1.35);
    }
    return p;
}

/**
 * Bessel J_m, cheap: the asymptotic form (amplitude ~ sqrt(2/pi x), zeros a
 * quarter period apart) with a core factor so an angular order m > 0 vanishes
 * at the centre of the dish as the real function does. Mirrored by
 * CymaticsMath.besselApprox, which is pinned against a series expansion.
 */
float besselApprox(float m, float x, float phase) {
    // J_0 peaks AT the centre, every higher order vanishes there: without the
    // step the core factor punched a black hole through the middle of the
    // dish for every mode alike.
    float ax = abs(x);
    float core = mix(1.0, ax * ax / (ax * ax + 0.45 * m * m + 0.05), step(0.5, m));
    float w = x - m * PI * 0.5 - PI * 0.25 - phase;
    // Two terms of Hankel's expansion, not one: the leading cosine alone puts
    // the INNERMOST rings badly out (J_4's first zero at 7.85 instead of
    // 7.59), and those are the rings filling the middle of the screen. With
    // both terms every ring lands within ~0.05 of the real J_m's zero.
    // Clamped only to bound the terms near the centre, where they diverge and
    // the core factor above owns the shape anyway.
    float inv = 1.0 / (8.0 * max(ax, 0.75));
    float mu = 4.0 * m * m;
    float c1 = clamp((mu - 1.0) * inv, -3.0, 3.0);
    float c0 = clamp(1.0 - (mu - 1.0) * (mu - 9.0) * inv * inv * 0.5, -3.0, 3.0);
    return (c0 * cos(w) - c1 * sin(w)) * inversesqrt(1.0 + 2.0 * ax) * core * 1.7;
}

/** Reach of one finger's excitation, as a fraction of the half-screen. */
#define TOUCH_REACH 0.45
/** Peak displacement a finger adds, against a modal field normalized to +-1. */
#define TOUCH_DRIVE 0.6

/**
 * The displacement a finger drives into the plate, and how live it makes it.
 *
 * A point driver on a plate radiates at the plate's OWN wavenumber - that is
 * why a Chladni figure reorganizes around the driver instead of acquiring an
 * unrelated pattern next to it - so the ripple frequency is uTouchK, which the
 * scene sends as pi times the loudest ringing mode's wavenumber. The phase
 * runs outward (cos(k*d - phase) with phase increasing), which is what a
 * driven plate does: it sheds rings.
 *
 * The finger is mapped through the SAME transform the pixel took, style warp
 * included, so it lands under the fingertip on all eleven substyles rather
 * than under where the fingertip would be on the unwarped plate.
 *
 * .x is the displacement, clamped to +-1 BEFORE the gain: five fingers in one
 * spot would otherwise sum to five, swamp the modal field, and turn the whole
 * screen into one saturated ridge after the tone map. Clamping the sum rather
 * than each term keeps one finger at full strength and makes five read as one
 * hard push, which is what a hand pressed on a plate actually does.
 *
 * .y is the strongest finger's strength, which becomes a floor under the
 * flat-field gate: touching a silent plate has to draw something, and a driven
 * ripple is a genuinely non-constant field, so the gate's reason to be closed
 * (a constant h, where the nodal Gaussians divide by ~0 and wash the screen)
 * does not apply under a finger.
 */
vec2 touchDrive(vec2 p, float aspect, mat2 spin) {
    float h = 0.0;
    float live = 0.0;
    float reach = TOUCH_REACH * uScale;
    for (int i = 0; i < TOUCH_MAX_POINTS; i++) {
        if (i >= uTouchCount) break;
        vec4 t = uTouchPoints[i];
        if (t.z <= 0.0) continue;
        vec2 ndc = vec2(t.x * aspect, t.y);
        vec2 c = styleCoordinates(spin * ndc * uScale, ndc);
        float d = length(p - c);
        h += t.z * cos(uTouchK * d - uTouchPhase) * exp(-(d * d) / (reach * reach));
        live = max(live, t.z);
    }
    return vec2(clamp(h, -1.0, 1.0), live);
}

/** The dish/plate displacement at [p], as the sum of every ringing mode. */
float field(vec2 p) {
    float h = 0.0;
    if (uGeometry < 0.5) {
        float r = length(p);
        float a = atan(p.y, p.x);
        for (int i = 0; i < 8; i++) {
            if (i >= uModeCount) break;
            vec4 M = uModes[i];
            // Angular order and radial order, out of the same (n, m) pair the
            // square plate reads as its own two orders.
            float ang = M.y;
            float rad = max(M.x - M.y, 1.0);
            // McMahon: the s-th zero of J_m sits near pi(s + m/2 - 1/4).
            float beta = PI * (rad + 0.5 * ang - 0.25);
            // "Flow" turns the standing wave into a travelling one: the rings
            // march outward the way a driven dish sheds them, faster for the
            // finer modes. The rate ladder is quantized to INTEGER multiples
            // of the accumulated phase so its 2*pi wrap is a whole number of
            // cycles for every mode - a fractional ladder would pop the rings
            // once per wrap. Applied as a PHASE, not as a shift of the radius
            // - shifting the radius pushed the argument negative near the
            // centre and the amplitude term returned NaN, which showed up as
            // a black hole punched through the middle of the dish.
            float travel = uTravelPhase * max(1.0, floor(0.7 + 0.09 * beta + 0.5));
            h += M.z * besselApprox(ang, beta * r, travel) * cos(ang * a + M.w);
        }
    } else {
        for (int i = 0; i < 8; i++) {
            if (i >= uModeCount) break;
            vec4 M = uModes[i];
            float n = M.x;
            float m = M.y;
            // The plate's own flow is a slow drift of the lattice, so the
            // figure breathes instead of standing frozen. uDriftShift wraps
            // at 2.0, an exact period of both cosines below.
            vec2 q = p + vec2(0.0, uDriftShift);
            float z = cos(n * PI * q.x) * cos(m * PI * q.y) - cos(m * PI * q.x) * cos(n * PI * q.y);
            h += M.z * z * cos(M.w);
        }
    }
    return h;
}

void main() {
    // Screen -> field coordinates. The field CONTINUES past the edges of the
    // screen: there is no rim to frame, so nothing is ever letterboxed.
    vec2 uv = vUv * 2.0 - 1.0;
    float aspect = uResolution.x / max(uResolution.y, 1.0);
    uv.x *= aspect;
    // Whole-field rotation from an INTEGRATED phase: scrubbing Swirl or Speed
    // (a preset fade, an LFO) changes how fast the field turns from here on,
    // never where it currently points.
    float sw = sin(uSwirlPhase);
    float cw = cos(uSwirlPhase);
    mat2 spin = mat2(cw, -sw, sw, cw);
    vec2 p = spin * uv * uScale;
    p = styleCoordinates(p, uv);

    // Normalized displacement: -1..1 whatever is playing, so every threshold
    // below means the same thing at any loudness. Variants recompose samples
    // of THE SAME resonator - a compositional merge, never a second audio
    // engine running out of phase with the first.
    float h = field(p) * uHeightNorm;
    float hexCell = 0.0; // Rosensweig's spike lattice, reused by its material.
    float caustLap = 0.0; // Caustic sheet's curvature probe.
    if (uStyle == 2) {
        // Drumhead: the membrane is CLAMPED at its rim - displacement pinned
        // to zero outside the head, the way a real skin is.
        h *= smoothstep(1.04, 0.88, length(uv));
    } else if (uStyle == 3) {
        // Harmonograph: a second, slowly precessing sample of the same field,
        // superposed - two pendulums drawing over each other.
        float a2 = 0.74 + 0.18 * sin(uTime * 0.09);
        mat2 r2 = mat2(cos(a2), -sin(a2), sin(a2), cos(a2));
        h = h * 0.58 + field(r2 * p * 0.82 + vec2(0.35, -0.18)) * uHeightNorm * 0.42;
    } else if (uStyle == 4) {
        // Faraday: subharmonic folding, then a high-frequency capillary
        // lattice riding the treble, then beat-spawned droplet rings.
        float sub = field(p * 1.62 + vec2(0.16, -0.11)) * uHeightNorm;
        h = sin(h * 2.45 + sub * 1.15) * 0.72;
        h += sin(p.x * 21.0 + uTravelPhase * 3.0) * sin(p.y * 21.0 - uTravelPhase * 3.0)
            * 0.16 * clamp(uTreble, 0.0, 1.5);
        for (int i = 0; i < 6; i++) {
            float amp = uDrops[i].w;
            if (amp < 0.003) continue;
            float d = length(p - uDrops[i].xy);
            h += amp * sin(9.0 * d - uDrops[i].z) * exp(-2.2 * d);
        }
        h *= 0.85;
    } else if (uStyle == 5) {
        // Shell: the mirrored second polar reading of the same modes.
        h = h * 0.72 + field(p.yx * vec2(-0.68, 0.68)) * uHeightNorm * 0.28;
    } else if (uStyle == 6) {
        // Caustic sheet: probe the surface's CURVATURE (a five-point
        // Laplacian over pixel-scale offsets). Light through a rippled
        // surface piles up where curvature focuses the rays - that
        // convergence, not the height itself, is what draws the web.
        vec2 ex = dFdx(p) * 1.5;
        vec2 ey = dFdy(p) * 1.5;
        caustLap = (field(p + ex) + field(p - ex) + field(p + ey) + field(p - ey)) * uHeightNorm - 4.0 * h;
    } else if (uStyle == 8) {
        // Standing chamber: room modes - PRODUCT cosines, cos(n pi x) *
        // cos(m pi y), whose figure is a grid of rectangular pressure cells,
        // not the plate's diagonal filigree (that is the difference formula).
        // uDriftShift slides the cell pattern along the room; its 2.0 wrap is
        // an exact period of every term.
        float room = 0.0;
        for (int i = 0; i < 4; i++) {
            if (i >= uModeCount) break;
            vec4 M = uModes[i];
            room += M.z * cos(M.x * PI * (p.x + uDriftShift)) * cos(M.y * PI * p.y) * cos(M.w);
        }
        h = room * uHeightNorm * 1.6;
    } else if (uStyle == 9) {
        // Rosensweig: the field's SMOOTHED square (h*h saturating - no kink
        // at h = 0, unlike the old signed pow(|h|, 0.48), whose infinite
        // derivative lit every nodal line as a seam) budgets a lattice of
        // hex-packed spikes, the instability's own geometry.
        float pool = h * h;
        pool = pool / (pool + 0.22);
        hexCell = hexLattice(p * 5.2);
        h = pool * (0.25 + 1.15 * pow(max(hexCell, 0.0), 2.0)) - 0.12;
    } else if (uStyle == 10) {
        // Kundt tube: the same resonance squeezed into a long bore - a 10:1
        // anisotropic resample makes the nodal geometry near-one-dimensional,
        // so striations stand at half-wavelength spacing along the axis.
        h = h * 0.3 + field(vec2(p.x * 2.1, p.y * 0.22)) * uHeightNorm * 0.7;
    }

    // The finger's driver rides ON TOP of the substyle recomposition, not
    // under it: styles 8 and 9 ASSIGN h rather than adding to it (the room
    // modes and the Rosensweig pool are their own fields), so a driver added
    // before this chain would be silently deleted on two of the eleven. Added
    // here it means the same thing everywhere, which is what lets one gesture
    // be described to a user once.
    float live = uFieldLive;
    if (uTouchCount > 0) {
        vec2 driven = touchDrive(p, aspect, spin);
        h += driven.x * TOUCH_DRIVE;
        live = max(live, driven.y);
    }
    float az = abs(h);

    vec2 g = vec2(dFdx(h), dFdy(h));
    float wRaw = fwidth(h);
    float w = max(wRaw, 1e-5);

    // FLAT-FIELD SAFETY GATE. The nodal/halo Gaussians divide by w: on a
    // degenerate flat field (h identically 0 - Audio drive zeroed by a raw
    // preset value, every mode decayed out) az and wRaw are both 0 and the
    // 1e-5 floor made nodal = halo = 1 across the WHOLE screen - a ~74%
    // bright wash after tone mapping, exactly the photosensitivity failure
    // VisualSafety exists to prevent, arriving from below it. The gate
    // demands a real gradient under the pixel (wRaw) AND a live plate before
    // any line light is emitted; the fill/spec/material layers are gated by
    // liveness alone, so a flat field renders near black rather than washing
    // out. `live` is uFieldLive raised by the finger, because a driven ripple
    // is the one thing that makes the field non-constant without the music:
    // the gate is closed on a CONSTANT h, and a touched plate never has one.
    float lineLive = live * smoothstep(2.0e-5, 1.2e-4, wRaw);

    // Nodal filigree and its halo: the sand of a plate, the standing ridges
    // of a dish. Both widths are measured in local slope, so a line keeps the
    // same weight on screen whether the figure is coarse or dense.
    float narrow = w * (0.6 + 1.3 * uLine);
    float wide = narrow * 4.0;
    float nodal = exp(-(az * az) / (narrow * narrow)) * lineLive;
    float halo = exp(-(az * az) / (wide * wide)) * lineLive;

    // Caustic sheen: light through a wavy surface piles up where the surface
    // is flat, which is what gives water cymatics its glassy plateaus.
    // ("flat" is a reserved interpolation qualifier in GLSL ES 3.00.)
    float plateau = clamp(1.0 - length(g) / w, 0.0, 1.0);
    float caustic = pow(plateau, 4.0) * uCaustic * smoothstep(0.15, 0.6, az);

    // Iridescence: the palette is sampled at three slightly different heights
    // for R/G/B, so slopes fringe into rainbow exactly as a thin film does.
    float disp = uIridescence * (0.02 + 0.05 * clamp(length(g) / w, 0.0, 1.0));
    // Hue tracks displacement WITHOUT being squeezed into one turn of the
    // wheel: a wide palette span therefore bands the field into repeating
    // colour rings (hue is cyclic, so the wrap is seamless) instead of
    // painting the whole figure one tint with a slight gradient. uBaseHue
    // already carries the substyle's own offset and the chroma nudge, so
    // every variant sits at its own point on the user's palette.
    float hue = uBaseHue + uHueSpan * h;
    vec3 body =
        vec3(
            hsv2rgb(vec3(fract(hue - disp), 0.9, 1.0)).r,
            hsv2rgb(vec3(fract(hue), 0.9, 1.0)).g,
            hsv2rgb(vec3(fract(hue + disp), 0.9, 1.0)).b
        );

    // Relief: a surface normal from the slope DIRECTION only (the magnitude
    // is divided out), so the field embosses the same whether the figure is
    // three rings wide or thirty, and a light can play across it.
    vec3 nrm = normalize(vec3(-g / w, 0.9));
    vec3 lightDir = normalize(vec3(0.35, 0.55, 0.75));
    float diffuse = clamp(dot(nrm, lightDir), 0.0, 1.0);
    float spec = pow(clamp(dot(reflect(-lightDir, nrm), vec3(0.0, 0.0, 1.0)), 0.0, 1.0), 22.0);

    // The surface itself. "Fill" runs from bare filigree over dark cells (the
    // sand-on-a-plate reading) to a fully filled iridescent surface (the
    // liquid reading); level keeps both honest to how loud the track is, and
    // `live` keeps a silent, untouched field from wearing the filled surface.
    float level = 0.35 + 0.75 * clamp(uEnergy, 0.0, 1.5);
    vec3 color = body * (0.04 + 0.20 * az * az + uFill * (0.10 + 0.80 * diffuse) * live) * level;

    // Halo (broad, palette-coloured), then the filigree on top (near white,
    // treble glinting on it and beats flaring it), then the caustic sheen.
    color += body * halo * uGlow * 0.45 * level;
    vec3 ridge = mix(vec3(1.0), body, 0.4);
    color += ridge * nodal * (0.7 + 0.45 * clamp(uTreble, 0.0, 1.5) + 0.35 * clamp(uBeat, 0.0, 1.0));
    color += ridge * (caustic + spec * uCaustic * 0.8) * (0.2 + 0.3 * clamp(uEnergy, 0.0, 1.5)) * live;

    // Material signatures: each substyle's own apparatus, painted out of the
    // same h, derivatives and palette so the family controls keep one meaning
    // everywhere. Every additive layer here is gated by lineLive/live
    // (or an az-window that is closed on a flat field), so no substyle can
    // reopen the flat-field wash.
    if (uStyle == 1) { // Chladni Sand: grains GATHER on the nodal lines.
        // exp(-|h|) concentrates the grains where the plate is still; the
        // music shakes them - jitter grows with local |h| and with level -
        // so loud passages scatter the figure and quiet ones let it settle.
        float shake = az * (2.0 + 9.0 * clamp(uEnergy, 0.0, 1.5));
        vec2 gp = gl_FragCoord.xy * 0.61
            + shake * vec2(sin(uTime * 2.3 + p.y * 21.0), cos(uTime * 1.9 + p.x * 19.0));
        float grain = smoothstep(0.55, 0.97, hash21(floor(gp)));
        float gather = exp(-az * 6.5);
        vec3 gold = mix(body, vec3(1.0, 0.84, 0.5), 0.72); // gold filigree
        color *= 0.34; // bare dark plate under the sand
        color += gold * grain * gather * lineLive * (0.5 + 0.8 * halo + 1.2 * nodal);
    } else if (uStyle == 2) { // Drumhead: matte struck skin inside a hard rim.
        float rr = length(uv);
        float rim = 1.0 - smoothstep(0.0, 0.05, abs(rr - 0.93));
        float lum = dot(color, vec3(0.299, 0.587, 0.114));
        color = mix(color, lum * vec3(1.05, 0.97, 0.85), 0.4); // vellum, not lacquer
        color *= 1.0 - smoothstep(0.86, 1.0, rr); // nothing past the shell
        color += ridge * rim * (0.4 + 0.7 * clamp(uBeat, 0.0, 1.0)) * live;
    } else if (uStyle == 3) { // Harmonograph: pendulum ink etched on dim paper.
        float etched = exp(-abs(sin(h * 15.0 + p.x * 1.2 - p.y * 0.7)) * 8.0);
        color *= 0.6;
        color += body * etched * live * (0.28 + 0.5 * uGlow);
    } else if (uStyle == 4) { // Faraday: subharmonic cells + capillary glint.
        float cells = pow(clamp(1.0 - az, 0.0, 1.0), 5.0);
        color += body * cells * live * (0.18 + 0.45 * uCaustic) * (0.5 + 0.5 * sin(uTime * 0.5 + h * 8.0));
        color += ridge * nodal * clamp(uTreble, 0.0, 1.5) * 0.35;
    } else if (uStyle == 5) { // Harmonic Shell: nacre fan with growth bands.
        float shellRim = pow(clamp(1.0 - dot(uv * 0.72, uv * 0.72), 0.0, 1.0), 0.36);
        float pearl = pow(clamp(dot(nrm, normalize(vec3(-0.25, 0.45, 0.86))), 0.0, 1.0), 9.0);
        float growth = exp(-abs(sin(p.y * 6.0 + h * 2.0)) * 5.0);
        color = color * (0.45 + 0.75 * shellRim)
            + ridge * pearl * 0.55 * live
            + body * growth * 0.16 * live;
    } else if (uStyle == 6) { // Caustic Sheet: sunlight folded through ripples.
        // Convergence: rays pile up where 1 + k * curvature collapses toward
        // zero - the fold lines of the refracted light, i.e. real caustics.
        float bend = caustLap / max(w, 1e-5);
        float web = clamp(1.0 / max(abs(1.0 + 2.6 * bend), 0.28) - 0.85, 0.0, 2.4);
        vec3 sunlit = mix(mix(body, vec3(0.6, 0.92, 1.0), 0.7), vec3(1.0), 0.45); // cyan-white
        color *= 0.5; // deep water
        color += sunlit * web * live * (0.4 + 0.9 * uCaustic);
    } else if (uStyle == 7) { // Levitator: droplets pinned at the antinode shelves.
        // A jittered lattice of beads, each held inside its own cell (jitter
        // + wobble + radius < half a cell, so nothing pops at cell borders),
        // lit only near antinodes (az high) - the levitation picture: matter
        // held where the pressure swing is strongest. Beads ride h, so bass
        // makes the whole stack bounce.
        vec2 bp = vec2(p.x * 3.4, (p.y + h * 0.22) * 4.6);
        vec2 cellId = floor(bp);
        float hc = hash21(cellId);
        vec2 centre = vec2(0.5) + (vec2(hc, fract(hc * 7.31)) - 0.5) * 0.16
            + 0.06 * clamp(uBeat, 0.0, 1.0) * vec2(sin(uTime * 1.4 + hc * 44.0), cos(uTime * 1.1 + hc * 61.0));
        float bead = smoothstep(0.30, 0.14, length(fract(bp) - centre));
        float antinode = smoothstep(0.3, 0.75, az);
        color *= 0.4; // dark chamber
        color += ridge * bead * antinode * live * (0.8 + 0.5 * clamp(uTreble, 0.0, 1.5));
        color += body * halo * 0.3;
    } else if (uStyle == 8) { // Standing Chamber: pressure cells in a wire room.
        // The room itself is FIXED architecture (a static grid); the music
        // lives in which cells glow. Depth fade keeps the perspective floor.
        float gx = abs(fract(p.x * 0.7 + 0.5) - 0.5);
        float gy = abs(fract(p.y * 0.7 + 0.5) - 0.5);
        float frame = exp(-min(gx, gy) * 26.0);
        float depthFade = 1.0 - smoothstep(-0.85, 1.35, uv.y);
        color *= 0.42 + 0.58 * depthFade;
        color += body * frame * live * (0.3 + 0.6 * clamp(uEnergy, 0.0, 1.5));
        color += ridge * nodal * 0.3;
    } else if (uStyle == 9) { // Rosensweig: black gloss, spike tips catching light.
        float tips = pow(max(hexCell, 0.0), 6.0) * smoothstep(0.1, 0.55, az);
        vec3 steel = mix(body, vec3(0.85, 0.9, 1.0), 0.6);
        color *= 0.3; // the fluid body is near-black
        color += steel * (spec * spec * 2.2 + tips * (0.9 + 0.8 * clamp(uBeat, 0.0, 1.0))) * live;
        color += body * halo * 0.18;
    } else if (uStyle == 10) { // Kundt Tube: dust bands inside a glass bore.
        // Dust piles where the air is still (|h| small) - half-wavelength
        // striations that RE-SPACE themselves as the dominant mode slides.
        float piles = exp(-az * az * 30.0);
        float striae = smoothstep(0.35, 0.9, hash21(floor(gl_FragCoord.xy * vec2(0.16, 1.1))));
        float bore = 1.0 - smoothstep(0.55, 0.8, abs(uv.y));
        float wallGlint = 1.0 - smoothstep(0.0, 0.07, abs(abs(uv.y) - 0.62));
        vec3 dust = mix(body, vec3(1.0, 0.94, 0.8), 0.55); // cork dust
        color *= bore; // dark outside the tube
        color += dust * piles * (0.35 + 0.65 * striae) * live * bore * (0.6 + 0.5 * clamp(uEnergy, 0.0, 1.5));
        color += ridge * wallGlint * 0.35 * live;
    }

    // Filmic-ish roll-off: the sum above is HDR by construction (three
    // additive layers), and clipping it would flatten every bright ridge into
    // the same white blob.
    color = vec3(1.0) - exp(-color * uExposure);
    fragColor = vec4(color, 1.0);
}
