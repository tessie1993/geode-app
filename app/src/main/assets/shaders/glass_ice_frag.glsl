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

// Glass Ice: a druse of ice crystals - hexagonal prisms with six-sided points,
// fanned out of a frozen knot - standing against a low winter sun over a dark
// sea. Ice is glass with a story: the crystals are clear at their tips and
// frosted toward their roots, where rime has grown, so the roughness of each
// point's lookup (GlassLook.rough, the pre-filtered world) rises with depth
// into the cluster, with a sugar grain on the normals. Light crosses each
// crystal (interior march) and leaves dispersed - ice's dispersion is low, so
// the fringes are fine - and on High crosses a second time. Inside, each
// crystal carries a crack: a hashed plane that the interior ray tests once,
// shining like a mirror where it catches the sun.
//
// ---- audio ------------------------------------------------------------------
//
//   snare        cracks: each hit lights the cracks in a few crystals (which ones
//                changes per onset), a white flare inside the ice that fades;
//   bass         grows the crystals (length);
//   flatness     rime: noise frosts the cluster further up toward the tips;
//   centroid     dispersion and how crisp the tips' highlights are;
//   pitch, pan   the sun's height and bearing (glassLook);
//   hat          sparkle on the rime.
//
// ---- macros -----------------------------------------------------------------
//
// Speed scales the slow turn. Complexity adds two crystals. Chaos ripples the
// ice. Energy scales every reaction.
//
// ---- cost -------------------------------------------------------------------
//
// The field loops over up to nine crystals per step, so the outer march is the
// cost (bounded by a sphere); one interior march, normals at two points, 5
// DUSK lookups; on High a second interior march. Estimated 5-7 ms at 810p on
// an Adreno 618-class GPU on Balanced. See the PR.

#define IC_MAX 9
#define IC_CAM_R 4.0
#define IC_IOR 1.31
#define IC_BOUND 2.8

mat3 gRot[IC_MAX];
vec2 gDim[IC_MAX];
float gCount;
const vec3 IC_BASE = vec3(0.0, -1.0, 0.0);

/** A hexagonal prism of apothem r along +y from y = -0.3, capped by a six-sided point at height L. */
float crystal(vec3 q, float r, float L) {
    vec2 a = abs(q.xz);
    float m = max(a.x * 0.8660254 + a.y * 0.5, a.y);
    float side = m - r;
    float tip = (q.y - L + m * 1.6) * 0.5300066;
    return max(max(side, tip), -q.y - 0.3);
}

