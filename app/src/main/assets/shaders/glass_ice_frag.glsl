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

// Glass Ice: inside a geode. The eye floats in a hollow stone whose inner wall
// is lined with ice crystals - hexagonal prisms with six-sided points - all
// pointing in at you, and light seeps through them from beyond the rind: the
// flowing AURORA, bent and split by every crystal, so the cave glows from
// within its walls. Between the crystals the rind is dark druzy stone that
// sparkles. The crystals are clear at their tips and frosted toward their
// roots (rime), each carries a crack that flares on the snare, and they grow
// and shrink with the bass so the cave breathes. The rig's look turns you round
// inside it.
//
// The lining is a latitude-longitude tiling of the sphere: each cell holds one
// crystal standing on the wall along the inward normal, with a hashed size and
// lean. The cells are not equal in area, so the march steps at 0.6 of the
// field (a cell's crystal does not know its neighbours').
//
// ---- audio ------------------------------------------------------------------
//
//   bass         the crystals grow toward you, the cave closes in;
//   snare        cracks flare white in a scatter of crystals (which ones changes
//                per onset);
//   flatness     rime climbs toward the tips;
//   centroid     dispersion;
//   hat          the druzy rind sparkles;
//   pan, pitch   steer the brightest band of the light beyond the walls.
//
// ---- macros -----------------------------------------------------------------
//
// Speed scales the slow turn of the lining. Complexity packs more, finer
// crystals. Chaos ripples the ice. Energy scales every reaction.
//
// ---- cost -------------------------------------------------------------------
//
// Outer march up to uSteps, one crystal per step; one interior march; normals
// at two points; 5 AURORA lookups; on High a second interior march. About the
// old cluster's cost (it looped over nine crystals per step); estimated 4-6 ms
// at 810p on an Adreno 618 class GPU, Balanced.

#define IC_R 4.2
#define IC_IOR 1.31

float gLon;
float gLat;
float gGrow;
float gSpin;
vec3 gCell;

float crystal(vec3 q, float r, float L) {
    vec2 a = abs(q.xz);
    float m = max(a.x * 0.8660254 + a.y * 0.5, a.y);
    float side = m - r;
    float tip = (q.y - L + m * 1.6) * 0.5300066;
    return max(max(side, tip), -q.y - 0.3);
}

/** The crystal of the cell `p` lies in: its local frame point. `cid` gets the cell id. */
float lining(vec3 p, out vec3 cid) {
    float rr = length(p);
    vec3 dir = p / max(rr, 1e-4);
    float lon = atan(dir.z, dir.x) + gSpin;
    float lat = acos(clamp(dir.y, -1.0, 1.0));
    float i = floor(lon / GLASS_TAU * gLon);
    float j = floor(lat / GLASS_PI * gLat);
    cid = vec3(i, j, 0.0);
    vec2 h = glassHash2(vec2(i, j) + 3.7);
    float clon = (i + 0.5 + 0.3 * (h.x - 0.5)) / gLon * GLASS_TAU - gSpin;
    float clat = (j + 0.5 + 0.3 * (h.y - 0.5)) / gLat * GLASS_PI;
    vec3 cd = vec3(sin(clat) * cos(clon), cos(clat), sin(clat) * sin(clon));
    // Local frame: y along the inward normal, standing on the wall.
    vec3 up = -cd;
    vec3 side = normalize(cross(abs(up.y) > 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0), up));
    vec3 fwd = cross(up, side);
    vec3 q = p - cd * (IC_R - 0.05);
    vec3 lq = vec3(dot(q, side), dot(q, up), dot(q, fwd));
    float hs = glassHash(vec2(i, j) + 11.0);
    float r = (0.3 + 0.18 * hs) * (16.0 / gLon);
    float L = (1.1 + 1.5 * glassHash(vec2(i, j) + 5.0)) * gGrow;
    float d = crystal(lq, r, L);
    // The rind: the inside of the sphere.
    return min(d, IC_R - rr);
}

