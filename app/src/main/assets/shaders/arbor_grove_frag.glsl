#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP; uSoundTex is R32F.
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

// Arbor Grove: one tree on a hill against a low dusk sun, smaller ones on the
// far ridge, the first stars coming out above. It was the family's flat free
// style; now the hill and the tree are marched, so the crown has depth, the
// sun rims it when you look into the light and lights its face when you turn
// away, and the ground fogs into the haze of the ridge. It is still the
// plainest statement of the family's idea: a silhouette the music blows about.
//
// The tree is arborTree (a mirror fork, twisted per level so the forks spread
// round the trunk in 3D, a touch of gravity so the outer limbs settle), with
// a leaf pad on every tip (the walk's gArbTip, a sphere there) so the crown
// reads as a crown and not a bare winter frame. The hill is a smooth height
// field under it. The far ridge and its trees are NOT marched: they sit on
// the horizon, where parallax is nil, so they are the old 2D silhouettes
// (arborTree2 in azimuth and elevation) drawn in the sky, cheap and sharp.
//
// ---- camera -------------------------------------------------------------------
//
// camAround: the rig's orbit about the tree, its look and dolly. The eye stays
// within GR_ORBIT of the origin and never travels, so the 8192 wrap cannot
// reach the world; the turn is 2 * CAM_SPIN_UNIT, an integer multiple, so the
// travel wrap lands on a whole turn and never jumps. The eye is lifted out of
// the hill if the rig's pitch would put it underground.
//
// ---- audio ---------------------------------------------------------------------
//
// Bass is the WIND, as before. SND_BASS_PRESENCE (slew-limited, so no frame
// can jerk a branch) sets how far the crown bows downwind: the tree is
// sheared by the square of the height, so the trunk barely moves while the
// crown bows, and the same lean nudges the per-level twist, which compounds
// down the folds so the twigs whip. A slow gust oscillator rides on top so a
// held bass note still sways, and the kick (SND_KICK_ENV) adds a short puff.
// Air (sndSpectrum(0.93)) glints on the outermost leaf pads on the sun side,
// small points, never the frame. The sun's halo widens with SND_LEVEL only.
//
// ---- macros and knobs ------------------------------------------------------------
//
// Speed: uTime drives the gust, the twinkle and every clock. Complexity:
// uMorph adds a fold level (arbLevels, cut by Detail). Chaos: uTurbulence and
// uWarp flutter the twist and gust; uTwist winds the forks into a spiral.
// Colour: sky, sun, rim and leaves lean on pal() without leaving dusk.
// Energy: uIntensity scales the bass lean, the kick puff and the air glints.
// Space: the rig's orbit, look and dolly. Knobs (offsets from 0.5, so off is
// the tuned look): k0 PULSE the kick puff, k1 TEXTURE the leaf pads' size,
// k2 FLOW the gust's rate and depth, k3 TONE the sky's hue, k4 BUILD the
// sun's halo, k5 SPACE the haze. The mids bow the limbs (arbMidBow).
//
// ---- why the distance estimate is safe ----------------------------------------------
//
// Hill: p.y minus a height whose slope is at most 0.23 (the bump's peak slope
// 0.144 plus the ripple's 0.085), so its gradient is below 1.03; scaled by
// 0.95. Tree: a bounding sphere first (an exact lower bound outside it), then
// the fold, a true lower bound almost everywhere (lib_arbor's header). The
// wind shear moves a point by gLean * h^2 with h clamped to GR_SHEAR_TOP, so
// its Jacobian is at most 1 + 2 * |gLean| * GR_SHEAR_TOP; the tree distance
// is divided by exactly that. The march then steps at 0.8 of the estimate to
// hide the fold's rare overshoot where a twisted twig crosses a fold plane.

#define GR_MAX_STEPS 120
#define GR_FAR 42.0
#define GR_HILL -1.9
#define GR_ORBIT 9.5
/** Height above the trunk's foot where the wind shear stops growing. */
#define GR_SHEAR_TOP 5.0
/** Largest |gLean|: 0.024 * 25 = 0.6 world units of bow at the crown. */
#define GR_LEAN_MAX 0.024
#define GR_BOUND_Y 2.3
#define GR_BOUND_R 4.8

