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
//#include lib_chrys

// Chrysanthemum Lotus: a lotus standing on still water at dusk, now a real
// flower you circle. The old style faked depth by layering fans of flat
// petals; here every petal is a thin curved sheet in 3D: a chrysLeaf outline
// (the family's petal, cream at the base flushing to the palette at the tip,
// veined and creased) laid on a surface that cups across its width and curls
// up along its length. Rings of them open around the flower's base: a wide
// outer ring lying out over the water, a middle ring, an upright inner cup and
// a bud ring hidden in the heart around a small gold seed pod. A few lily pads
// float around it, and the water mirrors the flower and the dusk sky.
//
// ---- camera ------------------------------------------------------------------
//
// camAround: the flower is one subject, so the eye circles it on the rig's
// orbit (one turn per 1024 world units of travel, CAM_SPIN_UNIT, which the
// 8192 travel wrap divides, so the wrap never shows) and the dolly pulls it
// in. Nothing is tiled along z, so there is no rebase to fit. The eye is held
// above the water, and its closest approach (LO_ORBIT * 0.65, the full dolly)
// is outside the bounding sphere, so it can never sit inside a petal.
//
// ---- audio ------------------------------------------------------------------
//
// The snare (SND_SNARE_ENV) LIFTS the flower: the outer ring, lying out over
// the water, swings up toward upright and the bud ring stands up out of the
// inner cup, so each snare reads as the flower drawing itself up. It goes
// through chrysBumpLean, so the petals start from rest instead of snapping.
// The bass band (sndLevel(SND_BASS_ATT)) sets how hard the water ripples:
// rings spread from the stem and bend the reflection (a geometry, not a
// light). Nothing turns the flower on the bar any more (owner 2026-11-12, no
// style spins to the music). Brightness follows SND_LEVEL only.
//
// ---- macros and knobs ---------------------------------------------------------
//
// Speed: uTime (already Speed-integrated) runs the ripples and the hue drift.
// Complexity: uMorph adds a wide back ring of eleven petals lying on the water,
// then a second bud ring. Chaos: uTurbulence + uWarp tilt each petal off its
// slot (chrysWobble) and fray the edges (chrysFray); uTwist bends every petal
// sideways along its length. Energy: uIntensity scales the snare lift and the
// bass ripple. Colour: pal() for the petal flush, sky, water and pads; the
// cream base is a fixed accent. Space: uZoom via view(), and the rig's orbit.
// Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE the snare lift,
// k1 TEXTURE the veins and fray, k2 FLOW how stirred the water stays between
// beats, k3 TONE the petal hue, k4 BUILD the sunset glow, k5 SPACE how far the
// flower opens.
//
// ---- why the distance estimate is safe ----------------------------------------
//
// A petal's distance is max(outline, sheet). The outline is chrysLeaf in the
// petal's own rigid frame. The sheet is |h| - thickness, where h is the offset
// along the petal's normal plus cup * across^2 + curl * along^2; with cup <= 0.9
// over a half-width <= 0.45 and curl <= 0.4 over a length <= 1.35 its gradient
// is at most sqrt(1 + 0.81^2 + 1.08^2) < 1.7. uTwist (clamped to 0.5) shears
// `across` by twist * along^2, adding up to 1.35 per unit twist, and the fray's
// noise adds a little per unit Chaos, so the field is divided by
// gLip = 1.7 + 2.7 |twist| + 0.6 chaos. Each ring is a polar repetition that
// measures the two nearest petals only; a petal is never wider than about two
// sectors at the radius it covers, so a third neighbour is never closer. All
// of it lies inside one sphere (LO_BOUND_R), whose distance is returned
// outside it, and the march starts where the ray enters that sphere.

#define LO_MAX_STEPS 96
#define LO_FAR 16.0
/** Where the petals' bases meet, just above the water. */
#define LO_BASE_Y -0.55
#define LO_WATER -0.6
/** The bounding sphere: centred over the base, holding every petal at full twist and lift. */
#define LO_BOUND_UP 0.5
#define LO_BOUND_R 1.95
#define LO_THICK 0.012
#define LO_ORBIT 4.6

