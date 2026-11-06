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

// Arbor Rosette: a nave of stained-glass rose windows, flown through. Each
// window is the old flat rosette given body: a forest folded through a
// seven-way kaleidoscope, one ring of trees growing outward from a glowing
// glass hoop, a second ring growing inward from the stone rim between them,
// their twigs lacing into tracery where they meet the wedge mirrors. Every
// limb is now a slab of glass with rounded edges and a lead came round it,
// lit from behind by the light down the nave, with jewels on the twig tips
// and fog between the windows. The hoop at each window's heart is open, and
// the camera flies through the holes, window after window.
//
// Both rings are still arborTree2 in the frame of one mirror wedge (dihedral,
// so a tree crossing the wedge edge meets its own reflection). Each fold level
// keeps its own jewel colour, a step along the palette: the depth record
// gArbLevel IS the colour index, as before.
//
// ---- camera ------------------------------------------------------------------
//
// camFly down +z. Windows stand every RS_PERIOD (16) world units; 16 divides
// the rig's 8192 travel wrap, so the eye's z is rebased by mod(z, 16) and
// neither the rebase nor the wrap shows. Per-window variety (turn direction,
// jitter seed, a small hue step) comes from the window's unwrapped index, read
// only mod 2 and mod 8, both of which divide the 512 windows in one wrap, so
// the window ahead keeps its look across the wrap too. The hole is
// RS_INNER * RS_SCALE = 3.2 wide in radius, past the rig's +-2 wander; on wide
// glide paths the eye can still cross glass, so a small bubble round the eye
// is carved out of the field and the eye never sits inside a limb. A nave
// wall (an analytic cylinder) bounds every ray that does not leave through
// the far end.
//
// ---- audio ------------------------------------------------------------------
//
// Pitch changes WALK THE HUES, as in the flat style. The pitch-change envelope
// (SND_HELD_NOTE_CHANGE) widens the hue step between fold levels: on a melodic
// move each window's colours fan out across the palette from trunk to twig and
// gather back toward one hue as the envelope falls. The key (uKeyHue, gated by
// uKeyStrength) sets the base hue. Hat (SND_HAT_ENV) swells the jewels on the
// twig tips. Swell (SND_LEVEL) lifts the backlight a little. The rig's own
// kick punches and drop pushes lurch the flight.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uTime turns every window (neighbours turn opposite ways) and drifts
// the hue. Complexity: uMorph adds a fork level to both rings (arbLevels, cut
// for the Detail budget). Chaos: uTurbulence and uWarp add per-branch jitter,
// the tracery growing irregular. Colour: every jewel and the light from pal().
// Energy: uIntensity scales the pitch walk, the key's pull and the hat jewels.
// Space: the camera rig.
// Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE the jewel
// swell, k1 TEXTURE the lead's width, k2 FLOW the light bleeding round the
// glass, k3 TONE the hue, k4 BUILD the backlight, k5 SPACE the fog.
//
// ---- why the distance estimate is safe ----------------------------------------
//
// The wedge fold and the window's turn are isometries, and arborTree2 is a
// lower bound in the units of its point (each fold a reflection or rotation
// plus a uniform scale, the limb distance multiplied back by that scale), so
// times RS_SCALE it is a world-unit bound in the window plane. A rounded
// extrusion of a 2D bound along z, a torus and a sphere round a tip are all
// 1-Lipschitz, and min/max keep that. The one gap is the family's: arborTree2
// only sees the subtree on its own side of a fork, so near a fold line it can
// overshoot a twig across it; the march steps at 0.75 of the estimate, as the
// marched Arbor styles do. Far from a window plane (|z| > RS_SKIP) nothing of
// the window exists past RS_REACH, so the field returns that slab distance
// without walking the trees: a bound, and the main saving of the march.

#define RS_MAX_STEPS 104
#define RS_FOLDS 7.0
/** Window units: the hoop the outward trees grow from, and the rim the inward ones hang from. */
#define RS_INNER 0.36
#define RS_OUTER 0.98
/** World units per window unit: a window is ~18 across. */
#define RS_SCALE 9.0
/** World units between windows; divides 8192. */
#define RS_PERIOD 16.0
/** Half-thickness of a glass slab and the rounding of its edge, world units. */
#define RS_THICK 0.035
#define RS_ROUND 0.03
#define RS_HOOP 0.11
#define RS_RIM 0.16
/** Every part of a window lies within RS_REACH of its plane; past RS_SKIP the trees are not walked. */
#define RS_REACH 0.2
#define RS_SKIP 0.7
#define RS_BUBBLE 0.45
#define RS_NAVE 10.5
#define RS_FAR 56.0

ArborForm2 gFo;
ArborForm2 gFi;
float gGrowOut;
float gGrowIn;
float gTurn;
float gCellBase;
float gJewel;
vec3 gEye;
/** Set by map(): which part (0 outward glass, 1 inward glass, 2 jewel, 3 hoop, 4 rim), its fold level, the 2D distance to the glass silhouette (world), the window id. */
float gPart;
float gLvl;
float gD2;
float gId;

