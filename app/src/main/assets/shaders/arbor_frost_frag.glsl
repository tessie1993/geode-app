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

// Arbor Frost: one great snow crystal hanging in the air in front of you, a
// real slab of ice now, growing its dendrites out of a hexagonal plate, while
// smaller flakes fall through the dark behind it. Raymarched, seen with
// camAround: the rig's look and dolly turn you about the crystal, which sways
// gently on its own and turns slowly about its axis, so its faceted faces
// catch the light.
//
// The crystal is still the 2D dendrite (arborDendrite2, the three-way fork of
// straight on plus a mirrored pair of side arms, folded into the twelve mirror
// wedges of a hexagon, D6), now EXTRUDED into a thin slab whose faces rise to
// a ridge along every limb's spine, the way a real dendrite is thickest down
// its middle. So each arm is a low prism with two sloped facets, and the
// central plate a shallow hexagonal pyramid. Ice is shaded with a fresnel rim,
// a fake refraction whose three channels read the palette at staggered
// offsets (dispersion), a specular glint per facet and a bright spine.
//
// The falling flakes behind are drawn, not marched: the ray meets three
// cylinders about the crystal's vertical axis (radius 7, 10 and 15, the eye
// always inside the first), and each cylinder is tiled with flake sprites
// that fall down it and turn. The eye is inside every cylinder, so the ray
// hits each exactly once, and they give real parallax when the rig turns.
//
// The crystal sits at the origin, so the rig's 8192 travel wrap never meets
// it; camAround's spin is zero (the sway is the crystal's own), so nothing
// depends on uCamTravel.
//
// ---- audio ------------------------------------------------------------------
//
// Treble is GROWTH. SND_TREBLE_PRESENCE (slew-limited: the air end of the
// spectrum, without a frame of jitter) sets how many levels of the dendrite
// exist, fractionally, so a bright passage grows the crystal out to its fine
// feathered tips and a dull one melts it back to a plate with stubs. Each
// section (SND_CUT_COUNT) picks a new crystal HABIT, side-arm angle and the
// two length ratios, hashed from the count and morphed in over the section
// envelope so it never snaps. Kick (SND_KICK_ENV) pulses the central plate's
// size, which pushes the arms out with it. Brightness follows SND_INTENSITY
// only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uTime turns and sways the crystal, drifts the flakes and the
// sparkle. Complexity: uMorph adds a level of growth (arbLevels) and the
// background flakes' detail. Chaos: uTurbulence and uWarp etch a fine
// irregular warp into the crystal's edges. Twist (uTwist) winds the arms into
// a pinwheel, more at the rim. Colour: ice, glow and sky from
// pal(). Energy: uIntensity scales the treble growth and the kick pulse.
// Space: camAround, the rig's look and dolly. Knobs (lib_arbor's roles, offsets
// from 0.5 so off is the tuned look): k0 PULSE the emission, k1 TEXTURE the
// dendrite's girth, k2 FLOW added to the Chaos bend, k3 TONE the palette
// position, k4 BUILD the glow round the limbs, k5 SPACE the reach through the
// sky's haze. The mids open the side arms' angle a little (arbMidBow), which
// moves no tip past CF_BOUND.
//
// ---- why the estimate is safe ----------------------------------------------
//
// The 2D fold is reflections, rotations and uniform scales, so arborDendrite2
// is a lower bound (lib_arbor's header), and the hex fold is a reflection.
// Chaos's sine warp of the plane has a slope of at most 0.012 * 47 per unit
// chaos, so the 2D distance is divided by 1 + 0.6 * chaos. The ridge plane
// |z| - th - RIDGE * min(-d2, cap) has gradient length at most sqrt(1 +
// RIDGE^2) and is divided by it; the max of two 1-Lipschitz fields is one.
// The crystal's rotation is rigid. Past the bounding sphere (radius CF_BOUND,
// above the largest habit plus the kick and chaos) the march never starts, and
// the steps are 0.75 of the estimate to hide the fold's rare overshoot.

