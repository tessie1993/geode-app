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
// tendrils hanging down to touch the water, fireflies drifting through the
// curtain. Raymarched, orbited with camAround.
//
// The droop is the `bend` term of arborTree: every level tilts a little more
// than its parent, so by the fifth fork the limbs have turned past horizontal
// and hang. The long length ratio (0.8) keeps the hanging twigs long, which is
// what separates a willow from an oak. The pond is a plane, solved
// analytically rather than marched, and the tendrils are cut at its surface so
// they end ON the water.
//
// ---- audio ------------------------------------------------------------------
//
// Mid is the DROOP. uMidSmooth (slew-limited) adds to the per-level bend, so a
// rich mid range weighs the curtain down and a thin one lets it lift; the
// effect compounds down the levels, the trunk stays put and the hanging
// strands swing. Kick (uHits.x) sends a ripple ring out across the pond from
// the trunk, as a normal perturbation that only bends the moon's reflection.
// Hat (uHits.z) startles the fireflies: their swarm radius jumps out and
// settles back as the envelope falls. Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales every clock (breeze, water lap, fireflies). Complexity:
// uMorph adds up to one fold level (arbLevels). Chaos: uTurbulence and uWarp
// shake the twist per level into a ragged, wind-torn curtain; uTwist winds the
// whole canopy into a spiral. Colour: leaves, sky and fireflies from pal().
// Energy: uIntensity scales the mid droop, the kick ripple and the hat
// startle. Space: camAround, the rig's orbit and dolly.

#define WL_MAX_STEPS 112
#define WL_FAR 24.0
#define WL_WATER -2.3
#define WL_FIREFLIES 10

ArborForm gF;
float gGrow;

