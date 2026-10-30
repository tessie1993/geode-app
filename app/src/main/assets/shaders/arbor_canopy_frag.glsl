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

// Arbor Canopy: flying down an endless avenue of old trees whose crowns lean
// in and lace together overhead into a green tunnel, afternoon sun breaking
// through in shafts and dapples on the path. Raymarched, flown with camFly.
//
// One tree is drawn: the plane is mirrored across x = 0 so there are two
// rows, and repeated along z with period 8 (a power of two, so the rig's
// 8192 travel wrap is invisible; the per-tree hash key wraps at 64 cells for
// the same reason). Each trunk leans inward, so the crowns of the two rows
// meet above the path. Leaf clusters are smooth-unioned onto every outermost
// tip through gArbTip. A tube around the travel axis and a bubble around the
// eye are carved out of the field so the camera path never flies into bark.
//
// ---- audio ------------------------------------------------------------------
//
// Snare (uHits.y) RE-ANGLES THE FORKS: on every snare the fork angle of every
// level opens out at once and springs back as the envelope falls, so the
// whole tunnel flexes wider in time with the backbeat, the crowns parting and
// closing over the path. The key (uKeyHue, gated by uKeyStrength) turns the
// leaves' season along the palette. Swell (uSwell) gently deepens the sun
// shafts, nothing on a hit.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the dapple drift and the leaves' stir. Complexity:
// uMorph adds a fork level (arbLevels). Chaos: uTurbulence and uWarp scatter
// each tree's twist and lean from its hash; uTwist adds a standing spiral to
// every crown. Colour: bark, leaves, light and haze from pal(). Energy:
// uIntensity scales the snare flex and the key's pull. Space: camFly, the
// rig's path.

#define CN_MAX_STEPS 112
#define CN_FAR 30.0
#define CN_PERIOD 8.0
#define CN_ROW 3.5
#define CN_GROUND -3.0

ArborForm gF;
float gGrow;
float gChaos;
float gStir;
vec3 gEye;
float gMat;

float map(vec3 p) {
    float side = step(0.0, p.x);
    vec3 q = vec3(abs(p.x), p.y, p.z);
    float cell = floor(q.z / CN_PERIOD + 0.5);
    float h = hash11(mod(cell, 64.0) * 7.7 + side * 31.0);
    q.z -= cell * CN_PERIOD + (h - 0.5) * 1.5;
    q -= vec3(CN_ROW, CN_GROUND, 0.0);
    float lean = 0.62 + 0.08 * (h - 0.5) * (1.0 + 2.0 * gChaos);
    q.xy = rot2(lean) * q.xy;
    q.xz = rot2(h * 6.2831) * q.xz;

    ArborForm f = gF;
    f.twist += (h - 0.5) * (0.4 + 1.2 * gChaos);
    float tree = arborTree(q, f, gGrow);
    float leaves = gArbTip - 0.15 - 0.025 * sin(gStir + h * 20.0);
    float crown = smin(tree, leaves, 0.12);
    gMat = smoothstep(-0.05, 0.05, tree - leaves);

    float ground = p.y - CN_GROUND;
    if (ground < crown) gMat = -1.0;
    float d = min(crown, ground);
    d = smax(d, 1.9 - length(p.xy - vec2(0.0, -0.6)), 0.6);
    return smax(d, 0.6 - length(p - gEye), 0.3);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

void main() {
    vec2 uv = arbView();
    float t = uTime * uSpeed;
    float energy = uIntensity;
    float swell = clamp(uSwell, 0.0, 1.5);
    float snare = clamp(uHits.y * energy, 0.0, 1.0);
    float key = uKeyHue * clamp(uKeyStrength * energy, 0.0, 1.0);

    gChaos = arbChaos();
    gStir = t * 1.3;
    gGrow = arbLevels(8.0, 1.0);
    gF = ArborForm(2.0, 0.46 + 0.30 * snare, 0.04, 1.57 + 0.5 * uTwist, 0.74, 2.3, 0.075, 0.74);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    gEye = ro;

    float hue = 0.22 + 0.25 * key;
    vec3 sunDir = normalize(vec3(0.25, 0.85, 0.45));
    vec3 haze = mix(pal(hue - 0.1) * 0.26, vec3(0.50, 0.46, 0.28), 0.4);

    float tm = 0.05;
    float hitT = -1.0;
    float used = 0.0;
    float mat = 0.0;
    float level = 0.0;
    for (int i = 0; i < CN_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        if (tm > CN_FAR) break;
        used = float(i);
        float d = map(ro + rd * tm);
        float eps = 0.0008 * tm + 0.0006;
        if (d < eps) {
            hitT = tm;
            mat = gMat;
            level = gArbLevel;
            break;
        }
        tm += d * 0.75;
    }

    // Sky glimpsed through the canopy: bright toward the sun.
    float sunAmt = pow(max(dot(rd, sunDir), 0.0), 6.0);
    vec3 col = haze * (0.8 + 0.6 * sunAmt);
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, 0.0015 * hitT + 0.0008);
        vec3 lit = arbLight(n, rd, sunDir, 0.5, 16.0);
        float ao = arbStepAO(used, uSteps);
        // Dapples: the sun only gets through where the leaf noise opens.
        float dap = smoothstep(0.42, 0.62, fbm3(vec3(p.xz * 1.4, t * 0.05), 2));
        vec3 base;
        if (mat < -0.5) {
            float path = smoothstep(1.9, 1.3, abs(p.x));
            base = mix(mix(vec3(0.16, 0.22, 0.08), pal(hue) * 0.3, 0.4), vec3(0.42, 0.34, 0.24), path);
            col = base * (0.35 + 0.7 * dap * lit.x) * ao;
        } else {
            vec3 bark = mix(vec3(0.20, 0.15, 0.11), pal(hue + 0.5) * 0.2, 0.25);
            vec3 leaf = mix(pal(hue + 0.05 * sin(p.y * 2.0)), vec3(0.30, 0.50, 0.16), 0.35);
            base = mix(bark, leaf, max(mat, smoothstep(5.0, 7.0, level)));
            col = base * (0.18 + 0.9 * lit.x * (0.4 + 0.6 * dap)) * ao;
            // Leaves glow through with the sun behind them.
            col += leaf * mat * pow(max(dot(rd, sunDir), 0.0), 3.0) * 0.35;
            col += haze * lit.y * 0.15;
        }
        col = mix(col, haze, 1.0 - exp(-hitT * 0.045));
    }
    // Sun shafts: soft bright streaks in the haze toward the sun.
    float shafts = pow(max(dot(rd, sunDir), 0.0), 3.0) * (0.55 + 0.45 * sin(rd.x * 37.0 + rd.z * 11.0 + t * 0.2));
    col += haze * shafts * (0.12 + 0.1 * swell);

    col *= 0.85 + 0.2 * swell;
    col *= 0.75 + 0.25 * smoothstep(2.2, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
