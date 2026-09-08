#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_touch
//#include lib_dmt

// Curl Bloom: a host body, raymarched, that is continuously changing what it
// IS - sphere, gem, torus, box, octahedron and round again - while the space
// it sits in is being stirred by an incompressible flow, so the surface is
// always folding through itself. Around it, a bank of smaller bodies of the
// same kind, each on its own orbit and its own clock: they bud out of
// nothing, tumble past the host and each other, and dissolve. That bank is
// what turns one object turning into a place with things living in it, which
// is the difference the reference material insists on.
//
// This is the 3D counterpart to the 2D fluid in the other new styles: the same
// idea - advect the domain through a divergence-free field - but applied to
// the domain of a distance function instead of to a texture lookup, so the
// stirring deforms an actual solid rather than smearing a picture of one.
//
// ---- the morph -------------------------------------------------------------
//
// The primitives sit on lib_dmt's closed ring and uFormPhase walks it,
// blending each neighbouring pair with mix(). That is a legitimate distance
// field and not an approximation: mix(a, b, t) of two 1-Lipschitz functions is
// 1-Lipschitz for any t in 0..1, because it is a convex combination. So the
// morph costs the march nothing - no step scale, no correction - and every
// intermediate shape is as marchable as the two it lies between. uFormPhase
// glides rather than jumps, so there is no value of it at which the body is
// discontinuous. A spike chooses the next plateau; the body takes most of a
// second to get there. The satellites walk the same ring, each offset by its
// own seed, so no two bodies are the same shape at the same moment.
//
// ---- the bank --------------------------------------------------------------
//
// dmtSatellites() lives OUTSIDE the stir. The host is deformed by the flow;
// the satellites orbit it in undeformed space, so they are hard, clean jewels
// against a body that is being folded, and the estimate for them needs no
// Lipschitz correction at all. The min() of the two fields divides only the
// host's term by the warp bound, which the step does below.
//
// ---- audio ------------------------------------------------------------------
//
// uSwell inflates the body, uBassSmooth deepens the stir, uTrebleSmooth
// sharpens the rim light. A spike picks the next primitive (uFormPhase), re-aims
// the flow (uMoveDir) and re-seeds the surface veining (uSpawnSeed). Nothing
// keys brightness off a raw envelope, so nothing here can flash.

#define CB_MAX_STEPS 128
#define CB_FAR 9.0
#define CB_FOCAL 1.5
#define CB_TAU 6.2831853

/** The bank: orbital radius, body radius at full life and the life cycle. */
#define CB_SAT_ORBIT 1.75
#define CB_SAT_RADIUS 0.26
#define CB_SAT_PERIOD 13.0

// The stir. Kept as globals because map() is called from the march, from the
// normal taps and from the shadow loop, and all of them must see one shape.
float gWarpScale;
float gWarpAmount;
float gLip;
float gRadius;
float gMorph;

// The host, in stirred space: lib_dmt's ring, walked by gMorph.
float body(vec3 p) {
    return dmtMorphBody(p, gRadius, gMorph);
}

// The scene: the body, seen through the stir.
//
// The warp is applied to the SAMPLE POINT, so what the march measures is the
// distance in warped space. That is an overestimate of the true distance in
// world space by at most the warp's Lipschitz bound, which is what gLip
// divides out at the step.
/** Which field won the last map(): 0 the host, 1 a satellite. Read after the march, before the normal. */
float gHitSat;
float gSatCount;

float map(vec3 p) {
    vec3 q = fluidWarp3(p, gWarpScale, gWarpAmount);
    // The host's estimate was measured in warped space; dividing it by the
    // warp bound here (rather than at the step) lets it sit in one min() with
    // the satellites, which need no correction.
    float host = body(q) / gLip;
    float sat = dmtSatellites(p, gSatCount, CB_SAT_ORBIT, CB_SAT_RADIUS, CB_SAT_PERIOD);
    gHitSat = sat < host ? 1.0 : 0.0;
    return min(host, sat);
}

