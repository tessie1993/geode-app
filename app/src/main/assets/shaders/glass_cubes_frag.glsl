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

// Glass Cubes: an endless lattice of rounded glass blocks hanging in the
// flowing AURORA light, and the eye flies through it on the rig's path. Every
// block turns slowly on its own axis, on time alone (owner 2026-11-12, no
// style spins to the music: the snare's flip wave and the kick's nod are gone).
// Through every block the ribbons of light behind it bend and split into
// rainbows, and on High the far wall's reflection sends a second image back
// through. The lattice thins near the path so the flight never collides, and
// fades into the light with distance.
//
// The lattice repeats every 2 units along z, a power of two, so it wraps with
// the rig's travel without a seam. Each block lives in its own cell, the step
// is capped at the cell border, and empty cells (half, hashed) open
// sightlines down the lattice.
//
// ---- audio ------------------------------------------------------------------
//
//   bass         the blocks breathe;
//   onsets       recolour the lattice a notch (the tint hash slides per onset);
//   centroid     dispersion; flatness frost; pitch steers the light;
//   hat          glints. The world flows faster in loud passages.
//
// ---- macros -----------------------------------------------------------------
//
// Speed scales the turning. Complexity deepens the tints. Chaos ripples the
// faces. Energy scales every reaction. Space is the rig (camFly). Knobs
// (lib_glass's roles, offsets from 0.5 so off is the tuned look): k0 PULSE the
// emission, k1 TEXTURE the blocks' width, k2 FLOW added to Chaos's ripple, k3
// TONE the palette position, k4 BUILD the world's glow, k5 SPACE the reach.
//
// ---- cost -------------------------------------------------------------------
//
// Outer march to 24 units at most uSteps steps, one interior march, normals at
// two points, 5 AURORA lookups (each 8 sines); on High a second interior
// march. Estimated 5-7 ms at 810p on an Adreno 618 class GPU, Balanced.

#define CB_S 2.0
#define CB_IOR 1.5
#define CB_FAR 24.0

float gHalf;
float gT;
vec3 gEye;

mat3 cellRot(vec3 c) {
    float h1 = glassHash3(c + 1.7);
    float h2 = glassHash3(c + 5.3);
    return glassEuler(vec3(h1 * 6.28, h2 * 6.28 + gT * (0.2 + 0.2 * h1), 0.3 * sin(gT * 0.3 + h2 * 6.0)));
}

/** Cell id of p, whether it holds a block, its local point. */
float cellBlock(vec3 p, out vec3 c, out vec3 q) {
    c = floor(p / CB_S + 0.5);
    q = p - c * CB_S;
    vec3 cw = c * CB_S;
    float empty = step(glassHash3(c + 9.1), 0.5);
    float nearEye = 1.0 - step(1.6, length(cw.xy - gEye.xy));
    return 1.0 - max(empty, nearEye);
}

float roundBox(vec3 p, vec3 b, float r) {
    vec3 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - r;
}

float map(vec3 p) {
    vec3 c;
    vec3 q;
    float has = cellBlock(p, c, q);
    vec3 a = CB_S * 0.5 - abs(q);
    float border = min(a.x, min(a.y, a.z)) + 0.05;
    if (has < 0.5) return border;
    float d = (roundBox(cellRot(c) * q, vec3(gHalf), 0.12) + glassSurfaceWarp(q)) / glassSurfaceLip();
    return min(d, border);
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

float crossBlock(vec3 q, vec3 dir, float eps) {
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
    gT = t;
    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kFlow = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    float kSpace = clamp(uKnob[5], 0.0, 1.0) - 0.5;
    gGlassFlow = kFlow;
    GlassEar ear = glassListen();
    float hue = 0.04 * sin(t * 0.021) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue) + 0.6 * kTone;
    GlassLook lk = glassLook(GLASS_ENV_FRACTAL, hue, ear);
    lk.glow *= 1.0 + kBuild;
    float reachFar = CB_FAR * (1.0 + 0.4 * kSpace);
    gHalf = (0.38 + 0.05 * clamp(ear.bass, 0.0, 1.2)) * (1.0 + 0.15 * kTexture);
    float spread = 0.015 + 0.04 * ear.sharp;
    float frost = ear.frost * 0.6;
    float tintSlide = glassGlide(SND_ONSET_COUNT, SND_ONSET_ENV) * 0.07;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    gEye = ro;
    vec3 col = glassEnv(rd, lk);

    float hitT = -1.0;
    float used = 0.0;
    float tm = 0.1;
    for (int i = 0; i < GLASS_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        if (tm > reachFar) break;
        used = float(i);
        float d = map(ro + rd * tm);
        float eps = glassEps(tm);
        if (d < eps) {
            hitT = tm;
            break;
        }
        tm += max(d, eps * 0.5);
    }

    if (hitT > 0.0) {
        float eps = glassEps(hitT);
        vec3 p = ro + rd * hitT;
        vec3 c;
        vec3 lq;
        cellBlock(p, c, lq);
        vec3 n = normalAt(p, eps);
        n = glassGrain(n, p, frost);
        float cosI = max(dot(n, -rd), 0.0);
        float fr = glassFresnel(cosI, CB_IOR);
        vec3 refl = glassEnvRough(reflect(rd, n), lk, frost * 0.1);
        vec3 tint = mix(vec3(1.0), pal(hue + glassHash3(c) * 0.6 + tintSlide), 0.4 + 0.4 * glassComplexity());
        GlassLook inner = lk;
        inner.rough += frost * 0.2;

        vec3 d1 = glassRefract(rd, n, 1.0 / CB_IOR);
        vec3 q = p - n * eps * 3.0;
        float ti = crossBlock(q, d1, eps);
        vec3 pe = q + d1 * ti;
        vec3 n2 = normalAt(pe, eps);
        vec3 absorb = glassBeer(tint, ti, 1.2);
        vec3 iors = glassIors(CB_IOR, spread);
        float fx = glassFresnel(max(dot(d1, n2), 0.0), CB_IOR);
        vec3 refr = glassEnv3(glassRefract(d1, -n2, iors.x), glassRefract(d1, -n2, iors.y), glassRefract(d1, -n2, iors.z), inner) * absorb * (1.0 - fx);
        vec3 d2 = reflect(d1, n2);
        if (glassMarchBounces()) {
            vec3 q2 = pe - n2 * eps * 3.0;
            float t2 = crossBlock(q2, d2, eps);
            vec3 n3 = normalAt(q2 + d2 * t2, eps);
            refr += glassEnv(glassRefract(d2, -n3, CB_IOR), inner) * absorb * glassBeer(tint, t2, 1.2) * fx * 1.5;
        } else {
            refr += glassEnv(d2, inner) * absorb * absorb * fx * 0.8;
        }
        vec3 block = (refr * (1.0 - fr) + refl * fr) * glassStepAO(used * 0.4);
        block += glassRim(cosI, hue + glassHash3(c) * 0.6, 1.0);
        block += vec3(1.0) * glassGlint(cellRot(c) * lq, 8.0, ear.hat) * (0.3 + fr);
        col = mix(block, col, smoothstep(reachFar * 0.45, reachFar, hitT));
    }

    col *= 0.85 + 0.15 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(glassTone(col * (1.0 + 0.5 * kPulse))), 1.0);
}