float gHue;
float gT;
float gChaos;
float gFray;
float gTwist;
float gLift;
float gOpen;
float gVein;
float gLip;
float gGlow;

/** Morph: how far the back ring and the second bud ring have grown, 0..1. */
float gMorph;

vec3 sunDir() {
    return normalize(vec3(-0.62, 0.22, -0.75));
}

/**
 * One petal at azimuth `a` about +y, its axis `tilt` radians from upright.
 * q is relative to the flower's base.
 */
void petal(vec3 q, float a, float tilt, float len, float width, float cup, float curl, float dim, float hueOff, inout ChrysHit found) {
    float ca = cos(a);
    float sa = sin(a);
    // Turn about y so the petal points along +z.
    vec3 r = vec3(ca * q.x - sa * q.z, q.y, sa * q.x + ca * q.z);
    float ct = cos(tilt);
    float st = sin(tilt);
    float along = r.z * st + r.y * ct;
    float outw = r.z * ct - r.y * st;
    float across = r.x - gTwist * along * along;
    // Positive h is outward and down; cupped edges and a curled tip move
    // inward and up, toward the flower's heart.
    float h = outw + cup * across * across + curl * along * along;
    vec2 lq = vec2(across, along);
    vec4 leaf = chrysLeaf(lq, len, width, 0.3, 0.95);
    leaf.x += chrysFray(lq * 0.5, gFray);
    float u = clamp(leaf.y, 0.0, 1.0);
    float d = max(leaf.x, abs(h) - LO_THICK * (1.4 - 0.8 * u));
    if (d < found.b.w) {
        found.b.w = d;
        found.a.x = u;
        found.a.y = clamp(leaf.z, -1.0, 1.0);
        found.a.z = dim;
        found.a.w = hueOff;
        found.mat = 0.0;
    }
}

/** A ring of `count` petals: the two nearest to q are measured, so neighbours overlap. */
void ring(vec3 q, float count, float offset, float tilt, float len, float width, float cup, float curl, float dim, float hueOff, float seed, inout ChrysHit found) {
    if (len < 0.01) return;
    float stepA = CHRYS_TAU / count;
    // +1e-6: atan(0, 0) is undefined in GLSL ES, and the axis is inside the pod.
    float phi = atan(q.x, q.z + 1e-6);
    float f = (phi - offset) / stepA;
    float j = floor(f + 0.5);
    float j2 = j + (f > j ? 1.0 : -1.0);
    for (int s = 0; s < 2; s++) {
        float jj = s == 0 ? j : j2;
        float w = gChaos * chrysWobble(mod(jj, count) + seed, gT);
        petal(q, offset + jj * stepA + 0.1 * w, tilt + 0.08 * w, len * (1.0 + 0.05 * w), width, cup, curl, dim, hueOff, found);
    }
}

