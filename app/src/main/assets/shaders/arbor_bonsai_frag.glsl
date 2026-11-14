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

// Arbor Bonsai: a gnarled little tree in a shallow tray, painted in ink on
// warm paper with a red seal of a sun behind it. Raymarched and orbited with
// camAround, but shaded as a brush painting: the light decides how much INK
// lands, not how bright a surface is, so the picture is paper minus ink, an
// inverted palette compared to every other Arbor style.
//
// The trunk is an arborTree with a slow sinuous warp through it (divided by
// its Lipschitz bound, 1.3) so it twists the way a wired bonsai does, and the
// foliage is clouded pads smooth-unioned onto every outermost tip through
// the hit's tip.
//
// ---- audio ------------------------------------------------------------------
//
// Sections SHAPE the tree. SND_CUT_COUNT hashes a new form for every section
// of the song: how many levels it has, how wide it forks, how hard it twists
// and how much it weeps, eased from the last section's form to the new one
// over the section envelope (SND_HELD_HARD_CUT) so the tree visibly re-grows into its
// new habit instead of snapping. Harmony (uHarmony) wets the brush: tonal
// music lays the foliage wash darker and softer, noisy music dry and pale.
// Kick (SND_KICK_ENV) nudges the pads up a little, a breath. Brightness follows
// SND_ENERGY_TREND only, and gently: this is paper.
//
// ---- macros -----------------------------------------------------------------
//
// Speed scales the pads' slow breath. Complexity: uMorph adds a level to every form (arbLevels).
// Chaos: uTurbulence and uWarp deepen the trunk's sinuous warp; uTwist adds a
// standing spiral to the forks. Colour: the ink and wash tints from pal(); the
// paper and the seal are fixed accents. Energy: uIntensity scales the harmony
// wash and the kick breath. Space: camAround, the rig's orbit and dolly.
// Knobs (lib_arbor's roles, offsets from 0.5 so off is the tuned look): k0
// PULSE the emission, k1 TEXTURE the limbs' girth, k2 FLOW added to the Chaos
// bend, k3 TONE the palette position of the ink and the wash, k4 BUILD how deep
// the wash lays (paper has no glow), k5 SPACE the reach of the orbit. The mids
// bow the limbs (arbMidBow).

#define BN_MAX_STEPS 104
#define BN_FAR 16.0
#define BN_BASE -1.15

ArborForm gF;
float gGrow;
float gPad;
float gGnarl;

vec4 formFor(float s) {
    float a = hash11(s * 3.1 + 0.7);
    float b = hash11(s * 5.3 + 1.9);
    float c = hash11(s * 7.7 + 4.1);
    // levels, tilt, twist, droop
    return vec4(5.0 + floor(a * 2.99), mix(0.45, 0.85, b), mix(1.2, 2.6, c), mix(-0.12, 0.28, fract(a * 9.1 + c)));
}

/** The world: the tree, its foliage pads and the tray. mat.x is the pad share (0 bark, 1 pad), mat.y 1 on the tray. */
ArborHit arborMap(vec3 p) {
    vec3 q = p - vec3(0.0, BN_BASE, 0.0);
    q.xz += gGnarl * vec2(sin(q.y * 2.3 + 0.5), cos(q.y * 1.9));
    ArborHit h = arborTree(q, gF, gGrow);
    float tree = h.d / (1.0 + 0.6 * gGnarl * 2.3);
    float pad = h.tip - gPad;
    float foliage = smin(tree, pad, 0.12);
    h.mat.x = smoothstep(-0.03, 0.03, tree - pad);
    vec3 b = p - vec3(0.0, BN_BASE - 0.16, 0.0);
    float tray = sdBox(b, vec3(1.05, 0.12, 0.62)) - 0.04;
    h.mat.y = step(tray, foliage);
    h.d = min(foliage, tray);
    return h;
}

//#include lib_arbor_field

