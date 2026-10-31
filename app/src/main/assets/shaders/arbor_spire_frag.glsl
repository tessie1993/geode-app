#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_arbor

// Arbor Spire: a tall neon conifer standing on a dark grid, its branches set
// round the trunk in a phyllotactic spiral, each branch a small fractal of
// its own, all of it outlined in glowing tube light. Raymarched, orbited with
// camAround.
//
// Phyllotaxis, not a fold, places the branches: branch k sits at height k*dy,
// turned k times the divergence angle round the trunk, shorter toward the
// top. At the golden angle (137.5 degrees) no two branches line up and the
// eye reads the famous crossing spirals; nudge the angle and the spirals
// wind into a few strong arms. Each branch is a short arborTree. A point only
// asks the branch slots at and just below its height (branches rise, so they
// reach up into the slots above them), four arborTree calls, not eighteen.
//
// ---- audio ------------------------------------------------------------------
//
// Timbre WINDS THE SPIRAL. The spectral centroid (uMotionBright, smoothed over
// half a second) bends the divergence angle off golden: a bright, airy mix
// winds the branches into one set of spiral arms, a dark, bassy one into the
// other, and the golden scatter lives in between. Because branch k turns by
// k times the angle, the top of the spire sweeps furthest. Kick (SND_KICK_ENV)
// lifts every branch toward the sky and lets it settle. Hat (SND_HAT_ENV) swells
// the neon beads on the twig tips. Brightness follows (0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)) only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the grid's scroll and the slow hue drift.
// Complexity: uMorph adds a fork level to every branch (arbLevels). Chaos:
// uTurbulence and uWarp jitter each branch's height and length from its index;
// uTwist adds a standing twist to every branch's fork planes. Colour: tube
// light from pal(), stepped by branch index. Energy: uIntensity scales the
// timbre wind, the kick lift and the hat beads. Space: camAround, the rig's
// orbit and dolly.

#define SP_MAX_STEPS 104
#define SP_FAR 22.0
#define SP_COUNT 15.0
#define SP_BOTTOM -2.8
#define SP_DY 0.40
#define SP_TOP (SP_BOTTOM + 0.5 + SP_COUNT * SP_DY)

ArborForm gF;
float gGrow;
float gDiv;
float gLift;
float gBead;
float gChaos;
float gIdx;
float gLevel;

float branchAt(vec3 p, float k) {
    float hk = hash11(k * 3.3 + 0.5);
    float y = SP_BOTTOM + 0.5 + k * SP_DY + (hk - 0.5) * 0.15 * gChaos;
    float frac = k / SP_COUNT;
    float len = mix(1.45, 0.28, frac) * (1.0 + (hk - 0.5) * 0.4 * gChaos);
    vec3 q = p - vec3(0.0, y, 0.0);
    q.xz = rot2(k * gDiv) * q.xz;
    q.xy = rot2(-(1.32 - gLift)) * q.xy;
    float sc = len / 0.75;
    q /= sc;
    float tree = arborTree(q, gF, gGrow);
    float bead = gArbTip - gBead / sc;
    return min(tree, bead) * sc;
}

float map(vec3 p) {
    float hy = clamp((p.y - SP_BOTTOM) / (SP_TOP - SP_BOTTOM), 0.0, 1.0);
    float trunk = length(p.xz) - mix(0.11, 0.015, hy);
    trunk = max(trunk, max(SP_BOTTOM - p.y, p.y - SP_TOP));
    float k0 = floor((p.y - SP_BOTTOM - 0.5) / SP_DY + 0.5);
    float d = trunk;
    gIdx = -1.0;
    gLevel = 0.0;
    for (int j = -2; j <= 1; j++) {
        float k = k0 + float(j);
        if (k < 0.0 || k > SP_COUNT - 1.0) continue;
        float b = branchAt(p, k);
        if (b < d) {
            d = b;
            gIdx = k;
            gLevel = gArbLevel;
        }
    }
    return d;
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
    float swell = clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    float bright = clamp(uMotionBright, 0.0, 1.0);
    float kick = clamp(SND_KICK_ENV * energy, 0.0, 1.0);
    float hat = clamp(SND_HAT_ENV * energy, 0.0, 1.0);

    gChaos = arbChaos();
    gDiv = 2.39996 + 0.16 * (bright - 0.5) * 2.0 * energy;
    gLift = 0.30 * kick;
    gBead = 0.035 + 0.035 * hat;
    gGrow = arbLevels(4.0, 1.0);
    gF = ArborForm(2.0, 0.55, 0.0, 1.57 + 0.6 * uTwist, 0.66, 0.75, 0.07, 0.66);

    vec3 ro;
    vec3 rd;
    camAround(uv, 7.5, 4.0 * CAM_SPIN_UNIT, ro, rd);

    float hue = 0.80 + 0.002 * t;

    float tPlane = rd.y < -1e-4 ? (SP_BOTTOM - ro.y) / rd.y : 1e9;
    float tMax = min(SP_FAR, tPlane);

    float tm = 0.05;
    float hitT = -1.0;
    float used = 0.0;
    float idx = 0.0;
    float level = 0.0;
    vec3 glow = vec3(0.0);
    for (int i = 0; i < SP_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        if (tm > tMax) break;
        used = float(i);
        float d = map(ro + rd * tm);
        float eps = 0.0008 * tm + 0.0005;
        if (d < eps) {
            hitT = tm;
            idx = gIdx;
            level = gLevel;
            break;
        }
        // Tube light bleeding into the dark around every limb.
        glow += pal(hue + max(gIdx, 0.0) * 0.045) * exp(-d * 16.0) * 0.012;
        tm += d * 0.8;
    }

    vec3 col = vec3(0.004, 0.003, 0.010);
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, 0.001 * hitT + 0.0005);
        vec3 lit = arbLight(n, rd, normalize(vec3(0.3, 1.0, 0.2)), 0.2, 30.0);
        float ao = arbStepAO(used, uSteps);
        vec3 tube = pal(hue + max(idx, 0.0) * 0.045 + level * 0.06);
        // Neon: the core of every limb dark, its silhouette edge blazing.
        col = tube * (0.08 + 0.15 * lit.x) * ao + tube * lit.y * 1.4 + vec3(lit.z) * 0.2;
        if (idx < 0.0) col = tube * (0.05 + 0.9 * lit.y);
    } else if (tPlane < SP_FAR) {
        vec3 p = ro + rd * tPlane;
        vec2 g = abs(fract(p.xz * 1.0 + vec2(0.0, t * 0.05)) - 0.5);
        float line = 1.0 - smoothstep(0.0, 0.03 + 0.004 * tPlane, min(g.x, g.y));
        col = pal(hue + 0.4) * line * 0.25 * exp(-tPlane * 0.12);
        // The spire's own light pooled on the floor.
        col += pal(hue) * 0.12 * exp(-length(p.xz) * 0.9);
    }
    col += glow * (0.8 + 0.3 * swell);

    col *= 0.85 + 0.2 * swell;
    col *= 0.75 + 0.25 * smoothstep(2.2, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