/** The style's world at p: its distance, and in `found` what is there (mat 0 petal, 1 seed pod, -1 nothing (outside the bound); a.x u, a.y v, a.z the ring's dimming, a.w its hue offset, b.w the nearest surface's distance). */
float mapAt(vec3 p, inout ChrysHit found) {
    vec3 q = p - vec3(0.0, LO_BASE_Y, 0.0);
    float db = length(q - vec3(0.0, LO_BOUND_UP, 0.0)) - LO_BOUND_R;
    if (db > 0.3) {
        found.mat = -1.0;
        return db;
    }

    found.b.w = 1e9;
    found.mat = -1.0;

    float morph = gMorph;
    // The back ring: only with Complexity, lying almost flat on the water.
    if (morph > 0.01) {
        ring(q, 11.0, 0.17, 1.46 + 0.5 * gOpen, 1.3 * smoothstep(0.0, 0.5, morph), 0.42, 0.5, 0.18, 0.42, 0.04, 31.0, found);
    }
    // The outer ring lies out over the water; the snare swings it up.
    ring(q, 9.0, 0.0, mix(1.3, 0.78, gLift) + gOpen, 1.2, 0.48, 0.55, 0.22, 0.55, 0.02, 27.0, found);
    ring(q, 8.0, 0.5, 0.92 + 0.7 * gOpen, 1.05, 0.5, 0.7, 0.28, 0.75, 0.0, 24.0, found);
    ring(q, 6.0, 0.21, 0.46 + 0.4 * gOpen, 0.9, 0.52, 0.8, 0.34, 0.95, -0.03, 18.0, found);
    // The bud ring: barely there at rest, standing up on a snare.
    ring(q, 4.0, 0.9, 0.16, 0.72 * (0.25 + 0.75 * gLift), 0.55, 0.9, 0.4, 1.05, -0.06, 12.0, found);
    if (morph > 0.5) {
        ring(q, 5.0, 1.4, 0.3, 0.55 * smoothstep(0.5, 1.0, morph) * (0.3 + 0.7 * gLift), 0.6, 0.9, 0.4, 1.1, -0.09, 15.0, found);
    }

    // The seed pod: a short rounded drum in the heart.
    vec2 dd = abs(vec2(length(q.xz), q.y - 0.13)) - vec2(0.15, 0.13);
    float pod = min(max(dd.x, dd.y), 0.0) + length(max(dd, 0.0)) - 0.02;
    if (pod < found.b.w) {
        found.b.w = pod;
        found.mat = 1.0;
        found.a.x = q.y;
        found.a.y = length(q.xz);
    }
    return found.b.w / gLip;
}

/** The style's world at p as a hit, for lib_chrys_field. */
ChrysHit chrysMap(vec3 p) {
    ChrysHit h = chrysHit(0.0);
    float d = mapAt(p, h);
    h.d = d;
    return h;
}

//#include lib_chrys_field

/** Where the ray enters and leaves the bounding sphere; x > y when it misses. */
vec2 boundSpan(vec3 ro, vec3 rd) {
    vec3 oc = ro - vec3(0.0, LO_BASE_Y + LO_BOUND_UP, 0.0);
    float b = dot(oc, rd);
    // The grown world (lib_grow: a twin aside, children, a layer opened) reaches past the bound by growSlack
    // while a control is on; the bound itself at rest.
    float boundR = LO_BOUND_R + growSlack(LO_BOUND_R, 1.0, 0.2);
    float c = dot(oc, oc) - boundR * boundR;
    float h = b * b - c;
    if (h < 0.0) return vec2(1.0, -1.0);
    float s = sqrt(h);
    return vec2(max(-b - s, 0.0), -b + s);
}

/** March the flower from t0 to t1 with `budget` steps (the loop's own bound is the constant). */
float marchFlower(vec3 ro, vec3 rd, float t0, float t1, float budget) {
    float t = t0;
    for (int i = 0; i < LO_MAX_STEPS; i++) {
        if (float(i) >= budget) break;
        float d = chrysField(ro + rd * t).d;
        if (d < 0.0012 + 0.0006 * t) return t;
        t += d * 0.85;
        if (t > t1) break;
    }
    return -1.0;
}

vec3 skyColour(vec3 rd) {
    float up = clamp(rd.y, -1.0, 1.0);
    vec3 col = mix(pal(gHue + 0.08) * 0.2, pal(gHue + 0.55) * 0.05, smoothstep(-0.05, 0.7, up));
    float sun = max(dot(rd, sunDir()), 0.0);
    col += pal(gHue + 0.12) * (0.28 * pow(sun, 24.0) + 0.1 * sun * sun * sun) * gGlow;
    return col;
}