#define CF_MAX_STEPS 88
#define CF_ARMS 6.0
#define CF_LEN 0.12
/** World units per crystal (2D) unit. */
#define CF_SCALE 2.4
/** Bounding sphere radius, world: the largest habit reaches 0.66 crystal units. */
#define CF_BOUND 1.72
/** Slab half-thickness at a limb's edge, world. */
#define CF_TH 0.018
/** Ridge slope: extra half-thickness per unit of depth inside the outline. */
#define CF_RIDGE 0.38
/** Depth past which the ridge stops rising (the plate's pyramid is capped). */
#define CF_RIDGE_CAP 0.16
#define CF_EYE 5.0

vec3 gHab;
float gGrow;
float gPlateR;
float gChaos;
/** k1 TEXTURE on the dendrite's girth. */
float gGirth;
float gT;
mat2 gYaw;
mat2 gPitch;
mat2 gRoll;
/** Read right after map(): the crystal-plane point and its 2D distance (crystal units). */
vec2 gC;
float gD2;
float gPlate;

vec3 habit(float s) {
    float h = hash11(s * 3.7 + 1.0);
    float h2 = hash11(s * 5.1 + 2.0);
    return vec3(mix(0.85, 1.25, h), mix(0.72, 0.80, h2), mix(0.38, 0.60, fract(h * 7.0 + h2)));
}

/** Fold the plane into one mirror wedge of the hexagon, arm along +x. */
vec2 hexFold(vec2 p) {
    float w = ARB_TAU / CF_ARMS;
    float a = atan(p.y, p.x);
    a = abs(mod(a + 0.5 * w, w) - 0.5 * w);
    return length(p) * vec2(cos(a), sin(a));
}

/** The crystal's outline in its own plane, crystal units. Writes gArb*, gPlate. */
float crystal2(vec2 p) {
    p += gChaos * 0.012 * vec2(sin(p.y * 47.0 + gT), cos(p.x * 43.0 - gT * 0.7));
    float r = length(p);
    p = rot2(3.0 * uTwist * r) * p;
    vec2 q = hexFold(p);
    float plate = q.x - gPlateR;
    float arm = arborDendrite2(vec2(q.y, q.x - gPlateR * 0.6), gHab.x, gHab.y, gHab.z, CF_LEN, 0.10 * gGirth, gGrow);
    gPlate = plate - arm;
    return min(plate, arm) / ((1.0 + 0.6 * gChaos) * (1.0 + 3.0 * abs(uTwist) * r));
}

/** World point into the crystal's frame: the sway, then the slow turn about its axis. */
vec3 toCrystal(vec3 p) {
    p.xz = gYaw * p.xz;
    p.yz = gPitch * p.yz;
    p.xy = gRoll * p.xy;
    return p;
}

