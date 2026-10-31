#version 300 es
precision highp float;

// LIFE - the simulation pass. Two rules share one state texture:
//
//   uRule 0  continuous Lenia (Chakazul's formulation, reimplemented):
//              A' = clip(A + (1/T) * G(K*A), 0, 1)
//            K is a normalized ring kernel over radius fraction r in (0,1]:
//            up to three equal rings, ring i weighted uB[i], the core shape
//            mapped inside each ring:
//              core 0  bump4  exp(4 - 1/(r(1-r)))
//              core 1  quad4  (4r(1-r))^4
//              core 2  step   1 on [1/4, 3/4]
//            Growth maps neighbourhood density u to [-1, 1]:
//              growth 0  gaus   2 exp(-(u-m)^2 / 2s^2) - 1
//              growth 1  quad4  2 max(0, 1-(u-m)^2/(9 s^2))^4 - 1
//            One engine covers Lenia species, SmoothLife (step core) and,
//            in the limit, Conway - the species parameters ARE the style.
//
//   uRule 1  Gray-Scott reaction-diffusion on (u, v):
//              du = Du lap(u) - u v^2 + f (1 - u)
//              dv = Dv lap(v) + u v^2 - (f + k) v
//            9-point laplacian (0.2 orthogonal / 0.05 diagonal), the curated
//            (f, k) pair is the organism: mitosis, coral, labyrinth, worms.
//
// State: r = A or u, g = v, b = slow age trace for the render pass, a = the
// melody ink new matter was born with (0..1, the present pass makes it hue).
// Ink travels with growth: Lenia averages it over the kernel weighted by
// living matter, Gray-Scott over the four neighbours weighted by v.
//
// Audio enters as MATTER and as a breathing RULE (LifeScene steers both from
// the sound frame): the kick envelope drops reagent at the birth orbit, the
// melody buds where its pitch points, hats sprinkle spores; the snare and a
// surge widen the growth window, and the rule morphs between the style's
// presets on kick16 and hard cuts. uDt carries the activity rate (quiet
// music freezes the colonies), uShift a whole-texel drift on the sound clock
// (exact, so it never smears; the state texture wraps as a torus).

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uPrev;
uniform vec2 uRes;
uniform int uRule;         // 0 Lenia, 1 Gray-Scott
uniform float uDt;         // Lenia: 1/T. GS: integration step per pass.
uniform int uCore;         // Lenia kernel core, 0..2
uniform int uGrowth;       // Lenia growth map, 0..1
uniform float uMu;
uniform float uSigma;
uniform float uRadius;     // Lenia kernel radius, texels
uniform int uRings;        // 1..3
uniform vec3 uB;           // ring peak weights
uniform float uF;          // GS feed
uniform float uK;          // GS kill
uniform vec2 uDiff;        // GS diffusion (Du, Dv)
uniform float uAniso;      // 0 isotropic; >0 favours x growth (frost)
uniform float uSeed;       // 1 on reset, decays; scatters a starting culture
uniform float uSeedJitter; // seed lattice scale
uniform float uSeedSalt;   // moves the seed lattice so every re-seed scatters a new culture
uniform float uKick;       // kick lump this frame (one per kick, sized by its envelope peak)
uniform vec2 uKickPos;     // kick blob position, uv
uniform float uKickSize;   // kick blob radius scale (kick random)
uniform float uBud;        // melody bud strength this frame (note strike, glide trail)
uniform vec2 uBudPos;      // where the melody points, uv
uniform float uInk;        // melody ink stamped into new matter, 0..1
uniform float uSprinkle;   // hat spore strength
uniform vec2 uShift;       // whole-texel drift this pass, texels
uniform float uAgeRate;    // the activity rate: age slows with the solver
uniform float uTime;

// ---- the finger as a seed --------------------------------------------------
//
// Audio enters this world as matter; so does a finger. It seeds the SAME
// channel the beat kick seeds - A for Lenia, v for Gray-Scott - so what grows
// out from under a fingertip is the organism this species actually makes, not
// a decoration painted over one. See SceneTouch.kt for the packing (xy is
// y-up NDC, aspect NOT applied). LifeScene zeroes uTouchCount on every substep
// but the first, so a touch is integrated once per frame however many times
// the solver steps.
#define TOUCH_MAX_POINTS 5
/** Per finger: xy = position, z = strength 0..1, w = age in seconds. */
uniform vec4 uTouchPoints[TOUCH_MAX_POINTS];
/** Occupied slots, including ones still fading after release. 0 = nothing touched. */
uniform int uTouchCount;

const float TAU = 6.2831853;

vec2 hash2(vec2 q) {
    q = vec2(dot(q, vec2(127.1, 311.7)), dot(q, vec2(269.5, 183.3)));
    return fract(sin(q) * 43758.5453);
}

