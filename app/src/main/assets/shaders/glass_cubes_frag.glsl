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

// Glass Cubes: nine rounded glass blocks hanging in a loose three-by-three
// grid, each tumbling slowly on its own axes, each tinted its own colour. The
// first of the family's marched styles: a ray marches to a block, refracts in,
// marches the block's interior once (the field negated, so it walks to the far
// wall), and refracts out into the backdrop - or, at a steep inner face, takes
// the mirror direction and stops. The rounded edges and corners are where the
// refraction bends hardest, so every block is outlined in bright, broken
// slivers of the studio's light panels, and its tint deepens with how much
// glass the ray crossed (Beer-Lambert), dark along a long diagonal, pale across
// a thin corner.
//
// One field, nine cells: a point is measured against the block of its own
// cell only, and the step is capped at the distance to the cell's border so a
// ray never strides past a neighbour it was not told about. The nine
// orientations are built once per pixel, not once per step.
//
// ---- audio ------------------------------------------------------------------
//
// Kicks (uHits.x) nod the blocks: each kick tips every block a little about
// its own z axis, each in its own direction, and the nod rises and settles as
// the envelope falls (glassBump), so the grid ripples on the beat instead of
// jumping. Bass (uBassSmooth) breathes their size a few percent. Mids
// (uMidSmooth) round their edges. Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the tumble. Complexity (uMorph) deepens the tint.
// Chaos (uWarp, uTurbulence) ripples the faces (glassSurfaceWarp). Energy
// (uIntensity) scales the kick nod and the bass breath. Colour: each block's
// tint and the room from pal(). Space is the rig (camAround, no spin).

#define CB_SPACING 1.55
#define CB_CAM_R 6.6
#define CB_BOUND 3.7
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
    // Cap the step at the border with any neighbouring cell.
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

void main() {
    vec2 uv = view();
    float t = glassTime();
    float energy = glassEnergy();
    float swell = clamp(uSwell, 0.0, 1.5);
    float nod = glassBump(uHits.x) * 0.45 * energy;
    gHalf = 0.4 + 0.03 * clamp(uBassSmooth, 0.0, 1.0) * energy;
    gRound = 0.08 + 0.06 * clamp(uMidSmooth, 0.0, 1.2);
    float hue = 0.04 * sin(t * 0.021) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue);

    for (int i = 0; i < 9; i++) {
        float fi = float(i);
        vec2 cell = vec2(mod(fi, 3.0) - 1.0, floor(fi / 3.0) - 1.0);
        float h1 = glassHash(cell + 1.7);
        float h2 = glassHash(cell + 5.3);
        float h3 = glassHash(cell + 9.1);
        gRot[i] = glassEuler(vec3(h1 * GLASS_TAU + t * (0.12 + 0.1 * h2),
                                  h2 * GLASS_TAU + t * (0.09 + 0.08 * h3),
                                  h3 * GLASS_TAU + nod * (h1 - 0.5) * 2.0));
        vec2 jit = (glassHash2(cell + 4.4) - 0.5) * 0.16;
        gCentre[i] = vec3(cell * CB_SPACING + jit, (glassHash(cell + 7.7) - 0.5) * 1.2);
    }

    vec3 ro;
    vec3 rd;
    camAround(uv, CB_CAM_R, 0.0, ro, rd);
    vec3 col = glassBackdrop(rd, hue);

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
        float cosI = max(dot(n, -rd), 0.0);
        float fr = glassFresnel(cosI, CB_IOR);
        vec3 refl = glassBackdrop(reflect(rd, n), hue);

        // One pass through the block.
        vec3 d1 = glassRefract(rd, n, 1.0 / CB_IOR);
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
        vec3 ex = glassRefract(d1, -n2, CB_IOR);
        vec3 tint = mix(vec3(1.0), pal(hue + 0.13 * id), 0.55 + 0.3 * glassComplexity());
        vec3 refr = glassBackdrop(ex, hue) * glassBeer(tint, ti, 1.3);
        col = mix(refr, refl, fr) * glassStepAO(used * 0.5);
    }

    col *= 0.85 + 0.18 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
