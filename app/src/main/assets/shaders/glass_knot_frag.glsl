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

// Glass Knot: a trefoil (a cinquefoil at high Complexity) of thick clear glass
// tube hanging in the flowing AURORA light, seen through a turning
// kaleidoscope: the view folds into six (eight) mirrored wedges, each
// holding the knot, so the knot and the light behind it multiply into a slowly
// rotating mandala. Its surface is coated with a thin
// interference film like a lens coating or an oil sheen, so its reflections
// run in bands of colour while what shows through stays clear. The tube is a
// marched torus-knot field (the distance to the nearest of the knot's strands
// in the torus's meridian plane). Light goes in, crosses the tube (interior
// march), and leaves dispersed per wavelength; on High the far wall's
// reflected share crosses again (glassMarchBounces). Where the tube crosses
// itself the strands magnify each other.
//
// ---- audio ------------------------------------------------------------------
//
//   snare        a bulge runs round the knot: each hit sends a swelling along
//                the tube that the glass magnifies as it passes;
//   pitch height the coating's thickness, 200 -> 650 nm: the sheen's colours
//                roll as the melody climbs;
//   bass         thickens the tube;
//   centroid     dispersion through the tube;
//   flatness     frost on the coating;
//   pan, pitch   the key light's bearing and height (glassLook);
//   hat          glints on the coating;
//   pan          turns the kaleidoscope; the music's clock turns it continually.
//
// ---- macros -----------------------------------------------------------------
//
// Speed scales the turn. Complexity switches trefoil to cinquefoil. Chaos
// ripples the glass. Energy scales every reaction. Knobs (lib_glass's roles,
// offsets from 0.5 so off is the tuned look): k0 PULSE the emission, k1
// TEXTURE the tube's width, k2 FLOW added to Chaos's ripple, k3 TONE the
// palette position, k4 BUILD the world's glow, k5 SPACE the reach of the eye.
//
// ---- cost -------------------------------------------------------------------
//
// Outer march up to uSteps (two strand distances per step), one interior
// march, normals at two points, 5 studio lookups; on High a second interior
// march. The heaviest Glass style; estimated 5-7 ms at 810p on an Adreno
// 618-class GPU on Balanced. See the PR.

#define KN_CAM_R 4.4
#define KN_R 1.0
#define KN_IOR 1.5

mat3 gOrient;
float gQ;
float gWind;
float gTube;
float gLip;
float gPulse;
float gBulge;

