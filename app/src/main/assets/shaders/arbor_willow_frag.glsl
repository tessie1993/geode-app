#version 300 es
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
//#include lib_sdf3
//#include lib_arbor

// Arbor Willow: a weeping willow on the edge of a still pond at night, its
// curtain of leaf strands hanging to the water, fireflies drifting through
// it. Raymarched, orbited with camAround.
//
// Two fields. The limbs are arborTree with gravity (`bend`): every child turns
// toward world down in proportion to how far off vertical it already is, so
// the limbs arch out and sag. The curtain is NOT grown off the tips: a strand
// long enough to reach the water leaves its own subtree's fold region and the
// fold would chop it into dashes. It is a separate polar repetition of plumb
// strands on three rings under the crown, each starting at the crown's
// underside and swaying from its top like a pendulum. The pond is a plane,
// solved analytically rather than marched, and everything is cut at its
// surface so the strands end ON the water.
//
// ---- audio ------------------------------------------------------------------
//
// Mid is the WEIGHT. uMidSmooth (slew-limited) adds to the limbs' gravity and
// lowers the curtain's hem toward the pond together: a rich mid range drags
// the whole tree down, a thin one lets it lift. Kick (uHits.x) sends a ripple
// ring out across the pond from the trunk, as a normal perturbation that only
// bends the moon's reflection. Hat (uHits.z) startles the fireflies: their
// swarm radius jumps out and settles back as the envelope falls. Brightness
// follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales every clock (curtain sway, water lap, fireflies).
// Complexity: uMorph adds a fold level (arbLevels) and a fourth curtain ring.
// Chaos: uTurbulence and uWarp shake the per-level twist and the strands'
// sway into a wind-torn curtain; uTwist winds the canopy into a spiral.
// Colour: leaves, sky and fireflies from pal(). Energy: uIntensity scales the
// mid weight, the kick ripple and the hat startle. Space: camAround, the rig's
// orbit and dolly.

#define WL_MAX_STEPS 112
#define WL_FAR 30.0
#define WL_WATER -2.3
#define WL_FIREFLIES 10
#define WL_TRUNK_Y (WL_WATER - 0.05)

ArborForm gF;
float gGrow;
float gHem;
float gSway;
float gTime;
float gRings;
float gIsLeaf;

/** Height of the crown's underside above radius r: the curtain's top. */
float crownUnder(float r) {
    return WL_TRUNK_Y + 3.4 - 0.22 * r * r;
}

float curtain(vec3 p) {
    float r = length(p.xz);
    float ring = clamp(floor((r - 0.9) / 0.75 + 0.5), 0.0, gRings - 1.0);
    float R = 0.9 + 0.75 * ring;
    float n = 16.0 + 9.0 * ring;
    float w = ARB_TAU / n;
    float a = atan(p.z, p.x);
    float cell = floor(a / w + 0.5);
    float da = a - cell * w;
    float h = hash11(cell * 7.31 + ring * 113.0);
    float top = crownUnder(R);
    float hem = WL_WATER + 0.05 + gHem * (0.4 + 0.6 * h);
    // Pendulum sway, zero at the strand's top and growing toward the hem.
    float drop = clamp((top - p.y) / max(top - hem, 0.1), 0.0, 1.0);
    float sway = gSway * drop * drop * sin(gTime * (0.6 + 0.3 * h) + h * 6.2831);
    vec2 q = vec2(r - R + 0.12 * h, r * sin(da) - sway);
    float vy = max(p.y - top, 0.0) + max(hem - p.y, 0.0);
    float rad = 0.022 * (1.0 + 0.35 * sin(p.y * 22.0 + h * 9.0));
    return length(vec3(q, vy)) - rad;
}