ArborForm gF;
float gGrow;
float gLean;
vec2 gWind;
float gPad;
float gTreeY;
vec3 gSun;
float gMat;
float gLevel;
float gPath;
float gTip;

float groundH(vec2 xz) {
    return GR_HILL + 0.75 * exp(-dot(xz, xz) * 0.05) + 0.12 * sin(xz.x * 0.45 + 0.4) * sin(xz.y * 0.5 + 1.1);
}

/** Elevation (sine) of the far ridge's crest at azimuth az; whole frequencies, so it closes round the turn. */
float ridgeEl(float az) {
    return 0.035 + 0.03 * sin(az * 3.0 + 1.0) + 0.015 * sin(az * 7.0 + 0.3);
}

/** The tree with leaf pads; writes gMat (1 bark, 2 leaf), gLevel, gPath, gTip. */
float treeD(vec3 p) {
    vec3 q = p - vec3(0.0, gTreeY, 0.0);
    float bound = length(q - vec3(0.0, GR_BOUND_Y, 0.0)) - GR_BOUND_R;
    if (bound > 0.5) {
        gMat = 1.0;
        return bound;
    }
    float h = clamp(q.y, 0.0, GR_SHEAR_TOP);
    q.xz -= gWind * (gLean * h * h);
    float lip = 1.0 + 2.0 * abs(gLean) * GR_SHEAR_TOP;
    float bark = arborTree(q, gF, gGrow);
    gLevel = gArbLevel;
    gPath = gArbPath;
    gTip = gArbTip;
    float pad = gTip - gPad * (0.7 + 0.6 * arbBranchHash(gPath, 0.0, 3.0));
    gMat = pad < bark ? 2.0 : 1.0;
    return min(bark, pad) / lip;
}