float map(vec3 p) {
    vec3 q = gOrient * p;
    float rho = length(q.xz);
    float phi = atan(q.z, q.x);
    float best = 1e3;
    for (int k = 0; k < 2; k++) {
        float s = phi + GLASS_TAU * float(k);
        float a = gQ * s * 0.5;
        vec2 c = vec2(KN_R + gWind * cos(a), gWind * sin(a));
        // The snare's bulge: a swelling centred at gPulse along the knot's 4*pi parameter.
        float ds = mod(s - gPulse + GLASS_TAU, 2.0 * GLASS_TAU) - GLASS_TAU;
        float tube = gTube + gBulge * exp(-ds * ds * 3.0);
        best = min(best, length(vec2(rho, q.y) - c) - tube);
    }
    return (best + glassSurfaceWarp(q)) / (gLip * glassSurfaceLip());
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

float crossTube(vec3 q, vec3 dir, float eps) {
    // Start a little past the wall: just inside, the (Lipschitz-scaled) field is already under eps.
    float ti = 0.03;
    for (int i = 0; i < GLASS_MAX_INNER; i++) {
        if (float(i) >= glassInnerSteps()) break;
        float d = -map(q + dir * ti);
        if (d < eps) break;
        ti += max(d, eps);
    }
    return ti;
}

void main() {
    vec2 uv = view();
    float t = glassTime();
    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kFlow = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    float kSpace = clamp(uKnob[5], 0.0, 1.0) - 0.5;
    gGlassFlow = kFlow;
    GlassEar ear = glassListen();
    float hue = 0.04 * sin(t * 0.02) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue) + 0.6 * kTone;
    GlassLook lk = glassLook(GLASS_ENV_FRACTAL, hue, ear);
    lk.glow *= 1.0 + kBuild;
    float spread = 0.015 + 0.04 * ear.sharp;
    float frost = ear.frost * 0.5;
    float coat = 200.0 + 450.0 * ear.height;

    gQ = glassComplexity() >= 0.5 ? 5.0 : 3.0;
    gWind = 0.38 + 0.08 * (0.5 + 0.5 * sin(t * 0.21));
    gTube = (0.17 + 0.05 * clamp(ear.bass, 0.0, 1.2)) * (1.0 + 0.3 * kTexture);
    gPulse = (1.0 - clamp(SND_SNARE_ENV, 0.0, 1.0)) * 2.0 * GLASS_TAU + glassHash1(floor(SND_ONSET_COUNT)) * 2.0 * GLASS_TAU;
    gBulge = 0.09 * ear.snare;
    float strand = gWind * gQ / (2.0 * max(KN_R - gWind - gTube, 0.2));
    gLip = sqrt(1.0 + strand * strand) * (1.0 + gBulge * 2.0);
    gOrient = glassEuler(vec3(0.0, t * 0.08, 0.0)) * glassEuler(vec3(1.05 + 0.08 * sin(t * 0.07), 0.0, 0.25));

    vec3 ro;
    vec3 rd;
    // The kaleidoscope: fold the view into mirrored wedges that turn on the music's clock.
    // Six wedges, eight with Complexity; each wedge looks at the knot from its centre, so
    // the knot repeats round the frame as a ring of glass.
    float wedges = glassComplexity() >= 0.5 ? 8.0 : 6.0;
    vec2 kv = uv * glassRot(0.05 * SND_CURVED_TIME + 0.3 * ear.pan);
    float ka = atan(kv.y, kv.x);
    float seg = GLASS_TAU / wedges;
    ka = abs(mod(ka, seg) - seg * 0.5);
    vec2 folded = vec2(cos(ka), sin(ka)) * length(kv);
    vec2 kuv = (folded - vec2(0.62, 0.0)) * 1.25;
    camAround(kuv, KN_CAM_R * (1.0 + 0.4 * kSpace), 0.0, ro, rd);
    vec3 col = glassEnv(rd, lk);

    float hitT = -1.0;
    float used = 0.0;
    vec2 tb = glassSphere(ro, rd, vec3(0.0), KN_R + gWind + gTube + 0.25);
    if (tb.y > 0.0) {
        float tm = max(tb.x, 0.0);
        for (int i = 0; i < GLASS_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (tm > tb.y) break;
            used = float(i);
            float d = map(ro + rd * tm);
            float eps = glassEps(tm);
            if (d < eps) {
                hitT = tm;
                break;
            }
            tm += max(d, eps * 0.5);
        }
    }

    if (hitT > 0.0) {
        float eps = glassEps(hitT);
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, eps);
        n = glassGrain(n, p, frost);
        float cosI = max(dot(n, -rd), 0.0);
        float fr = glassFresnel(cosI, KN_IOR);
        // The coating: interference colours on the reflection, strongest face on where plain glass reflects least.
        vec3 film = glassFilm(coat, cosI, 1.38);
        vec3 refl = glassEnvRough(reflect(rd, n), lk, frost * 0.1) * mix(vec3(1.0), film * 2.0, 0.7);
        float frC = fr + 0.12 * (1.0 - fr);
        vec3 tint = mix(vec3(1.0), pal(hue + 0.5), 0.15);

        vec3 d1 = glassRefract(rd, n, 1.0 / KN_IOR);
        vec3 q = p - n * eps * 3.0;
        float ti = crossTube(q, d1, eps);
        vec3 pe = q + d1 * ti;
        vec3 n2 = normalAt(pe, eps);
        vec3 absorb = glassBeer(tint, ti, 0.8);
        vec3 iors = glassIors(KN_IOR, spread);
        float fx = glassFresnel(max(dot(d1, n2), 0.0), KN_IOR);
        vec3 refr = glassEnv3(glassRefract(d1, -n2, iors.x), glassRefract(d1, -n2, iors.y), glassRefract(d1, -n2, iors.z), lk) * absorb * (1.0 - fx);
        vec3 d2 = reflect(d1, n2);
        if (glassMarchBounces()) {
            vec3 q2 = pe - n2 * eps * 3.0;
            float t2 = crossTube(q2, d2, eps);
            vec3 pe2 = q2 + d2 * t2;
            vec3 n3 = normalAt(pe2, eps);
            refr += glassEnv(glassRefract(d2, -n3, KN_IOR), lk) * absorb * glassBeer(tint, t2, 0.8) * fx * 1.5;
        } else {
            refr += glassEnv(d2, lk) * absorb * absorb * fx * 0.8;
        }
        col = (refr * (1.0 - frC) + refl * frC) * glassStepAO(used * 0.5);
        col += glassRim(cosI, hue + 0.1, 1.0);
        col += vec3(1.0) * glassGlint(gOrient * p, 9.0, ear.hat) * (0.3 + fr);
    }

    col *= 0.85 + 0.15 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(glassTone(col * (1.0 + 0.5 * kPulse))), 1.0);
}