float map(vec3 p) {
    vec3 c;
    return (lining(p, c) * 0.6 + glassSurfaceWarp(p)) / glassSurfaceLip();
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

float crossIce(vec3 q, vec3 dir, float eps) {
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
    GlassEar ear = glassListen();
    float hue = 0.55 + 0.08 * uKeyStrength * sin(GLASS_TAU * uKeyHue) + 0.03 * sin(t * 0.02);
    GlassLook lk = glassLook(GLASS_ENV_FRACTAL, hue, ear);
    gGrow = 1.0 + 0.35 * clamp(ear.bass, 0.0, 1.2);
    gLon = glassComplexity() >= 0.5 ? 22.0 : 16.0;
    gLat = glassComplexity() >= 0.5 ? 11.0 : 8.0;
    gSpin = t * 0.02;
    float rime = 0.3 + 0.5 * ear.frost;
    float spread = 0.008 + 0.025 * ear.sharp;
    vec3 cold = vec3(0.78, 0.9, 1.1);

    vec3 ro;
    vec3 rd;
    camAround(uv, 1.2, 0.0, ro, rd);

    float hitT = -1.0;
    float used = 0.0;
    float tm = 0.05;
    for (int i = 0; i < GLASS_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        used = float(i);
        float d = map(ro + rd * tm);
        float eps = glassEps(tm);
        if (d < eps) {
            hitT = tm;
            break;
        }
        tm += max(d, eps * 0.5);
        if (tm > IC_R * 2.5) break;
    }
    if (hitT < 0.0) hitT = tm;

    vec3 p = ro + rd * hitT;
    vec3 cid;
    float dl = lining(p, cid);
    float rind = step(IC_R - length(p), 0.02 + glassEps(hitT));
    vec3 col;
    if (rind > 0.5) {
        // The druzy rind: dark stone that sparkles, lit faintly by the light it holds back.
        float n = glassNoise(vec2(atan(p.z, p.x), acos(clamp(p.y / IC_R, -1.0, 1.0))) * 18.0);
        col = mix(pal(hue + 0.75) * 0.03, pal(hue + 0.6) * 0.08, 0.5 + 0.5 * n);
        col += glassEnv(normalize(p), lk) * 0.35;
        col += vec3(0.9, 0.95, 1.0) * glassGlint(p, 22.0, 0.3 + ear.hat) * 0.7;
    } else {
        float eps = glassEps(hitT);
        vec3 n = normalAt(p, eps);
        float depth = smoothstep(IC_R - 1.6, IC_R - 0.2, length(p));
        float rough = rime * depth;
        n = glassGrain(n, p * 0.6, rough);
        float cosI = max(dot(n, -rd), 0.0);
        float fr = glassFresnel(cosI, IC_IOR);
        vec3 refl = glassEnvRough(reflect(rd, n), lk, rough * 0.25);
        GlassLook inner = lk;
        inner.rough += rough * 0.3;
        vec3 d1 = glassRefract(rd, n, 1.0 / IC_IOR);
        vec3 q = p - n * eps * 3.0;
        float ti = crossIce(q, d1, eps);
        vec3 pe = q + d1 * ti;
        vec3 n2 = normalAt(pe, eps);
        vec3 absorb = glassBeer(cold * 0.85, ti, 0.5);
        vec3 iors = glassIors(IC_IOR, spread);
        float fx = glassFresnel(max(dot(d1, n2), 0.0), IC_IOR);
        vec3 refr = glassEnv3(glassRefract(d1, -n2, iors.x), glassRefract(d1, -n2, iors.y), glassRefract(d1, -n2, iors.z), inner) * absorb * (1.0 - fx) * 2.0;
        vec3 d2 = reflect(d1, n2);
        if (glassMarchBounces()) {
            vec3 q2 = pe - n2 * eps * 3.0;
            float t2 = crossIce(q2, d2, eps);
            vec3 n3 = normalAt(q2 + d2 * t2, eps);
            refr += glassEnv(glassRefract(d2, -n3, IC_IOR), inner) * absorb * fx * 1.5;
        } else {
            refr += glassEnv(d2, inner) * absorb * absorb * fx * 0.8;
        }
        // The crack: flares on the snare in a scatter of crystals that changes per onset.
        float crackOn = step(0.65, glassHash(cid.xy + floor(SND_ONSET_COUNT) * 0.37));
        float crack = exp(-abs(dot(p - normalize(p) * (IC_R - 0.6), normalize(vec3(glassHash2(cid.xy) - 0.5, 0.3)))) * 30.0);
        refr += cold * crack * crackOn * ear.snare * 1.6;
        col = (refr * (1.0 - fr) + refl * fr) * glassStepAO(used * 0.4);
        col += glassRim(cosI, hue, 0.35);
        col += vec3(1.0) * glassGlint(p, 14.0, ear.hat) * (0.2 + rough);
    }

    col *= 0.8 + 0.2 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(glassTone(col)), 1.0);
}