vec3 normalAt(vec3 p, float e) {
    // The tetrahedron 4-tap: four map() calls rather than the six a central
    // difference costs.
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

// Cheap ambient occlusion: five taps along the normal, each asking how much
// nearer the surface is than it would be on a flat plane. This is what makes
// the folds of the stir read as depth rather than as a paint job.
float occlusion(vec3 p, vec3 n) {
    float occ = 0.0;
    float w = 1.0;
    for (int i = 1; i <= 5; i++) {
        float h = 0.035 * float(i);
        occ += (h - map(p + n * h)) * w;
        w *= 0.62;
    }
    return clamp(1.0 - 2.2 * occ, 0.0, 1.0);
}

void main() {
    vec2 uv = view();

    float bass = clamp(uBassSmooth, 0.0, 1.5);
    float treb = clamp(uTrebleSmooth, 0.0, 1.5);
    float swell = clamp(uSwell, 0.0, 1.5);
    float finger = touchFalloff(uv, 0.6);

    // The stir. Amount is kept modest deliberately: the Lipschitz bound below
    // is what pays for it, and a large amount makes every ray take tiny steps
    // and the body dissolve into banding at low Detail.
    gWarpScale = 1.15 + 0.25 * swell;
    gWarpAmount = 0.10 + 0.11 * bass + 0.05 * finger;
    gLip = fluidWarp3Lipschitz(gWarpScale, gWarpAmount);
    gRadius = 0.86 * (1.0 + 0.07 * swell);
    // Glides; never steps. See the morph note at the top.
    gMorph = uFormPhase;
    // Detail buys population: three satellites at the floor, six at the top.
    gSatCount = mix(3.0, float(DMT_MAX_SATELLITES), clamp((uSteps - 64.0) / 64.0, 0.0, 1.0));

    // The camera orbits on two unrelated slow rates, so the body is seen from
    // a new angle every second even in silence, and a spike banks the orbit
    // toward the new travel direction.
    vec3 ro = vec3(0.0, 0.0, -3.1);
    mat3 cam = rotY(uTime * 0.11 + uFlowPhase * 0.9) * rotX(0.32 * sin(uTime * 0.07) + uMoveDir.y * 0.25);
    ro = cam * ro;
    vec3 rd = cam * normalize(vec3(uv, CB_FOCAL));

    // ---- the march ---------------------------------------------------------
    float t = 0.0;
    float hitT = -1.0;
    // How close the ray ever came without hitting: the glow around the body.
    float near = 1e9;
    for (int i = 0; i < CB_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        if (t > CB_FAR) break;
        vec3 p = ro + rd * t;
        float d = map(p);
        float eps = 0.0009 * t + 0.0004;
        near = min(near, d);
        if (d < eps) {
            hitT = t;
            break;
        }
        // map() already divided the host's term by the warp bound.
        t += max(d, eps);
    }

    // The room: the chrysanthemum on the camera-relative direction, so the
    // mandala sits behind the host and turns as the camera orbits.
    vec3 fog = dmtChrysanthemum(transpose(cam) * rd, 0.66, swell);
    vec3 col = fog;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        // Which body was hit is captured BEFORE the normal taps overwrite it.
        map(p);
        float isSat = gHitSat;
        float satHue = gDmtSatHue;
        float satLife = gDmtSatLife;
        float satBand = gDmtSatBand;
        float e = max(0.0012 * hitT, 0.0006);
        vec3 n = normalAt(p, e);
        float ao = occlusion(p, n);

        // Where on the host we are, in stirred space: the veining follows the
        // fold rather than the underlying primitive, which is what sells the
        // surface as something the flow made.
        vec3 q = fluidWarp3(p, gWarpScale, gWarpAmount);
        float vein = fbm3(q * 3.4 + uSpawnSeed * 17.0, 3);
        // A new spawn re-seeds the veining and grows it in over a second.
        vein = mix(0.5, vein, spawnGrow(1.1));

        // The host is banded by its veining and coloured from one end of the
        // palette; a satellite is banded by its distance from its own centre
        // (a shell coordinate) and takes its hue from its seed, so the bank
        // is a spread of related colours rather than copies of the host.
        float hue = mix(0.58 + 0.32 * smoothstep(0.35, 0.72, vein), 0.15 + 0.7 * satHue, isSat);
        float band = mix(vein * 0.7, satBand * 0.5, isSat);
        // Thin where the flow has stretched the host and where a satellite
        // is young or dying; both read as translucency.
        float thin = mix(smoothstep(0.55, 0.9, vein) * 0.8, (1.0 - satLife) * 0.5, isSat);
        col = dmtShade(n, rd, hue, band, ao, thin, treb);
        // A body arriving or leaving glows; the light fades in with its life.
        col += pal(hue + 0.45) * isSat * (1.0 - satLife) * 0.22;
        // Depth haze.
        col = mix(col, fog, 1.0 - exp(-hitT * 0.16));
    } else {
        // The halo: rays that grazed the body without hitting it. `near` is
        // the closest approach, so this is a true silhouette glow and not a
        // radial gradient pasted behind the object.
        col += dmtHalo(near, 0.50, 5.5, 0.30 + 0.35 * uSpike);
    }

    // The particle layer, in screen space, riding the 2D half of the same
    // library so it matches the other styles in the family.
    col += mix(pal(0.52), vec3(1.0), 0.35) * fluidMotes(uv, 5.5, 0.15) * 0.24;

    if (!touchIdle()) {
        col += pal(0.5 + 0.15 * sin(uTime * 0.05)) * min(touchWake(uv), 3.0) * 0.05;
    }
    col *= 0.68 + 0.32 * smoothstep(2.2, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