float map(vec3 p) {
    float g = (p.y - groundH(p.xz)) * 0.95;
    float tr = treeD(p);
    if (g < tr) gMat = 0.0;
    return min(g, tr);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** Dusk sky without the far ridge: warm at the horizon toward the sun, night overhead, stars, sun and halo. */
vec3 skyCol(vec3 rd, float hue, float swell, float halo, float t) {
    float el = rd.y;
    float toSun = max(dot(rd, gSun), 0.0);
    vec3 horizon = mix(pal(hue + 0.02) * 1.15, vec3(1.0, 0.52, 0.24), 0.35);
    vec3 dusk = mix(pal(hue + 0.30) * 0.55, vec3(0.50, 0.20, 0.32), 0.35);
    vec3 night = pal(hue + 0.62) * 0.05 + vec3(0.02, 0.018, 0.06);
    vec3 low = mix(dusk * 0.7, horizon, 0.35 + 0.65 * toSun * toSun);
    vec3 col = mix(low, dusk, smoothstep(-0.02, 0.30, el));
    col = mix(col, night, smoothstep(0.18, 0.85, el));

    vec3 sunCol = mix(pal(hue + 0.05), vec3(1.0, 0.85, 0.6), 0.55);
    float ang = sqrt(max(2.0 * (1.0 - dot(rd, gSun)), 0.0));
    col += sunCol * (1.0 - smoothstep(0.034, 0.040, ang)) * 0.8;
    col += horizon * exp(-ang * (5.0 - 1.3 * swell)) * (0.30 + 0.12 * swell) * halo;

    vec3 sp = rd * 150.0;
    float s = hashCell(ivec3(floor(sp)));
    float star = (1.0 - smoothstep(0.0, 0.3, length(fract(sp) - 0.5))) * step(0.993, s);
    star *= smoothstep(0.15, 0.6, el) * (0.6 + 0.4 * sin(t * (1.0 + 3.0 * s) + s * 400.0));
    col += vec3(0.85, 0.85, 1.0) * star * 0.7;
    return col;
}

/** Coverage of one far-ridge tree standing at azimuth az0, scale in radians. */
float farTree(float az, float el, float az0, float scale, float seed, float px, float lean, float jitter) {
    float da = az - az0;
    da -= ARB_TAU * floor(da / ARB_TAU + 0.5);
    vec2 q = vec2(da, el - ridgeEl(az0) + 0.004) / scale;
    if (abs(q.x) > 1.8 || q.y < -0.1 || q.y > 1.9) return 0.0;
    ArborForm2 f = ArborForm2(vec2(0.50, 0.44), vec2(0.70, 0.66), lean, jitter, 0.40, 0.09, 0.70);
    float d = arborTree2(q, f, arbLevels2(7.0, 1.0), seed) * scale;
    return arbInk(d, px);
}

/** What a ray that leaves the scene sees: the sky, the hazy ridge and its trees. */
vec3 backdrop(vec3 rd, float hue, float swell, float halo, float t, float px, float lean, float jitter) {
    vec3 col = skyCol(rd, hue, swell, halo, t);
    float az = atan(rd.z, rd.x);
    float el = rd.y;
    if (el < 0.14) {
        vec3 haze = mix(col, pal(hue + 0.45) * 0.18 + vec3(0.05, 0.02, 0.04), 0.62);
        col = mix(col, haze, arbInk(el - ridgeEl(az), px));
        float sunAz = atan(gSun.z, gSun.x);
        float ft = farTree(az, el, sunAz + 0.30, 0.055, 3.0, px, lean * 0.6, jitter + 0.1);
        ft = max(ft, farTree(az, el, sunAz + 2.70, 0.045, 7.0, px, lean * 0.5, jitter + 0.1));
        ft = max(ft, farTree(az, el, sunAz - 2.20, 0.040, 13.0, px, lean * 0.5, jitter + 0.1));
        col = mix(col, haze * 0.8, ft);
    }
    return col;
}

void main() {
    vec2 uv = arbView();

    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)
    float energy = uIntensity;
    float chaos = arbChaos();
    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kFlow = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    float kSpace = clamp(uKnob[5], 0.0, 1.0) - 0.5;

    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    float bass = clamp(SND_BASS_PRESENCE, 0.0, 1.5) * energy;
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0) * energy * (1.0 + 1.6 * kPulse);
    float air = clamp(sndSpectrum(0.93), 0.0, 1.0) * energy;

    // ---- the wind -------------------------------------------------------------
    float gustRate = 1.0 + 0.8 * kFlow;
    float gust = 0.55 + (0.45 + 0.3 * kFlow) * sin(t * 1.25 * gustRate) * sin(t * 0.53 * gustRate + 1.0);
    float flutter = chaos * 0.35 * sin(t * 4.1) * sin(t * 2.7 + 1.3);
    float lean = (0.15 + 0.9 * bass) * (gust - 0.35) + 0.12 * sin(t * 0.37) + flutter + 0.35 * kick;
    lean = clamp(lean, -1.0, 1.0);
    gLean = GR_LEAN_MAX * lean;
    float windA = 0.6 + 0.4 * sin(t * 0.05);
    gWind = vec2(cos(windA), sin(windA));

    gGrow = arbLevels(8.0, 1.0);
    float twist = 2.25 + 0.6 * uTwist + 0.07 * lean + chaos * 0.12 * sin(t * 3.1 + 0.4);
    gF = ArborForm(2.0, 0.50, 0.05 + 0.02 * lean + arbMidBow(), twist, 0.72, 1.15, 0.085, 0.72);
    gPad = clamp(0.10 * (1.0 + 1.2 * kTexture), 0.02, 0.18);
    gTreeY = groundH(vec2(0.0)) - 0.05;

    float sunAz = -1.2;
    gSun = normalize(vec3(cos(sunAz), 0.10, sin(sunAz)));
    float hue = 0.10 * kTone;
    float halo = 1.0 + 1.4 * kBuild;
    // The 2D far trees take the old flat lean and jitter.
    float lean2 = (0.006 + 0.04 * bass) * (gust - 0.35) + 0.010 * sin(t * 0.37) + 0.07 * flutter + 0.08 * uTwist;
    float jitter2 = 0.20 + 0.45 * chaos;

    vec3 ro;
    vec3 rd;
    camAround(uv, GR_ORBIT, 2.0 * CAM_SPIN_UNIT, ro, rd);
    ro.y = max(ro.y, groundH(ro.xz) + 0.5);
    // Angular pixel footprint, taken here in uniform control flow.
    float px = max(length(fwidth(rd)), 1e-4);

    float tm = 0.05;
    float hitT = -1.0;
    float used = 0.0;
    float hitMat = 0.0;
    float hitLevel = 0.0;
    float hitPath = 0.0;
    for (int i = 0; i < GR_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        used = float(i);
        float d = map(ro + rd * tm);
        if (d < 0.0005 + 0.0008 * tm) {
            hitT = tm;
            hitMat = gMat;
            hitLevel = gLevel;
            hitPath = gPath;
            break;
        }
        tm += d * 0.8;
        if (tm > GR_FAR) break;
    }

    vec3 col;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, 0.0012 * hitT + 0.0006);
        float ao = arbStepAO(used, uSteps);
        vec3 horizon = mix(pal(hue + 0.02) * 1.15, vec3(1.0, 0.52, 0.24), 0.35);
        vec3 dusk = mix(pal(hue + 0.30) * 0.55, vec3(0.50, 0.20, 0.32), 0.35);
        vec3 sunCol = mix(pal(hue + 0.05), vec3(1.0, 0.85, 0.6), 0.55);
        vec3 ink = vec3(0.025, 0.018, 0.04);
        // Into the light: the tree is the old silhouette with a rim. Away from
        // it: the sun's low warm light on its face.
        float intoSun = max(dot(rd, gSun), 0.0);
        float backlit = intoSun * intoSun;
        vec3 lit = arbLight(n, rd, gSun, 0.35, 16.0);
        float skyLight = 0.5 + 0.5 * n.y;

        if (hitMat < 0.5) {
            vec3 grass = mix(ink, pal(hue + 0.35) * 0.08, 0.5);
            col = grass * (0.6 + 0.6 * skyLight) * ao + horizon * lit.x * 0.10 * (1.0 - backlit);
            col += horizon * lit.y * 0.12 * backlit;
        } else {
            vec3 base = hitMat > 1.5 ? mix(ink, pal(hue + 0.30) * 0.22 + vec3(0.03, 0.05, 0.02), 0.6) : ink * 1.4;
            col = base * (0.5 + 0.8 * skyLight) * ao;
            col += base * dusk * 0.6 * skyLight * ao;
            col += sunCol * base * 3.0 * lit.x * (1.0 - backlit) * ao;
            // Rim on the edge that faces the sun, strongest looking into it.
            col += horizon * lit.y * (0.15 + 0.55 * backlit);
            // Leaves glow through when the sun is behind them.
            if (hitMat > 1.5) col += horizon * 0.25 * backlit * pow(max(dot(-n, gSun), 0.0), 2.0) * 0.6;
            // Air glints on the outermost pads, sun side only, a few of them.
            float outer = step(gGrow - 2.5, hitLevel) * step(1.5, hitMat);
            float pick = step(0.55, arbBranchHash(hitPath, 1.0, 9.0));
            float tw = 0.5 + 0.5 * sin(t * 7.0 + arbBranchHash(hitPath, 2.0, 5.0) * 40.0);
            float sunSide = smoothstep(0.0, 0.5, dot(n, gSun));
            col += sunCol * outer * pick * tw * sunSide * air * 0.9;
        }
        float fogK = 0.032 * (1.0 + 1.2 * kSpace);
        vec3 fogCol = skyCol(normalize(vec3(rd.x, 0.02, rd.z)), hue, swell, halo, t);
        fogCol = mix(fogCol, pal(hue + 0.45) * 0.18 + vec3(0.05, 0.02, 0.04), 0.45);
        col = mix(col, fogCol, 1.0 - exp(-hitT * fogK));
    } else {
        col = backdrop(rd, hue, swell, halo, t, px, lean2, jitter2);
    }

    col *= 0.9 + 0.12 * swell;
    col *= 0.78 + 0.22 * smoothstep(2.2, 0.5, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