/** The flower's colour at a hit, `hit` the field there. */
vec3 shadeFlower(vec3 p, vec3 rd, vec3 n, float swell, ChrysHit hit) {
    // Petals are thin: light the side facing the eye, and let the low sun
    // shine through from behind.
    if (dot(n, rd) > 0.0) n = -n;
    vec3 L = sunDir();
    float dif = max(dot(n, L), 0.0);
    float through = max(dot(-n, L), 0.0);
    float fres = pow(1.0 - clamp(-dot(n, rd), 0.0, 1.0), 3.0);
    float sky = 0.5 + 0.5 * n.y;
    if (hit.mat > 0.5) {
        // The pod: gold-green, its flat top pricked with darker seeds.
        vec3 pc = mix(vec3(0.72, 0.6, 0.24), pal(gHue + 0.15), 0.25);
        vec2 top = (p.xz) / 0.15;
        float ang = atan(top.y, top.x + 1e-6);
        float seed = length(vec2(length(top) - 0.6, (fract(ang * 7.0 / CHRYS_TAU) - 0.5) * 2.4));
        float isTop = smoothstep(0.5, 0.9, n.y);
        pc *= 1.0 - 0.6 * isTop * (1.0 - smoothstep(0.14, 0.22, min(seed, length(top))));
        return pc * (0.12 * sky + 0.7 * dif) + pal(gHue + 0.1) * 0.08 * (0.6 + 0.4 * swell);
    }
    float u = hit.a.x;
    float v = hit.a.y;
    // Cream at the base flushing to the palette at the tip, with fine
    // lengthwise veins and a crease down the middle, as the flat lotus had.
    vec3 cream = vec3(0.95, 0.9, 0.86);
    vec3 pc = mix(cream, pal(gHue + hit.a.w), smoothstep(0.25, 1.0, u));
    float veins = (1.0 - gVein) + gVein * cos(v * 14.0);
    float crease = 1.0 - 0.25 * exp(-abs(v) * 10.0);
    float tuck = mix(0.45, 1.0, smoothstep(0.0, 0.4, u));
    pc *= veins * crease * tuck * mix(1.0, hit.a.z, 0.5);
    vec3 col = pc * (0.1 * sky + 0.62 * dif);
    // Backlit petals glow, more at their thin tips.
    col += pc * pal(gHue + 0.08) * through * (0.35 + 0.45 * u) * gGlow;
    col += pal(gHue + 0.1) * fres * 0.08;
    return col;
}

/** Folds one lily pad (xy centre, z radius, w notch angle) into `res` if the water point w lies on it. */
void lilyPad(vec2 w, vec4 pad, inout vec4 res) {
    vec2 o = w - pad.xy;
    float r = length(o);
    if (r >= pad.z || res.a > 0.5) return;
    float ang = atan(o.y, o.x + 1e-6);
    // The notch every lily pad has, cut toward its own angle.
    float notch = abs(mod(ang - pad.w + CHRYS_PI, CHRYS_TAU) - CHRYS_PI);
    if (notch < 0.18 * (r / pad.z)) return;
    vec3 pc = mix(vec3(0.05, 0.13, 0.07), pal(gHue + 0.35) * 0.2, 0.3);
    pc *= 0.85 + 0.15 * cos(ang * 18.0);
    pc *= 1.0 + 0.5 * smoothstep(pad.z * 0.85, pad.z, r);
    res = vec4(pc * (0.5 + 0.9 * sunDir().y), 1.0);
}

/** The four pads floating around the flower, outside its bound and the eye's orbit. */
vec4 lilyPads(vec2 w) {
    vec4 res = vec4(0.0);
    lilyPad(w, vec4(1.95, 0.7, 0.5, 0.4), res);
    lilyPad(w, vec4(-1.65, 1.25, 0.42, 2.1), res);
    lilyPad(w, vec4(-0.95, -1.9, 0.55, 4.0), res);
    lilyPad(w, vec4(1.3, -1.65, 0.36, 5.3), res);
    return res;
}