void main() {
    vec2 uv = arbView();
    // The normalization shell (lib_arbor): every variable from its driver, mapped to its range here, once.
    ArborIn N = arborIn();
    float chaos = arbSpan(N.chaos, 0.0, 0.0, 2.0);
    float orbit = arbSpan(N.reach, 5.0, 4.0, 6.0);
    float padInkK = arbSpan(N.glow, 0.0, -0.1, 0.1);
    float emit = arbSpan(N.emit, 1.0, 0.75, 1.25);
    float hueShift = arbSpan(N.tone, 0.0, -0.3, 0.3);
    float padR = arbSpan(N.density, 0.15, 0.11, 0.19);
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)
    float energy = uIntensity;
    float swell = clamp(SND_ENERGY_TREND * uSoundDrive, 0.0, 1.5);
    float harmony = clamp(uHarmony, 0.0, 1.0) * energy;
    float kick = clamp(SND_KICK_ENV * uSoundDrive * energy, 0.0, 1.0);

    float morphIn = smoothstep(0.0, 1.0, 1.0 - clamp(SND_HELD_HARD_CUT, 0.0, 1.0));
    vec4 form = mix(formFor(SND_CUT_COUNT - 1.0), formFor(SND_CUT_COUNT), morphIn);
    gGrow = arbLevels(form.x, 1.0);
    gF = arborShape(ArborForm(2.0, form.y, form.w + arbMidBow(), form.z, 0.68, 0.72, 0.17, 0.68), N);
    gPad = padR + 0.02 * sin(t * 0.5) + 0.035 * kick;
    gGnarl = 0.10 + 0.12 * chaos;

    vec3 ro;
    vec3 rd;
    camAround(uv, orbit, 2.0 * CAM_SPIN_UNIT, ro, rd);

    // ---- paper, wash and seal ----------------------------------------------
    float grain = fbm3(vec3(uv * 18.0, 3.0), 3);
    vec3 paper = vec3(0.93, 0.89, 0.81) * (0.92 + 0.1 * grain);
    paper *= 1.0 - 0.10 * smoothstep(0.6, 2.0, length(uv));
    float seal = length(uv - vec2(0.20, 0.52)) - 0.17;
    vec3 sealCol = vec3(0.78, 0.16, 0.10);
    paper = mix(paper, mix(paper, sealCol, 0.85 * (0.85 + 0.15 * grain)), 1.0 - smoothstep(-0.004, 0.004, seal));

    float tm = 0.05;
    float hitT = -1.0;
    float used = 0.0;
    float isPad = 0.0;
    float isTray = 0.0;
    float level = 0.0;
    for (int i = 0; i < BN_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        if (tm > BN_FAR) break;
        used = float(i);
        ArborHit h = arborField(ro + rd * tm);
        float d = h.d;
        float eps = 0.0008 * tm + 0.0005;
        if (d < eps) {
            hitT = tm;
            isPad = h.mat.x;
            isTray = h.mat.y;
            level = h.level;
            break;
        }
        tm += d * 0.75;
    }

    vec3 col = paper;
    // A soft shadow wash on the paper under the tray.
    vec3 inkCol = pal(0.62 + hueShift) * 0.10 + vec3(0.03, 0.03, 0.04);
    vec3 washCol = mix(vec3(0.25, 0.32, 0.26), pal(0.30 + hueShift) * 0.5, 0.5);
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = arborNormal(p, 0.0012 * hitT + 0.0006);
        vec3 l = normalize(vec3(-0.5, 0.8, 0.4));
        vec3 lit = arbLight(n, rd, l, 0.3, 12.0);
        float ao = arbStepAO(used, uSteps);
        float brush = 0.85 + 0.3 * fbm3(p * 9.0, 2);
        float ink;
        vec3 tone;
        if (isTray > 0.5) {
            ink = 0.55 + 0.35 * (1.0 - lit.x);
            tone = mix(inkCol, vec3(0.35, 0.18, 0.12), 0.5);
        } else {
            float trunkInk = 0.80 + 0.15 * (1.0 - lit.x);
            float padInk = (0.25 + 0.25 * harmony + padInkK) + 0.35 * (1.0 - lit.x) + 0.45 * lit.y;
            ink = mix(trunkInk, padInk, isPad);
            tone = mix(inkCol, washCol, isPad);
        }
        ink = clamp(ink * brush * mix(1.0, ao, 0.6), 0.0, 1.0);
        col = mix(paper, tone, ink);
        // Dark ink outline where the surface turns away: the brush's edge.
        col = mix(col, inkCol, smoothstep(0.35, 0.8, lit.y) * (1.0 - 0.5 * isPad));
    }

    col *= 0.94 + 0.06 * swell;
    // k0 PULSE: the emission, as lib_flame's finish.
    col *= emit;
    fragColor = vec4(grade(col), 1.0);
}