float kernelCore(float r) {
    if (uCore == 0) {
        float d = r * (1.0 - r);
        return d <= 0.0 ? 0.0 : exp(4.0 - 1.0 / d);
    }
    if (uCore == 1) {
        float d = 4.0 * r * (1.0 - r);
        float d2 = d * d;
        return d2 * d2;
    }
    return (r >= 0.25 && r <= 0.75) ? 1.0 : 0.0;
}

float growthMap(float u) {
    float d = u - uMu;
    if (uGrowth == 0) return 2.0 * exp(-d * d / (2.0 * uSigma * uSigma)) - 1.0;
    float t = max(0.0, 1.0 - d * d / (9.0 * uSigma * uSigma));
    float t2 = t * t;
    return 2.0 * t2 * t2 - 1.0;
}

void main() {
    vec2 texel = 1.0 / uRes;
    // Read upstream of the drift; the shift is whole texels, so every tap
    // still lands on a texel centre.
    vec2 uv = vUv - uShift * texel;
    vec4 c = clamp(texture(uPrev, uv), vec4(0.0), vec4(1.0));

    float newA;
    float newV = c.g;
    float ink = c.a;

    if (uRule == 0) {
        // Disc sampling: 6 radii x 12 angles, kernel weight from the ring
        // formula at each tap, ring phases staggered so no radial spoke
        // pattern imprints on the organisms. Enough resolution for the
        // 3-ring species; linear filtering widens each tap to a 2x2 mean.
        float sum = 0.0;
        float norm = 0.0;
        float inkSum = 0.0;
        float inkW = 0.0;
        float rings = float(uRings);
        for (int ri = 0; ri < 6; ri++) {
            float rf = (float(ri) + 0.5) / 6.0;
            float br = rf * rings;
            int idx = int(min(br, rings - 0.001));
            float w = kernelCore(fract(br)) * (idx == 0 ? uB.x : (idx == 1 ? uB.y : uB.z));
            if (w <= 0.0) continue;
            float rad = rf * uRadius;
            for (int ai = 0; ai < 12; ai++) {
                float a = TAU * (float(ai) + 0.41 * float(ri)) / 12.0;
                vec4 s = texture(uPrev, uv + rad * vec2(cos(a), sin(a)) * texel);
                sum += w * s.r;
                norm += w;
                inkSum += w * s.r * s.a;
                inkW += w * s.r;
            }
        }
        float u = sum / max(norm, 1e-5);
        newA = clamp(c.r + uDt * growthMap(u), 0.0, 1.0);
        if (inkW > 1e-4) ink = mix(ink, inkSum / inkW, 0.1);
    } else {
        float wx = 0.2 * (1.0 + uAniso);
        float wy = 0.2 * (1.0 - 0.7 * uAniso);
        vec4 sl = texture(uPrev, uv - vec2(texel.x, 0.0));
        vec4 sr = texture(uPrev, uv + vec2(texel.x, 0.0));
        vec4 sd = texture(uPrev, uv - vec2(0.0, texel.y));
        vec4 su = texture(uPrev, uv + vec2(0.0, texel.y));
        vec4 s1 = texture(uPrev, uv - texel);
        vec4 s2 = texture(uPrev, uv + texel);
        vec4 s3 = texture(uPrev, uv + vec2(texel.x, -texel.y));
        vec4 s4 = texture(uPrev, uv + vec2(-texel.x, texel.y));
        vec2 lap = wx * (sl.rg + sr.rg) + wy * (sd.rg + su.rg) + 0.05 * (s1.rg + s2.rg + s3.rg + s4.rg) -
            (2.0 * wx + 2.0 * wy + 0.2) * c.rg;
        float inkW = sl.g + sr.g + sd.g + su.g;
        if (inkW > 1e-3) ink = mix(ink, (sl.g * sl.a + sr.g * sr.a + sd.g * sd.a + su.g * su.a) / inkW, 0.08 * uDt);
        float uvv = c.r * c.g * c.g;
        newA = clamp(c.r + uDt * (uDiff.x * lap.x - uvv + uF * (1.0 - c.r)), 0.0, 1.0);
        newV = clamp(c.g + uDt * (uDiff.y * lap.y + uvv - (uF + uK) * c.g), 0.0, 1.0);
    }

    // -- seeding and audio matter ---------------------------------------
    // Positions below are in the destination frame (vUv): matter lands where
    // the beat or the note puts it on screen, after the drift. `fresh` is how
    // much new matter this texel got; it takes the melody's ink.
    float aspect = uRes.x / uRes.y;
    float fresh = 0.0;
    float seedInk = uInk;
    if (uSeed > 0.0) {
        vec2 cell = floor(vUv * uSeedJitter) + uSeedSalt;
        vec2 h = hash2(cell);
        seedInk = clamp(uInk + 0.5 * (h.y - 0.5), 0.0, 1.0);
        float inCell = smoothstep(0.7, 0.95, h.x) * uSeed;
        if (uRule == 0) {
            // Smooth blobs, not full cells: hard squares of A never evolve
            // into organisms, gaussian lumps do.
            vec2 centre = (cell - uSeedSalt + 0.2 + 0.6 * hash2(cell + 7.7)) / uSeedJitter;
            vec2 dpos = (vUv - centre) * vec2(aspect, 1.0) * uSeedJitter;
            float lump = inCell * h.y * exp(-dot(dpos, dpos) * 2.2);
            newA = max(newA, lump);
            fresh = max(fresh, lump);
        } else if (inCell > 0.0) {
            newV = max(newV, inCell * 0.9);
            fresh = max(fresh, inCell);
        }
    }
    if (uKick > 0.0) {
        vec2 dpos = (vUv - uKickPos) * vec2(aspect, 1.0) / max(uKickSize, 0.2);
        float blob = exp(-dot(dpos, dpos) * 900.0) * uKick;
        if (uRule == 0) {
            // Half strength: a saturated disc every beat floods a Lenia world
            // into one mass; a soft lump buds a new organism instead.
            newA = clamp(newA + blob * 0.5, 0.0, 1.0);
        } else {
            newV = clamp(newV + blob, 0.0, 1.0);
        }
        fresh = max(fresh, blob);
    }
    if (uBud > 0.0) {
        // The melody's bud: smaller and tighter than the kick's, so a run of
        // notes reads as a strand of spores following the line.
        vec2 dpos = (vUv - uBudPos) * vec2(aspect, 1.0);
        float bud = exp(-dot(dpos, dpos) * 1800.0) * uBud;
        if (uRule == 0) {
            newA = clamp(newA + bud * 0.45, 0.0, 1.0);
        } else {
            newV = clamp(newV + bud * 0.8, 0.0, 1.0);
        }
        fresh = max(fresh, bud);
    }
    if (uTouchCount > 0) {
        // MAX over the fingers, not a sum: the injected amount per texel is then
        // bounded by one finger's worth however many are down, so five fingers on
        // one spot cannot hand the solver a value it has to be clamped out of.
        // Both rules are conditionally stable only for bounded state - Lenia's A
        // and Gray-Scott's v both live in [0,1] - and an out-of-range seed does
        // not make a bigger organism, it makes a dead saturated disc.
        float seedTouch = 0.0;
        for (int i = 0; i < TOUCH_MAX_POINTS; i++) {
            if (i >= uTouchCount) break;
            vec4 t = uTouchPoints[i];
            if (t.z <= 0.0) continue;
            vec2 dpos = (vUv - (t.xy * 0.5 + 0.5)) * vec2(aspect, 1.0);
            seedTouch = max(seedTouch, t.z * exp(-dot(dpos, dpos) * 280.0));
        }
        if (uRule == 0) {
            // Same reasoning as the beat kick's half strength: a saturated disc
            // floods a Lenia world into one mass, a soft lump buds an organism.
            newA = clamp(newA + seedTouch * 0.45, 0.0, 1.0);
        } else {
            newV = clamp(newV + seedTouch * 0.85, 0.0, 1.0);
        }
        fresh = max(fresh, seedTouch);
    }
    if (uSprinkle > 0.0) {
        vec2 h = hash2(floor(vUv * uRes * 0.5) + fract(uTime) * 61.7);
        if (uRule == 0) {
            // Lenia metabolizes seeds slowly, so sprinkle sparsely and softly
            // or the hats read as static instead of as new spores.
            float sp = step(1.0 - uSprinkle * 0.0012, h.x);
            newA = max(newA, sp * 0.5);
            fresh = max(fresh, sp * 0.5);
        } else {
            float sp = step(1.0 - uSprinkle * 0.004, h.x);
            newV = max(newV, sp * 0.7);
            fresh = max(fresh, sp * 0.7);
        }
    }

    // Age trace: rises where matter lives, bleeds slowly; the render pass
    // shades history without a second history texture.
    float live = uRule == 0 ? newA : newV;
    float age = clamp(c.b + (live > 0.12 ? 0.02 : -0.006) * uAgeRate, 0.0, 1.0);
    // Freshly injected matter is born young and takes the melody's ink.
    age *= 1.0 - clamp(fresh * 1.5, 0.0, 1.0);
    ink = mix(ink, seedInk, clamp(fresh * 2.0, 0.0, 1.0));

    fragColor = vec4(newA, newV, age, ink);
}