void main() {
    ChrysHit fld = chrysHit(0.0);
    vec2 uv = view();
    // The normalization shell (lib_chrys): every variable from its driver, mapped to its range here, once.
    // This style's older knob roles: PULSE lifts the outer ring on the snare, SPACE opens the rings.
    ChrysIn N = chrysIn();
    float liftK = chrysSpan(N.emit, 1.0, 0.4, 1.6);
    float rippleK = chrysSpan(N.sway, 1.0, 0.6, 1.4);
    gMorph = chrysSpan(N.rings, 0.0, 0.0, 1.0);

    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    gLift = clamp(chrysBumpLean(SND_SNARE_ENV, 2.0) * uIntensity * liftK, 0.0, 1.2);
    float bass = clamp(sndLevel(SND_BASS_ATT) * uIntensity, 0.0, 1.5);
    gT = uTime;  // uTime already integrates Speed (ShaderScene::update)
    gChaos = chrysSpan(N.chaos, 0.0, 0.0, 2.0);
    gFray = gChaos + chrysSpan(N.fray, 0.0, 0.0, 0.4);
    gVein = chrysSpan(N.width, 0.08, 0.016, 0.144);
    gTwist = clamp(chrysSpan(N.curl, 0.0, -1.0, 1.0), -0.5, 0.5);
    gOpen = chrysSpan(N.reach, 0.0, -0.125, 0.125);
    gLip = 1.7 + 2.7 * abs(gTwist) + 0.6 * gFray;
    gHue = 0.95 + 0.02 * sin(gT * 0.03) + chrysSpan(N.tone, 0.0, -0.04, 0.04);
    gGlow = (0.8 + 0.2 * swell) * chrysSpan(N.glow, 1.0, 0.55, 1.45);


    vec3 ro;
    vec3 rd;
    camAround(uv, LO_ORBIT, CAM_SPIN_UNIT, ro, rd);
    // Never under the water: the rig's pitch can swing the orbit low.
    ro.y = max(ro.y, LO_WATER + 0.25);

    float tWater = rd.y < -1e-4 ? (LO_WATER - ro.y) / rd.y : LO_FAR;
    vec2 span = boundSpan(ro, rd);
    float hitT = -1.0;
    if (span.x < span.y) hitT = marchFlower(ro, rd, span.x, min(min(span.y, tWater), LO_FAR), uSteps);

    vec3 sky = skyColour(rd);
    vec3 col = sky;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = chrysNormal(p, 0.001 + 0.0006 * hitT);
        fld = chrysField(p);
        col = shadeFlower(p, rd, n, swell, fld);
        col = mix(col, sky, 1.0 - exp(-hitT * 0.03));
    } else if (tWater < LO_FAR) {
        vec3 w = ro + rd * tWater;
        vec4 pad = lilyPads(w.xz);
        vec3 horizon = skyColour(normalize(vec3(rd.x, 0.0, rd.z)));
        if (pad.a > 0.5) {
            col = pad.rgb;
        } else {
            // Rings spreading from the stem tilt the water; the bass sets how hard.
            float rr = max(length(w.xz), 1e-3);
            float amp = (0.02 + 0.1 * bass) * rippleK;
            float wave = rr * 14.0 - gT * 2.2;
            float slope = amp * sin(wave) * exp(-0.25 * rr);
            vec3 n = normalize(vec3(w.x / rr * slope, 1.0, w.z / rr * slope));
            vec3 rf = reflect(rd, n);
            rf.y = abs(rf.y);
            vec3 wo = w + vec3(0.0, 0.01, 0.0);
            vec3 refl = skyColour(rf);
            vec2 rs = boundSpan(wo, rf);
            if (rs.x < rs.y) {
                float rt = marchFlower(wo, rf, rs.x, rs.y, uSteps * 0.5);
                if (rt > 0.0) {
                    vec3 rp = wo + rf * rt;
                    vec3 rn = chrysNormal(rp, 0.0015 + 0.0006 * rt);
                    fld = chrysField(rp);
                    refl = shadeFlower(rp, rf, rn, swell, fld);
                }
            }
            float fres = 0.35 + 0.65 * pow(1.0 - clamp(-dot(rd, n), 0.0, 1.0), 4.0);
            col = mix(pal(gHue + 0.6) * 0.03, refl * vec3(0.45, 0.55, 0.65), fres);
            // Glints on the ripple crests, facing the low sun.
            float glint = pow(max(sin(wave), 0.0), 12.0) * pow(max(dot(rf, sunDir()), 0.0), 6.0);
            col += pal(gHue + 0.1) * 0.25 * glint * gGlow;
        }
        col = mix(col, horizon, 1.0 - exp(-tWater * 0.06));
    }

    col *= 0.85 + 0.15 * swell;
    col *= 0.7 + 0.3 * (1.0 - smoothstep(0.6, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