vec2 wedge(vec2 p) {
    float w = ARB_TAU / RS_FOLDS;
    float a = atan(p.y, p.x);
    a = abs(mod(a + 0.5 * w, w) - 0.5 * w);
    return length(p) * vec2(cos(a), sin(a));
}

/** A 2D shape of distance d2 extruded to half-thickness h along z, its edge rounded by RS_ROUND without growing its footprint. */
float slab(float d2, float z, float h) {
    vec2 e = vec2(d2 + RS_ROUND, abs(z) - h);
    return min(max(e.x, e.y), 0.0) + length(max(e, 0.0)) - RS_ROUND;
}

float map(vec3 p) {
    float zc = p.z + 0.5 * RS_PERIOD;
    float cell = floor(zc / RS_PERIOD);
    float zl = zc - cell * RS_PERIOD - 0.5 * RS_PERIOD;
    float id = gCellBase + cell;
    gId = id;
    float carve = RS_BUBBLE - length(p - gEye);
    gPart = 4.0;
    gLvl = 0.0;
    gD2 = 0.0;
    if (abs(zl) > RS_SKIP) return max(abs(zl) - RS_REACH, carve);

    float rw = length(p.xy);
    float hoop = length(vec2(rw - RS_INNER * RS_SCALE, zl)) - RS_HOOP;
    float rim = length(vec2(rw - (RS_OUTER + 0.03) * RS_SCALE, zl)) - RS_RIM;

    float dir = mod(id, 2.0) < 0.5 ? 1.0 : -1.0;
    float seed = mod(id, 8.0) * 3.7 + 1.0;
    vec2 w = rot2(gTurn * dir) * p.xy / RS_SCALE;

    // Outward ring, from the hoop.
    vec2 q = wedge(w);
    float dOut = arborTree2(vec2(q.y, q.x - RS_INNER), gFo, gGrowOut, seed) * RS_SCALE;
    float lOut = gArbLevel;
    float tOut = gArbTip * RS_SCALE;

    // Inward ring, half a wedge round, from the rim.
    vec2 q2 = wedge(rot2(ARB_TAU / RS_FOLDS * 0.5) * w);
    float dIn = arborTree2(vec2(q2.y, RS_OUTER - q2.x), gFi, gGrowIn, seed + 2.0) * RS_SCALE;
    float lIn = gArbLevel;
    float tIn = gArbTip * RS_SCALE;

    float gOut = slab(dOut, zl, RS_THICK);
    float gIn = slab(dIn, zl, RS_THICK);
    float jewel = length(vec2(min(tOut, tIn), zl)) - gJewel;

    float d = rim;
    if (hoop < d) {
        d = hoop;
        gPart = 3.0;
    }
    if (gIn < d) {
        d = gIn;
        gPart = 1.0;
        gLvl = lIn;
        gD2 = dIn;
    }
    if (gOut < d) {
        d = gOut;
        gPart = 0.0;
        gLvl = lOut;
        gD2 = dOut;
    }
    if (jewel < d) {
        d = jewel;
        gPart = 2.0;
    }
    return max(d, carve);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

void main() {
    vec2 uv = arbView();
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)
    float energy = uIntensity;
    float chaos = arbChaos();
    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    float pitch = clamp(SND_HELD_NOTE_CHANGE * energy, 0.0, 1.0);
    float hat = clamp(SND_HAT_ENV * energy, 0.0, 1.0);
    float key = uKeyHue * clamp(uKeyStrength * energy, 0.0, 1.0);

    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kFlow = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    float kSpace = clamp(uKnob[5], 0.0, 1.0) - 0.5;

    float hue = key + 0.01 * t + 0.3 * kTone;
    float stepHue = 0.05 + 0.16 * pitch;

    float jitter = 0.08 + 0.45 * chaos;
    gFo = ArborForm2(vec2(0.44, 0.44), vec2(0.70, 0.70), 0.0, jitter, 0.12, 0.10, 0.72);
    gFi = ArborForm2(vec2(0.58, 0.58), vec2(0.66, 0.66), 0.0, jitter, 0.12, 0.09, 0.70);
    gGrowOut = arbLevels(7.0, 1.0);
    gGrowIn = arbLevels(6.0, 1.0);
    gTurn = t * 0.04;
    gJewel = (0.005 + 0.008 * hat * (1.0 + 1.6 * kPulse)) * RS_SCALE;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    gCellBase = floor(ro.z / RS_PERIOD);
    // 16 divides the 8192 wrap, so the rebase is exact and invisible, and it
    // keeps highp z's fraction intact far down the travel.
    ro.z = mod(ro.z, RS_PERIOD);
    gEye = ro;

    // The nave wall bounds the march from inside.
    float tWall = 1e9;
    float a = dot(rd.xy, rd.xy);
    float b = dot(ro.xy, rd.xy);
    float c = dot(ro.xy, ro.xy) - RS_NAVE * RS_NAVE;
    if (c < 0.0 && a > 1e-6) tWall = (-b + sqrt(max(b * b - a * c, 0.0))) / a;
    float tMax = min(RS_FAR, tWall);

    float flow = 1.0 + 1.4 * kFlow;
    float tm = 0.05;
    float hitT = -1.0;
    float used = 0.0;
    float part = 4.0;
    float lvl = 0.0;
    float d2 = 0.0;
    float id = 0.0;
    vec3 glow = vec3(0.0);
    for (int i = 0; i < RS_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (tm > tMax) break;
        used = float(i);
        float d = map(ro + rd * tm);
        float eps = 0.0007 * tm + 0.0005;
        if (d < eps) {
            hitT = tm;
            part = gPart;
            lvl = gLvl;
            d2 = gD2;
            id = gId;
            break;
        }
        // Light through the glass bleeding into the haze round every limb.
        if (gPart < 2.5) {
            glow += pal(hue + 0.1 + gLvl * stepHue) * exp(-d * 9.0) * 0.010 * exp(-tm * 0.03);
        }
        tm += max(d * 0.75, eps);
    }

    // The nave's haze, brightest straight ahead where the light comes down
    // through the open hearts of the windows.
    float ahead = pow(max(rd.z, 0.0), 10.0);
    float back = (1.0 + 1.2 * kBuild) * (0.9 + 0.2 * swell);
    vec3 fogCol = pal(hue + 0.5) * (0.025 + 0.12 * ahead * back) + vec3(0.003, 0.003, 0.006);
    float fog = 0.035 * (1.0 + 1.4 * kSpace);
    vec3 col = fogCol;
    vec3 lightDir = normalize(vec3(0.25, 0.6, 1.0));

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, 0.0008 * hitT + 0.0006);
        float ao = arbStepAO(used, uSteps);
        vec3 lit = arbLight(n, rd, lightDir, 0.3, 40.0);
        float r = length(p.xy) / RS_SCALE;
        float winHue = hue + 0.04 * mod(id, 8.0);
        // Backlight behind the glass: brightest near each window's heart, as
        // the flat window glowed at its centre.
        float bl = (0.45 + 0.55 * smoothstep(1.2, 0.2, r)) * back;
        if (part < 1.5) {
            vec3 g = pal(winHue + (part > 0.5 ? 0.33 : 0.0) + lvl * stepHue);
            float core = smoothstep(0.0, -0.012 * RS_SCALE, d2);
            col = g * (0.35 + 0.65 * core) * bl * (0.7 + 0.3 * lit.x) + vec3(lit.z) * 0.25;
            // Lead came: a dark metal edge round every piece.
            float leadW = 0.04 * (1.0 + 1.2 * kTexture);
            float lead = smoothstep(-leadW, -0.35 * leadW, d2);
            vec3 metal = vec3(0.012) + vec3(0.16) * lit.z + pal(winHue + 0.5) * 0.04 * lit.y;
            col = mix(col, metal, lead * 0.9);
        } else if (part < 2.5) {
            vec3 j = pal(winHue + 0.5 + stepHue * 4.0) * 1.3;
            col = j * (0.6 + 0.6 * lit.x) + vec3(lit.z) * 0.6 + j * lit.y * 0.6;
        } else if (part < 3.5) {
            // The hoop: the flat window's hub, now a ring of lit glass.
            vec3 h = pal(winHue);
            col = h * (0.5 + 0.4 * lit.x) * back + h * lit.y * 0.8 + vec3(lit.z) * 0.3;
        } else {
            // Stone rim with a thin line of light round it.
            col = vec3(0.02, 0.018, 0.024) * (0.4 + lit.x) * ao + vec3(lit.z) * 0.08
                + pal(winHue + 0.55) * 0.12 * lit.y;
        }
        col *= mix(1.0, ao, 0.5);
        col = mix(col, fogCol, 1.0 - exp(-hitT * fog));
    } else if (tWall < RS_FAR) {
        // The nave wall: dark stone, washed by the colour of the nearest window.
        vec3 p = ro + rd * tWall;
        float zl = mod(p.z + 0.5 * RS_PERIOD, RS_PERIOD) - 0.5 * RS_PERIOD;
        float ang = atan(p.y, p.x);
        float rib = 1.0 - smoothstep(0.0, 0.05, abs(fract(ang * (14.0 / ARB_TAU)) - 0.5) - 0.42);
        float wash = exp(-abs(zl) * 0.35);
        col = vec3(0.012, 0.011, 0.016) * (1.0 + rib)
            + pal(hue + 0.2) * 0.07 * wash * back;
        col = mix(col, fogCol, 1.0 - exp(-tWall * fog));
    }
    col += glow * flow * (0.8 + 0.3 * swell);

    col *= 0.78 + 0.22 * smoothstep(2.2, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
