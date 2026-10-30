#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_glass

// Glass Knot: a trefoil knot of clear glass tube hanging in the studio and
// turning slowly, its every edge broken into rainbow. The knot is the (2, 3)
// torus knot: a curve that winds twice round the axis of a torus while it
// winds three times round the tube. In the meridian half-plane at azimuth phi
// the curve passes through exactly two points, at curve parameters
// (phi + 2 pi k) / 2 for k = 0, 1, and the distance to the knot's tube is
// taken to the nearer of them. atan's jump at -pi only swaps which k is which,
// so the minimum over both is seamless. Measuring in the meridian plane ignores
// how the strand leans across it, which makes the estimate too long by up to
// the lean; it is divided by that bound (gLip) before stepping.
//
// The ray marches to the tube, refracts in, crosses it once and leaves through
// the far wall three times at three IORs (glassExitDispersed). A tube is the
// shape that shows dispersion best: its silhouette is all grazing incidence,
// where the three channels part furthest, so the knot is drawn in clear glass
// with spectral fringes along every edge and a bright Fresnel rim.
//
// ---- audio ------------------------------------------------------------------
//
// Snares (uHits.y) breathe the windings: each snare pushes the strands out
// from the torus's core circle and lets them settle back as the envelope falls
// (glassBump), so the knot loosens and re-tightens on the backbeat. Bass
// (uBassSmooth, with uBandsLo.y) thickens the tube. The key (uKeyHue,
// uKeyStrength) drifts the room's palette. Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the knot's turn. Complexity (uMorph) at 0.5 and above
// ties a (2, 5) cinquefoil instead of the trefoil. Chaos (uWarp, uTurbulence)
// ripples the tube (glassSurfaceWarp). Energy (uIntensity) scales the snare
// breath and the bass. Colour: the room and the glass's faint tint from pal().
// Space is the rig (camAround, no spin).

#define KN_CAM_R 4.4
#define KN_R 1.0
#define KN_IOR 1.5

mat3 gOrient;
float gQ;
float gWind;
float gTube;
float gLip;

float map(vec3 p) {
    vec3 q = gOrient * p;
    float rho = length(q.xz);
    float phi = atan(q.z, q.x);
    float best = 1e3;
    for (int k = 0; k < 2; k++) {
        float a = gQ * (phi + GLASS_TAU * float(k)) * 0.5;
        vec2 c = vec2(KN_R + gWind * cos(a), gWind * sin(a));
        best = min(best, length(vec2(rho, q.y) - c));
    }
    return (best - gTube + glassSurfaceWarp(q)) / (gLip * glassSurfaceLip());
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

void main() {
    vec2 uv = view();
    float t = glassTime();
    float energy = glassEnergy();
    float swell = clamp(uSwell, 0.0, 1.5);
    float bass = clamp(0.6 * uBassSmooth + 0.4 * uBandsLo.y, 0.0, 1.2);
    float hue = 0.04 * sin(t * 0.02) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue);

    gQ = glassComplexity() >= 0.5 ? 5.0 : 3.0;
    gWind = 0.42 + 0.09 * glassBump(uHits.y) * energy;
    gTube = 0.15 + 0.05 * bass * energy;
    float strand = gWind * gQ / (2.0 * max(KN_R - gWind - gTube, 0.2));
    gLip = sqrt(1.0 + strand * strand);
    gOrient = glassEuler(vec3(0.0, t * 0.08, 0.0)) * glassEuler(vec3(1.05 + 0.08 * sin(t * 0.07), 0.0, 0.25));

    vec3 ro;
    vec3 rd;
    camAround(uv, KN_CAM_R, 0.0, ro, rd);
    vec3 col = glassBackdrop(rd, hue);

    float hitT = -1.0;
    float used = 0.0;
    vec2 tb = glassSphere(ro, rd, vec3(0.0), KN_R + gWind + gTube + 0.15);
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
        float cosI = max(dot(n, -rd), 0.0);
        float fr = glassFresnel(cosI, KN_IOR);
        vec3 refl = glassBackdrop(reflect(rd, n), hue);

        vec3 d1 = glassRefract(rd, n, 1.0 / KN_IOR);
        vec3 q = p - n * eps * 3.0;
        float ti = 0.0;
        for (int i = 0; i < GLASS_MAX_INNER; i++) {
            if (float(i) >= glassInnerSteps()) break;
            float d = -map(q + d1 * ti);
            if (d < eps) break;
            ti += max(d, eps);
        }
        vec3 pe = q + d1 * ti;
        vec3 n2 = normalAt(pe, eps);
        float spread = 0.03 + 0.05 * (1.0 - cosI);
        vec3 tint = mix(vec3(1.0), pal(hue + 0.5), 0.2);
        vec3 refr = glassExitDispersed(d1, n2, KN_IOR, spread, hue) * glassBeer(tint, ti, 1.0);
        col = mix(refr, refl, fr) * glassStepAO(used * 0.5);
        col += pal(hue + 0.15) * pow(1.0 - cosI, 5.0) * 0.12;
    }

    col *= 0.85 + 0.18 * swell;
    col *= 0.78 + 0.22 * smoothstep(2.2, 0.3, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