float map(vec3 p) {
    vec3 b = p - IC_BASE;
    float d = (length(b * vec3(0.8, 2.2, 0.8)) - 0.75) / 2.2;
    for (int i = 0; i < IC_MAX; i++) {
        if (float(i) >= gCount) break;
        d = min(d, crystal(gRot[i] * b, gDim[i].x, gDim[i].y));
    }
    return (d + glassSurfaceWarp(p)) / glassSurfaceLip();
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

float gCrystal;

float mapId(vec3 p) {
    vec3 b = p - IC_BASE;
    float d = (length(b * vec3(0.8, 2.2, 0.8)) - 0.75) / 2.2;
    gCrystal = -1.0;
    for (int i = 0; i < IC_MAX; i++) {
        if (float(i) >= gCount) break;
        float c = crystal(gRot[i] * b, gDim[i].x, gDim[i].y);
        if (c < d) {
            d = c;
            gCrystal = float(i);
        }
    }
    return d;
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
    float hue = 0.55 + 0.05 * uKeyStrength * sin(GLASS_TAU * uKeyHue);
    GlassLook lk = glassLook(GLASS_ENV_DUSK, hue, ear);
    lk.height *= 0.6;
    float grow = 1.0 + 0.2 * clamp(ear.bass, 0.0, 1.2);
    float rime = 0.35 + 0.45 * ear.frost;
    float spread = 0.006 + 0.02 * ear.sharp;
    vec3 cold = vec3(0.75, 0.9, 1.1);

    gCount = 7.0 + floor(glassComplexity() * 2.0 + 0.5);
    for (int i = 0; i < IC_MAX; i++) {
        float fi = float(i);
        float h1 = glassHash1(fi * 1.7 + 0.3);
        float h2 = glassHash1(fi * 2.9 + 4.1);
        float h3 = glassHash1(fi * 4.3 + 7.7);
        float tilt = fi < 0.5 ? 0.0 : 0.3 + 0.55 * h1;
        float az = fi * 2.39996 + 0.4 + t * 0.03;
        gRot[i] = transpose(glassEuler(vec3(tilt, az, 0.0)));
        gDim[i] = vec2(0.13 + 0.09 * h2, (1.0 + 1.1 * h3) * (1.0 - 0.25 * tilt) * grow);
    }

    vec3 ro;
    vec3 rd;
    camAround(uv, IC_CAM_R, 0.0, ro, rd);
    vec3 col = glassEnv(rd, lk);

    float hitT = -1.0;
    float used = 0.0;
    vec2 tb = glassSphere(ro, rd, vec3(0.0, -0.2, 0.0), IC_BOUND * grow);
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
        mapId(p);
        float cid = gCrystal;
        vec3 n = normalAt(p, eps);
        // Rime: frosted toward the roots of the cluster, clear at the tips.
        float depth = 1.0 - smoothstep(-1.4, 0.9, p.y);
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
        vec3 absorb = glassBeer(cold * 0.85, ti, 0.6);
        vec3 iors = glassIors(IC_IOR, spread);
        float fx = glassFresnel(max(dot(d1, n2), 0.0), IC_IOR);
        vec3 refr = glassEnv3(glassRefract(d1, -n2, iors.x), glassRefract(d1, -n2, iors.y), glassRefract(d1, -n2, iors.z), inner) * absorb * (1.0 - fx);
        vec3 d2 = reflect(d1, n2);
        if (glassMarchBounces()) {
            vec3 q2 = pe - n2 * eps * 3.0;
            float t2 = crossIce(q2, d2, eps);
            vec3 n3 = normalAt(q2 + d2 * t2, eps);
            refr += glassEnv(glassRefract(d2, -n3, IC_IOR), inner) * absorb * glassBeer(cold * 0.85, t2, 0.6) * fx * 1.5;
        } else {
            refr += glassEnv(d2, inner) * absorb * absorb * fx * 0.8;
        }
        // The crystal's crack: one hashed plane through its axis, crossed by the interior ray or not.
        if (cid >= 0.0) {
            int ci = int(cid);
            vec3 b = p - IC_BASE;
            vec3 cn = normalize(vec3(glassHash1(cid + 2.0) - 0.5, 0.35 * (glassHash1(cid + 5.0) - 0.5), glassHash1(cid + 9.0) - 0.5));
            vec3 lp = gRot[ci] * b;
            vec3 ld = gRot[ci] * d1;
            float den = dot(ld, cn);
            float tc = abs(den) > 1e-4 ? -dot(lp, cn) / den : -1.0;
            if (tc > 0.0 && tc < ti) {
                vec3 xp = lp + ld * tc;
                float inside = step(length(xp.xz), gDim[ci].x * 0.8) * step(0.0, xp.y) * step(xp.y, gDim[ci].y * 0.7);
                vec3 crackRefl = glassEnv(transpose(gRot[ci]) * reflect(ld, cn * sign(-den)), lk);
                float lit = step(0.6, glassHash1(cid + floor(SND_ONSET_COUNT) * 0.7)) * ear.snare;
                refr = mix(refr, crackRefl * 1.2 + cold * lit * 1.5, inside * 0.5);
            }
        }
        col = (refr * (1.0 - fr) + refl * fr) * glassStepAO(used * 0.5);
        col += vec3(1.0) * glassGlint(p, 14.0, ear.hat) * (0.3 + rough);
    }

    col *= 0.85 + 0.15 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(glassTone(col)), 1.0);
}