float map(vec3 p) {
    float tree = arborTree(p - vec3(0.0, WL_TRUNK_Y, 0.0), gF, gGrow);
    float c = curtain(p) * 0.8;
    gIsLeaf = step(c, tree);
    return max(min(tree, c), WL_WATER - p.y);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

vec3 skyCol(vec3 rd, vec3 moonDir, float hue) {
    float up = clamp(rd.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 c = mix(pal(hue) * 0.16 + vec3(0.02, 0.035, 0.05), pal(hue + 0.15) * 0.02 + vec3(0.004, 0.006, 0.02),
                 smoothstep(0.45, 1.0, up));
    float m = dot(rd, moonDir);
    c += vec3(0.9, 0.95, 1.0) * smoothstep(0.9993, 0.9996, m);
    c += vec3(0.35, 0.45, 0.55) * pow(max(m, 0.0), 60.0) * 0.5;
    return c;
}

void main() {
    vec2 uv = view();

    float t = uTime * uSpeed;
    float energy = uIntensity;
    float chaos = arbChaos();
    float swell = clamp(uSwell, 0.0, 1.5);
    float mid = clamp(uMidSmooth, 0.0, 1.5) * energy;
    float kick = clamp(uHits.x * energy, 0.0, 1.0);
    float hat = clamp(uHits.z * energy, 0.0, 1.0);

    gTime = t;
    gGrow = arbLevels(7.0, 1.0);
    gRings = 3.0 + floor(arbMorph() + 0.5);
    float twist = 2.4 + 0.05 * sin(t * 0.23) + chaos * 0.35 * sin(t * 1.7) + 0.6 * uTwist;
    float droop = 0.20 + 0.20 * mid + 0.03 * sin(t * 0.4) + chaos * 0.05 * sin(t * 2.3);
    gF = ArborForm(2.0, 0.36, droop, twist, 0.78, 1.40, 0.065, 0.78);
    gHem = clamp(1.5 - 0.8 * mid, 0.2, 1.6);
    gSway = 0.10 + 0.25 * chaos;

    vec3 ro;
    vec3 rd;
    camAround(uv, 10.5, 3.0 * CAM_SPIN_UNIT, ro, rd);

    vec3 moonDir = normalize(vec3(-0.45, 0.42, 0.78));
    float hue = 0.48 + 0.05 * sin(t * 0.02);

    float tPlane = rd.y < -1e-4 ? (WL_WATER - ro.y) / rd.y : 1e9;
    float tMax = min(WL_FAR, tPlane);

    float tm = 0.05;
    float hitT = -1.0;
    float used = 0.0;
    float hitLevel = 0.0;
    float hitLeaf = 0.0;
    for (int i = 0; i < WL_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        if (tm > tMax) break;
        used = float(i);
        float d = map(ro + rd * tm);
        float eps = 0.0008 * tm + 0.0005;
        if (d < eps) {
            hitT = tm;
            hitLevel = gArbLevel;
            hitLeaf = gIsLeaf;
            break;
        }
        tm += d * 0.8;
    }

    vec3 col;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, 0.0012 * hitT + 0.0006);
        vec3 lit = arbLight(n, rd, moonDir, 0.6, 18.0);
        float ao = arbStepAO(used, uSteps);
        vec3 bark = vec3(0.13, 0.11, 0.10);
        vec3 leaf = mix(vec3(0.45, 0.60, 0.48), pal(hue + 0.1), 0.6);
        float leafy = max(hitLeaf, smoothstep(3.0, 6.0, hitLevel));
        vec3 base = mix(bark, leaf, leafy);
        col = base * (0.18 + 0.95 * lit.x) * ao;
        col += vec3(0.6, 0.7, 0.8) * lit.y * 0.25 * ao + vec3(lit.z) * 0.15;
        // The hem catches a little moonlight thrown back off the pond.
        col += leaf * 0.15 * smoothstep(WL_WATER + 1.5, WL_WATER, p.y) * leafy;
        col = mix(col, skyCol(rd, moonDir, hue), 1.0 - exp(-hitT * 0.035));
    } else if (tPlane < WL_FAR) {
        vec3 p = ro + rd * tPlane;
        float r = length(p.xz);
        // The kick's ripple ring travels outward as its envelope falls.
        float ringR = 0.6 + 6.0 * (1.0 - kick);
        float ring = exp(-pow((r - ringR) * 3.0, 2.0)) * kick;
        float lap = 0.015 * sin(p.x * 3.1 + t * 0.7) * sin(p.z * 2.7 - t * 0.5);
        vec2 radial = p.xz / max(r, 1e-3) * ring * 0.35;
        vec3 n = normalize(vec3(radial.x + lap, 1.0, radial.y + lap));
        vec3 rr = reflect(rd, n);
        float fres = 0.04 + 0.96 * pow(1.0 - max(dot(n, -rd), 0.0), 5.0);
        col = skyCol(rr, moonDir, hue) * (0.25 + 0.75 * fres);
        // The canopy's shade on the water, darkest under the trunk.
        col *= mix(0.35, 1.0, smoothstep(0.5, 3.5, r));
        col = mix(col, skyCol(rd, moonDir, hue) * 0.6, 1.0 - exp(-tPlane * 0.04));
    } else {
        col = skyCol(rd, moonDir, hue);
    }

    // Fireflies: points tested against the ray, hidden behind what it hit.
    float tHit = hitT > 0.0 ? hitT : min(tPlane, WL_FAR);
    float swarm = 2.2 + 1.3 * hat;
    vec3 flyCol = mix(vec3(0.75, 1.0, 0.45), pal(hue + 0.3), 0.3);
    for (int k = 0; k < WL_FIREFLIES; k++) {
        float fk = float(k);
        float a = fk * 2.39996 + t * (0.10 + 0.05 * hash11(fk));
        vec3 fp = vec3(cos(a), 0.0, sin(a)) * swarm * (0.6 + 0.6 * hash11(fk + 3.0));
        fp.y = WL_WATER + 0.6 + 2.0 * hash11(fk + 9.0) + 0.3 * sin(t * 0.7 + fk);
        vec3 w = fp - ro;
        float along = dot(w, rd);
        if (along < 0.0 || along > tHit) continue;
        float dist = length(w - rd * along);
        float twinkle = 0.6 + 0.4 * sin(t * (2.0 + hash11(fk + 5.0) * 3.0) + fk);
        col += flyCol * exp(-dist * dist * 600.0 / max(along * 0.15, 0.2)) * twinkle * 0.8;
    }

    col *= 0.85 + 0.2 * swell;
    col *= 0.75 + 0.25 * smoothstep(2.2, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