float map(vec3 p) {
    vec3 q = toCrystal(p);
    vec2 c = q.xy / CF_SCALE;
    float d2 = crystal2(c);
    gC = c;
    gD2 = d2;
    float dw = d2 * CF_SCALE;
    float depth = min(max(-dw, 0.0), CF_RIDGE_CAP);
    float slab = (abs(q.z) - CF_TH - CF_RIDGE * depth) / sqrt(1.0 + CF_RIDGE * CF_RIDGE);
    return max(dw, slab);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

vec3 skyColour(vec3 rd, float hue) {
    return mix(pal(hue) * 0.10 + vec3(0.01, 0.015, 0.04), pal(hue + 0.1) * 0.03, smoothstep(-1.0, 1.0, rd.y));
}

/**
 * One layer of falling flakes on the cylinder of radius R about +y. The eye is
 * inside it, so the far root is the one hit. Returns the flakes' coverage.
 */
float flakeLayer(vec3 ro, vec3 rd, float R, float cells, float seed, out float tHit) {
    float a = max(dot(rd.xz, rd.xz), 1e-4);
    float b = dot(ro.xz, rd.xz);
    float c = dot(ro.xz, ro.xz) - R * R;
    float tk = (-b + sqrt(max(b * b - a * c, 0.0))) / a;
    tHit = tk;
    vec3 h = ro + rd * tk;
    // u runs once round the cylinder in a whole number of cells, so the
    // atan seam at -pi is a cell wall; v falls with time.
    float cellW = ARB_TAU * R / cells;
    vec2 g = vec2((atan(h.z, h.x) / ARB_TAU + 0.5) * cells, h.y / cellW + gT * (0.19 + 0.05 * seed));
    const float cellSize = 0.32;
    float px = min(max(length(fwidth(g * cellSize)), 1e-4), 0.05);
    vec2 cell = floor(g);
    cell.x = mod(cell.x, cells);
    float hc = hash13(vec3(cell, 4.0 + seed));
    vec2 lp = (fract(g) - 0.5) * cellSize;
    lp -= (vec2(hash13(vec3(cell, 5.0 + seed)), hash13(vec3(cell, 6.0 + seed))) - 0.5) * cellSize * 0.5;
    lp = rot2(gT * (0.3 + hc) + hc * 6.0) * lp;
    float sz = 0.12 + 0.12 * hc;
    vec2 fq = hexFold(lp / sz);
    float fd = arborDendrite2(vec2(fq.y, fq.x - 0.08), 1.05, 0.75, 0.5, 0.18, 0.12, arbLevels2(3.0, 1.0)) * sz;
    return arbInk(fd, px) * step(0.55, hc);
}

void main() {
    vec2 uv = arbView();
    float kPulse = arbKnob(0);
    float kTexture = arbKnob(1);
    float kFlow = arbKnob(2);
    float kTone = arbKnob(3);
    float kBuild = arbKnob(4);
    float kSpace = arbKnob(5);
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)
    gT = t;
    float energy = uIntensity;
    gChaos = max(arbChaos() + 0.5 * kFlow, 0.0);
    gGirth = 1.0 + 0.5 * kTexture;
    float swell = clamp(SND_INTENSITY * uSoundDrive, 0.0, 1.5);
    float treb = clamp(SND_TREBLE_PRESENCE * uSoundDrive, 0.0, 1.5) * energy;
    float kick = clamp(SND_KICK_ENV * uSoundDrive * energy, 0.0, 1.0);

    float morphIn = smoothstep(0.0, 1.0, 1.0 - clamp(SND_HELD_HARD_CUT, 0.0, 1.0));
    gHab = mix(habit(SND_CUT_COUNT - 1.0), habit(SND_CUT_COUNT), morphIn);
    gHab.x += arbMidBow();
    gPlateR = 0.075 + 0.025 * kick;
    gGrow = arbLevels(2.5 + 4.5 * clamp(treb, 0.0, 1.2), 1.0);
    gYaw = rot2(0.45 * sin(t * 0.11));
    gPitch = rot2(0.28 * sin(t * 0.077 + 1.0));
    gRoll = rot2(t * 0.03);

    float hue = 0.58 + 0.04 * sin(t * 0.03) + 0.6 * kTone;

    vec3 ro;
    vec3 rd;
    camAround(uv, CF_EYE, 0.0, ro, rd);

    // ---- sky and falling flakes (uniform control flow: fwidth inside) -----
    vec3 sky = skyColour(rd, hue);
    vec3 back = sky;
    for (int k = 0; k < 3; k++) {
        float fk = float(k);
        float R = 7.0 + 3.0 * fk + fk * fk;
        float tk;
        float ink = flakeLayer(ro, rd, R, 40.0 + 16.0 * fk, fk * 11.0, tk);
        back += pal(hue + 0.05) * 0.35 * ink * exp(-tk * 0.07);
    }
    // The crystal's own light, a soft halo about it.
    vec3 toC = -ro;
    float miss = length(cross(rd, toC));
    back += pal(hue + 0.08) * 0.10 * exp(-max(miss - 0.5, 0.0) * 1.6) * (0.8 + 0.3 * swell);

    // ---- the crystal --------------------------------------------------------
    float bb = dot(toC, rd);
    float disc = bb * bb - dot(toC, toC) + CF_BOUND * CF_BOUND;
    vec3 col = back;
    if (disc > 0.0) {
        float sq = sqrt(disc);
        float tm = max(bb - sq, 0.05);
        float tEnd = bb + sq;
        float hitT = -1.0;
        float used = 0.0;
        float dMin = 1e9;
        vec2 hc = vec2(0.0);
        float hd2 = 0.0;
        float hPlate = 0.0;
        float tip = 1.0;
        float lvl = 0.0;
        float path = 0.0;
        for (int i = 0; i < CF_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (tm > tEnd) break;
            used = float(i);
            float d = map(ro + rd * tm);
            dMin = min(dMin, d);
            if (d < 0.0006 + 0.0004 * tm) {
                hitT = tm;
                hc = gC;
                hd2 = gD2;
                hPlate = gPlate;
                tip = gArbTip;
                lvl = gArbLevel;
                path = gArbPath;
                break;
            }
            tm += d * 0.75;
        }
        // The glow close round every limb, the 2D style's exp(-d * 55) halo.
        col += pal(hue + 0.08) * 0.16 * exp(-max(dMin, 0.0) * 22.0) * (0.8 + 0.3 * swell) * (1.0 + kBuild);

        if (hitT > 0.0) {
            vec3 p = ro + rd * hitT;
            vec3 n = normalAt(p, 0.0008 + 0.0003 * hitT);
            vec3 L = normalize(vec3(-0.4, 0.75, -0.55));
            vec3 lit = arbLight(n, rd, L, 0.3, 60.0);
            float ao = arbStepAO(used, uSteps);

            vec3 ice = mix(vec3(0.85, 0.95, 1.0), pal(hue + 0.02), 0.35);
            // Spine: deep inside the outline, along every limb's centre.
            float spine = smoothstep(-0.002, -0.012, hd2);
            float rings = (0.5 + 0.5 * sin(length(hc) * 260.0)) * step(hPlate, 0.0);
            // Fake refraction with dispersion: the bent ray reads the palette
            // at three staggered offsets, and what is behind shows through.
            vec3 rf = refract(rd, n, 0.76);
            float kp = hue + 0.4 * (0.5 + 0.5 * dot(rf, vec3(0.42, 0.62, 0.66)));
            vec3 prism = vec3(pal(kp).r, pal(kp + 0.07).g, pal(kp + 0.14).b);
            vec3 through = skyColour(rf, hue) * 2.0 + pal(hue + 0.08) * 0.10;

            vec3 body = ice * (0.10 + 0.40 * lit.x) * ao + through * 0.5
                      + ice * (0.22 * spine + 0.07 * rings);
            col = body + prism * (0.12 + 0.75 * lit.y) * (0.4 + 0.4 * ao)
                + mix(vec3(1.0), prism, 0.3) * lit.z * 0.7;

            // Sparkle on the outermost tips, each twinkling on its own clock.
            float spark = (1.0 - smoothstep(0.0, 0.006, tip)) * step(gGrow - 2.0, lvl);
            spark *= 0.5 + 0.5 * sin(t * 3.0 + hash11(path * 0.37) * 40.0);
            col += vec3(1.0) * spark * 0.5;
            col = mix(col, sky, 1.0 - exp(-max(hitT - 3.0, 0.0) * 0.04 / (1.0 + 0.4 * kSpace)));
        }
    }

    col *= 0.88 + 0.15 * swell;
    col *= 0.8 + 0.2 * smoothstep(2.0, 0.3, length(uv));
    // k0 PULSE: the emission, as lib_flame's finish.
    col *= 1.0 + 0.5 * kPulse;
    fragColor = vec4(grade(col), 1.0);
}