float map(vec3 p) {
    float tree = arborTree(p - vec3(0.0, WL_WATER - 0.05, 0.0), gF, gGrow);
    return max(tree, WL_WATER - p.y);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

vec3 skyCol(vec3 rd, vec3 moonDir, float hue) {
    float up = clamp(rd.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 c = mix(pal(hue) * 0.10 + vec3(0.01, 0.02, 0.03), pal(hue + 0.15) * 0.02 + vec3(0.004, 0.006, 0.02),
                 smoothstep(0.45, 1.0, up));
    float m = dot(rd, moonDir);
    c += vec3(0.9, 0.95, 1.0) * smoothstep(0.9993, 0.9996, m);
    c += vec3(0.35, 0.45, 0.55) * pow(max(m, 0.0), 60.0) * 0.5;
    return c;
}

void main() {
    vec2 uv = view();

    float swell = clamp(uSwell, 0.0, 1.5);
    float t = uTime * uSpeed;
    float energy = uIntensity;
    float chaos = arbChaos();
    float mid = clamp(uMidSmooth, 0.0, 1.5) * energy;
    float kick = clamp(uHits.x * energy, 0.0, 1.0);
    float hat = clamp(uHits.z * energy, 0.0, 1.0);

    gGrow = arbLevels(8.0, 1.0);
    float twist = 1.75 + 0.05 * sin(t * 0.23) + chaos * 0.35 * sin(t * 1.7) + 0.6 * uTwist;
    gF = ArborForm(2.0, 0.40, 0.17 + 0.13 * mid + 0.02 * sin(t * 0.4) + chaos * 0.04 * sin(t * 2.3),
                   twist, 0.80, 1.25, 0.075, 0.80);

    vec3 ro;
    vec3 rd;
    camAround(uv, 8.0, 3.0 * CAM_SPIN_UNIT, ro, rd);

    vec3 moonDir = normalize(vec3(-0.45, 0.42, 0.78));
    float hue = 0.48 + 0.05 * sin(t * 0.02);

    float tPlane = rd.y < -1e-4 ? (WL_WATER - ro.y) / rd.y : 1e9;
    float tMax = min(WL_FAR, tPlane);

    float tm = 0.05;
    float hitT = -1.0;
    float used = 0.0;
    float hitLevel = 0.0;
    for (int i = 0; i < WL_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        if (tm > tMax) break;
        used = float(i);
        float d = map(ro + rd * tm);
        float eps = 0.0008 * tm + 0.0005;
        if (d < eps) {
            hitT = tm;
            hitLevel = gArbLevel;
            break;
        }
        tm += d * 0.8;
    }

    vec3 col;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, 0.0012 * hitT + 0.0006);
        vec3 lit = arbLight(n, rd, moonDir, 0.5, 18.0);
        float ao = arbStepAO(used, uSteps);
        vec3 bark = vec3(0.09, 0.08, 0.075);
        vec3 leaf = mix(vec3(0.42, 0.55, 0.45), pal(hue + 0.1), 0.6);
        vec3 base = mix(bark, leaf, smoothstep(2.0, 5.5, hitLevel));
        col = base * (0.12 + 0.75 * lit.x) * ao;
        col += vec3(0.6, 0.7, 0.8) * lit.y * 0.25 * ao + vec3(lit.z) * 0.15;
        // Lower strands catch a little reflected moonlight off the pond.
        col += leaf * 0.12 * smoothstep(WL_WATER + 1.5, WL_WATER, p.y);
        col = mix(col, skyCol(rd, moonDir, hue), 1.0 - exp(-hitT * 0.045));
    } else if (tPlane < WL_FAR) {
        vec3 p = ro + rd * tPlane;
        float r = length(p.xz);
        // The kick's ripple ring travels outward as its envelope falls.
        float ringR = 0.6 + 6.0 * (1.0 - kick);
        float ring = exp(-pow((r - ringR) * 3.0, 2.0)) * kick;
        float lap = 0.015 * sin(p.x * 3.1 + t * 0.7) * sin(p.z * 2.7 - t * 0.5);
        vec3 n = normalize(vec3(p.x / max(r, 1e-3) * ring * 0.35 + lap, 1.0, p.z / max(r, 1e-3) * ring * 0.35 + lap));
        vec3 rr = reflect(rd, n);
        float fres = 0.04 + 0.96 * pow(1.0 - max(dot(n, -rd), 0.0), 5.0);
        col = skyCol(rr, moonDir, hue) * (0.25 + 0.75 * fres);
        // The canopy's shade on the water, darkest under the trunk.
        col *= mix(0.35, 1.0, smoothstep(0.5, 4.5, r));
        col = mix(col, skyCol(rd, moonDir, hue) * 0.6, 1.0 - exp(-tPlane * 0.05));
    } else {
        col = skyCol(rd, moonDir, hue);
    }

    // Fireflies: points tested against the ray, hidden behind what it hit.
    float tHit = hitT > 0.0 ? hitT : min(tPlane, WL_FAR);
    float swarm = 2.2 + 1.3 * hat;
    for (int k = 0; k < WL_FIREFLIES; k++) {
        float fk = float(k);
        float a = fk * 2.39996 + t * (0.10 + 0.05 * hash11(fk));
        float h = WL_WATER + 0.8 + 2.2 * hash11(fk + 9.0) + 0.3 * sin(t * 0.7 + fk);
        vec3 fp = vec3(cos(a), 0.0, sin(a)) * swarm * (0.6 + 0.6 * hash11(fk + 3.0));
        fp.y = h;
        vec3 w = fp - ro;
        float along = dot(w, rd);
        if (along < 0.0 || along > tHit) continue;
        float dist = length(w - rd * along);
        float twinkle = 0.6 + 0.4 * sin(t * (2.0 + hash11(fk + 5.0) * 3.0) + fk);
        col += mix(vec3(0.75, 1.0, 0.45), pal(hue + 0.3), 0.3) * exp(-dist * dist * 900.0 / max(along * 0.15, 0.2)) * twinkle * 0.8;
    }

    col *= 0.85 + 0.2 * swell;
    col *= 0.75 + 0.25 * smoothstep(2.2, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
