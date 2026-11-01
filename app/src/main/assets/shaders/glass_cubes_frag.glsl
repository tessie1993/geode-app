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

// Glass Cubes: nine rounded glass blocks hanging in a three-by-three wall in
// the neon room, each a different palette tint. Rounded edges are what make a
// block read as glass (they catch thin bright lines of the neon on every
// edge), so the blocks are a marched rounded-box field, one cell per block.
// The light goes in, crosses the block (an interior march on the negated
// field), and leaves dispersed per wavelength; on High the share the far wall
// reflects crosses again and leaves too (glassMarchBounces), so each block
// shows a second, mirrored image of the room.
//
// ---- audio ------------------------------------------------------------------
//
//   snare        a flip wave: every hit turns the blocks a quarter turn, the turn
//                travelling across the wall from one corner (SND_SNARE_TOGGLE,
//                which eases 0 <-> 1 per hit, lagged per block), and back on the next;
//   kick         a nod: the blocks dip, eased;
//   bass         the blocks breathe (size);
//   centroid     dispersion: bright mixes fringe the edges harder;
//   flatness     frost: the faces go milky and grained;
//   pan, pitch   the neon ring's bearing and height (glassLook);
//   hat          glints on the faces.
//
// ---- macros -----------------------------------------------------------------
//
// Speed scales the slow tumble. Complexity deepens the tints. Chaos ripples
// the faces (glassSurfaceWarp). Energy scales every reaction.
//
// ---- cost -------------------------------------------------------------------
//
// Outer march up to uSteps (most rays leave the bounding sphere early), one
// interior march (glassInnerSteps), 4-tap normals at two points, 4 world
// lookups; on High a second interior march and normal. Estimated 4-6 ms at
// 810p on an Adreno 618-class GPU on Balanced; see the PR.

#define CB_SPACING 1.6
#define CB_CAM_R 5.2
#define CB_BOUND 3.9
#define CB_IOR 1.5

mat3 gRot[9];
vec3 gCentre[9];
float gHalf;
float gRound;
float gId;

float roundBox(vec3 p, vec3 b, float r) {
    vec3 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - r;
}

float map(vec3 p) {
    vec2 cell = clamp(floor(p.xy / CB_SPACING + 0.5), -1.0, 1.0);
    int idx = int(cell.x + 1.0 + (cell.y + 1.0) * 3.0);
    gId = float(idx);
    vec3 q = gRot[idx] * (p - gCentre[idx]);
    float d = (roundBox(q, vec3(gHalf), gRound) + glassSurfaceWarp(q)) / glassSurfaceLip();
    float border = 1e3;
    if (cell.x < 0.5) border = min(border, (cell.x + 0.5) * CB_SPACING - p.x);
    if (cell.x > -0.5) border = min(border, p.x - (cell.x - 0.5) * CB_SPACING);
    if (cell.y < 0.5) border = min(border, (cell.y + 0.5) * CB_SPACING - p.y);
    if (cell.y > -0.5) border = min(border, p.y - (cell.y - 0.5) * CB_SPACING);
    return min(d, max(border, 0.0) + 0.04);
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

/** Walks from inside a block to its far wall; returns the distance. */
float crossBlock(vec3 q, vec3 dir, float eps) {
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
    GlassEar ear = glassListen();
    float hue = 0.04 * sin(t * 0.021) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue);
    GlassLook lk = glassLook(GLASS_ENV_NEON, hue, ear);
    float nod = ear.kick * 0.35;
    gHalf = 0.5 + 0.04 * clamp(ear.bass, 0.0, 1.2);
    gRound = 0.1;
    float spread = 0.015 + 0.04 * ear.sharp;
    float frost = ear.frost * 0.6;
    float flipT = clamp(SND_SNARE_TOGGLE, 0.0, 1.0);

    for (int i = 0; i < 9; i++) {
        float fi = float(i);
        vec2 cell = vec2(mod(fi, 3.0) - 1.0, floor(fi / 3.0) - 1.0);
        float h1 = glassHash(cell + 1.7);
        float h2 = glassHash(cell + 5.3);
        // The flip wave: lagged by distance from the top-left corner.
        float lag = length(cell - vec2(-1.0, 1.0)) / 2.83;
        float flip = clamp(flipT * 1.7 - lag * 0.7, 0.0, 1.0);
        flip = flip * flip * (3.0 - 2.0 * flip) * 1.5707963 * min(glassEnergy(), 1.0);
        gRot[i] = glassEuler(vec3(0.35 + flip + 0.12 * sin(t * (0.2 + 0.1 * h1) + h2 * 6.0),
                                  0.5 + 0.15 * sin(t * (0.15 + 0.1 * h2) + h1 * 6.0),
                                  nod * (h1 - 0.5)));
        gCentre[i] = vec3(cell * CB_SPACING, 0.25 * sin(t * 0.3 + h1 * 6.0) - nod * 0.3);
    }

    vec3 ro;
    vec3 rd;
    camAround(uv, CB_CAM_R, 0.0, ro, rd);
    vec3 col = glassEnv(rd, lk);

    float hitT = -1.0;
    float used = 0.0;
    float id = 0.0;
    vec2 tb = glassSphere(ro, rd, vec3(0.0), CB_BOUND);
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
                id = gId;
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
        float fr = glassFresnel(cosI, CB_IOR);
        vec3 refl = glassEnvRough(reflect(rd, n), lk, frost * 0.1);
        vec3 tint = mix(vec3(1.0), pal(hue + 0.11 * id), 0.45 + 0.4 * glassComplexity());
        GlassLook inner = lk;
        inner.rough += frost * 0.2;

        vec3 d1 = glassRefract(rd, n, 1.0 / CB_IOR);
        vec3 q = p - n * eps * 3.0;
        float ti = crossBlock(q, d1, eps);
        vec3 pe = q + d1 * ti;
        vec3 n2 = normalAt(pe, eps);
        vec3 absorb = glassBeer(tint, ti, 1.1);
        vec3 iors = glassIors(CB_IOR, spread);
        float fx = glassFresnel(max(dot(d1, n2), 0.0), CB_IOR);
        vec3 refr = glassEnv3(glassRefract(d1, -n2, iors.x), glassRefract(d1, -n2, iors.y), glassRefract(d1, -n2, iors.z), inner) * absorb * (1.0 - fx);
        vec3 d2 = reflect(d1, n2);
        if (glassMarchBounces()) {
            vec3 q2 = pe - n2 * eps * 3.0;
            float t2 = crossBlock(q2, d2, eps);
            vec3 pe2 = q2 + d2 * t2;
            vec3 n3 = normalAt(pe2, eps);
            refr += glassEnv(glassRefract(d2, -n3, CB_IOR), inner) * absorb * glassBeer(tint, t2, 1.1) * fx * 1.5;
        } else {
            refr += glassEnv(d2, inner) * absorb * absorb * fx * 0.8;
        }
        col = (refr * (1.0 - fr) + refl * fr) * glassStepAO(used * 0.5);
        col += vec3(1.0) * glassGlint(gRot[int(id)] * (p - gCentre[int(id)]), 8.0, ear.hat) * (0.3 + fr);
    }

    col *= 0.85 + 0.15 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(glassTone(col)), 1.0);
}
