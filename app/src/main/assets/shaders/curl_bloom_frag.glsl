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

// Curl Bloom: one solid body, raymarched, that is continuously changing what
// it IS. Sphere becomes octahedron becomes torus becomes box and round to the
// sphere again, while the space it sits in is being stirred by an
// incompressible flow, so the surface is always folding through itself.
//
// This is the 3D counterpart to the 2D fluid in the other new styles: the same
// idea - advect the domain through a divergence-free field - but applied to
// the domain of a distance function instead of to a texture lookup, so the
// stirring deforms an actual solid rather than smearing a picture of one.
//
// ---- the morph -------------------------------------------------------------
//
// The four primitives sit on a ring and uFormPhase walks it, blending each
// neighbouring pair with mix(). That is a legitimate distance field and not an
// approximation: mix(a, b, t) of two 1-Lipschitz functions is 1-Lipschitz for
// any t in 0..1, because it is a convex combination. So the morph costs the
// march nothing - no step scale, no correction - and every intermediate shape
// is as marchable as the two it lies between.
//
// The ring is closed (box blends back into sphere), and uFormPhase glides
// rather than jumps, so there is no value of it at which the body is
// discontinuous. A spike chooses the next plateau; the body takes most of a
// second to get there.
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

// The stir. Kept as globals because map() is called from the march, from the
// normal taps and from the shadow loop, and all of them must see one shape.
float gWarpScale;
float gWarpAmount;
float gLip;
float gRadius;
float gMorph;

// The body, in stirred space.
//
// Four primitives on a closed ring. gMorph in 0..4 selects the pair; the
// fractional part is the blend. Written as a chain of mix() rather than a
// branch so every ray executes the same code and the field is continuous
// across every boundary including the 4 -> 0 wrap.
float body(vec3 p) {
    float r = gRadius;
    float t = fract(gMorph * 0.25) * 4.0;
    float i = floor(t);
    float f = smoothstep(0.0, 1.0, fract(t));

    float sphere = sdSphere(p, r);
    float octa = sdOctahedron(p, r * 1.25);
    float torus = sdTorus(p, vec2(r * 0.78, r * 0.34));
    float box = sdBox(p, vec3(r * 0.72));

    // The pair either side of i, on the ring.
    float a = i < 0.5 ? sphere : (i < 1.5 ? octa : (i < 2.5 ? torus : box));
    float b = i < 0.5 ? octa : (i < 1.5 ? torus : (i < 2.5 ? box : sphere));
    // Rounded a little throughout: opRound is a constant offset of the field,
    // so it is exact and free, and it keeps the octahedron's points from
    // aliasing into fireflies at the rim.
    return opRound(mix(a, b, f), r * 0.06);
}

// The scene: the body, seen through the stir.
//
// The warp is applied to the SAMPLE POINT, so what the march measures is the
// distance in warped space. That is an overestimate of the true distance in
// world space by at most the warp's Lipschitz bound, which is what gLip
// divides out at the step.
float map(vec3 p) {
    vec3 q = fluidWarp3(p, gWarpScale, gWarpAmount);
    return body(q);
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
    gMorph = uFormPhase * 4.0;

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
        // Divided by the warp's Lipschitz bound: the estimate was measured in
        // warped space and would otherwise overshoot the true clearance.
        t += max(d / gLip, eps);
    }

    // The medium the body hangs in, brightened toward the middle of the frame.
    vec3 fog = pal(0.66) * 0.055 * (0.6 + 0.5 * swell);
    vec3 col = fog;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        float e = max(0.0012 * hitT, 0.0006);
        vec3 n = normalAt(p, e);
        float ao = occlusion(p, n);

        // Where on the body we are, in stirred space: the veining follows the
        // fold rather than the underlying primitive, which is what sells the
        // surface as something the flow made.
        vec3 q = fluidWarp3(p, gWarpScale, gWarpAmount);
        float vein = fbm3(q * 3.4 + uSpawnSeed * 17.0, 3);
        // A new spawn re-seeds the veining and grows it in over a second.
        vein = mix(0.5, vein, spawnGrow(1.1));

        vec3 key = normalize(vec3(-0.45, 0.62, -0.65));
        float dif = clamp(dot(n, key), 0.0, 1.0);
        float back = clamp(dot(n, -key), 0.0, 1.0);
        float fres = pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 3.5);
        float spec = pow(clamp(dot(reflect(rd, n), key), 0.0, 1.0), 24.0 + 60.0 * treb);

        // Two materials mixed by the veining, so the fold lines are a colour
        // change as well as a shape one.
        vec3 skin = mix(pal(0.58), pal(0.90), smoothstep(0.35, 0.72, vein));
        col = skin * (0.10 + 0.75 * dif) * ao;
        // Rim, taking the treble - slew-limited, so a cymbal brightens the
        // edge over several frames rather than on the one it lands.
        col += mix(vec3(1.0), skin, 0.4) * fres * (0.22 + 0.55 * treb);
        col += vec3(1.0) * spec * 0.30;
        // Subsurface: light coming through the thin parts, which is what makes
        // the torus and the octahedron's points read as translucent.
        col += pal(0.10) * back * 0.16 * (0.5 + 0.5 * swell);
        // Depth haze.
        col = mix(col, fog, 1.0 - exp(-hitT * 0.16));
    } else {
        // The halo: rays that grazed the body without hitting it. `near` is
        // the closest approach, so this is a true silhouette glow and not a
        // radial gradient pasted behind the object.
        col += pal(0.50) * exp(-near * 5.5) * (0.30 + 0.35 * uSpike);
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
