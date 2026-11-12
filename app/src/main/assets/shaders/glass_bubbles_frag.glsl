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
//#include lib_glass

// Glass Bubbles: a flight down a long dark hall strung with neon arches, full
// of soap bubbles rising from a wet floor to pop under the ceiling. Every
// bubble carries the arches' coloured arcs wrapped round its surface, banded by
// interference. A bubble is a film a few hundred nanometres thick: it barely
// bends light, it colours it. The near wall reflects the hall through
// glassFilm's two-beam interference; the far wall, seen from inside, adds a
// second, mirrored set. The film is not uniform: water drains down, so it is
// thick and banded at the foot, thins toward the crown and goes black (the
// patch just before a bubble pops) at the very top; and it swirls, slow
// marbled currents carried round the surface. Bubbles composite front to back
// as transmission and added reflection; the march walks on through each one
// it meets, so bubbles stack behind bubbles, fogged by depth.
//
// The arches are real geometry: a rounded neon tube bent round a rectangle
// across the hall every 16 world units, reflected in the floor and in every
// film by an exact mirror lookup (neonMirror: the reflected ray meets the next
// arch's plane in closed form), and glowing into the air the march crosses.
// Beyond the hall's open ends and above it lies the family's fractal world
// (GLASS_ENV_FRACTAL), which the films also reflect.
//
// ---- camera -----------------------------------------------------------------
//
// camFly: the eye rides the rig's path down +z, and the dolly (kick punches,
// drop pushes) is already in uCamPos. The eye's z is rebased by BB_ZWRAP (64);
// every z term has a period that divides it (bubble cells 4, so the column
// hash repeats every 16 cells; arches 16; the arch chase 64), and 64 divides
// the rig's 8192 travel wrap, so neither the rebase nor the wrap ever shows.
// The old orbit round a vortex is gone; the hall is flown through instead.
//
// ---- audio ------------------------------------------------------------------
//
//   pitch height     the film's thickness, 280 -> 720 nm: the band colours roll
//                    from gold-magenta through to green-blue as the melody climbs;
//   snare            a shiver runs over every bubble: a travelling ripple on the
//                    surface normals, so the reflected neon wobbles;
//   bass             the bubbles breathe (radius);
//   hat              glints on the films;
//   onsets           the light chases from arch to arch, one arch per onset, eased;
//   pan, width       the arches roll with the pan and stand wider with the
//                    stereo width (and the fractal world's key and lights, as before);
//   flatness         a frost on the reflections (the films go milky in noise).
//
// ---- macros and knobs ---------------------------------------------------------
//
// Speed scales the rise and swirl (uTime). Complexity fills more of the hall's
// cells with bubbles. Chaos wobbles the surfaces (glassWobble). Energy scales
// every reaction. Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE
// the bass breath, k1 TEXTURE the marbling, k2 FLOW the sway, k3 TONE the hue,
// k4 BUILD the neon's strength, k5 SPACE the bubbles' size.
//
// ---- why the distance estimate is safe ------------------------------------------
//
// Bubbles live one per 4-unit cell, in columns that rise at their own rate (y
// is repeated in the column's rising frame). Each cell measures only its own
// sphere, an exact distance. A neighbour's sphere stays inside its own cell
// with room to spare: radius at most 0.85 * 1.112 (breath) * 1.2 (SPACE) <
// 1.14, jitter 0.2 and sway at most 0.375 sideways, none vertically, so its
// surface is at least 0.29 inside its cell's wall: the cell estimate is clamped
// to the distance out of the cell plus BB_GAP (0.2). Every bubble lies inside
// the hall box, so the estimate is also never less than the distance to that
// box. The arches are an exact rounded outline of a rotated rectangle and the
// floor a plane. Bubbles shrink to nothing within 0.7 of the eye, so the eye is
// never inside a film; the rig's path keeps well inside the arches and above
// the floor. Normals are the sphere's own, analytic: no finite differences.
//
// ---- cost -------------------------------------------------------------------
//
// A bounded march (BB_MAX_STEPS, broken on uSteps, the Detail budget). Per
// bubble met, two film evaluations and two neon mirrors; the fractal world is
// read once for the sky and once per bubble's near wall. At most three bubbles
// deep (two on Saver).

#define BB_MAX_STEPS 96
#define BB_LAYERS 3
/** Rebase period of the eye's z; divides 8192. */
#define BB_ZWRAP 64.0
#define BB_CELL 4.0
#define BB_HALF 2.0
/** How far inside its own cell any bubble surface keeps (see the header). */
#define BB_GAP 0.2
/** World units between arches. */
#define BB_ARCH 16.0
#define BB_ARCH_HY 8.5
#define BB_ARCH_CY 1.2
#define BB_TUBE 0.12
#define BB_FLOOR -7.6
/** Where a bubble's centre is born and where it pops. */
#define BB_BORN -6.4
#define BB_POP 7.0
#define BB_FAR 44.0
#define BB_FOG 0.035

float gT;
vec3 gEye;
float gFill;
float gSize;
float gBreath;
float gSway;
float gHalfX;
float gRoll;
float gChase;
float gHue;
float gNeonGain;
/**
 * What the field holds at a point (lib_glass_field): the distance, the bubble of the cell (centre,
 * radius, id), the distance to the nearest arch's tube and its index, the material (0 bubble, 1 neon,
 * 2 floor).
 */
struct GlassHit {
    float d;
    vec3 c;
    float r;
    float id;
    float neonD;
    float arch;
    int mat;
};

/** Distance in the arch plane to the rectangle's outline, rolled by the pan. */
float archOutline(vec2 xy) {
    vec2 q = glassRot(gRoll) * (xy - vec2(0.0, BB_ARCH_CY));
    vec2 e = abs(q) - vec2(gHalfX, BB_ARCH_HY);
    return abs(length(max(e, 0.0)) + min(max(e.x, e.y), 0.0));
}

/** An arch's light; `aid` is its index mod 4. The brightest step chases on with the onsets. */
vec3 neonColour(float aid) {
    float chase = fract((aid - gChase) / 4.0);
    return mix(pal(gHue + aid * 0.17), vec3(1.0), 0.25) * (0.9 + 1.6 * smoothstep(0.6, 1.0, chase)) * gNeonGain;
}

/**
 * The arches seen along the mirror ray `rr` from `p`: the ray meets the next
 * arch plane ahead in closed form, and the lobe is how close it lands to the
 * tube there, widened by `blur` and by distance (a wet, imperfect mirror).
 */
vec3 neonMirror(vec3 p, vec3 rr, float blur) {
    if (abs(rr.z) < 1e-3) return vec3(0.0);
    float k = p.z / BB_ARCH - 0.5;
    float n = rr.z > 0.0 ? floor(k) + 1.0 : ceil(k) - 1.0;
    float tp = ((n + 0.5) * BB_ARCH - p.z) / rr.z;
    vec3 q = p + rr * tp;
    float dist = archOutline(q.xy);
    float w = BB_TUBE + blur + 0.012 * tp;
    float lobe = exp(-dist * dist / (w * w)) * BB_TUBE / w;
    return neonColour(mod(n, 4.0)) * lobe * exp(-tp * BB_FOG);
}

GlassHit glassMap(vec3 p) {
    GlassHit h;
    float zi = floor(p.z / BB_ARCH);
    float dz = p.z - (zi + 0.5) * BB_ARCH;
    float dn = length(vec2(archOutline(p.xy), dz)) - BB_TUBE;
    h.neonD = dn;
    h.arch = mod(zi, 4.0);
    float df = p.y - BB_FLOOR;

    // The bubble column of this cell rises at its own rate.
    float cx = floor(p.x / BB_CELL);
    float cz = floor(p.z / BB_CELL);
    vec2 colId = vec2(cx, mod(cz, BB_ZWRAP / BB_CELL));
    float h1 = glassHash(colId + 0.37);
    float h2 = glassHash(colId + vec2(11.3, 5.1));
    float rise = gT * (0.22 + 0.16 * h2) + h1 * BB_CELL;
    float qy = p.y - rise;
    float cy = floor(qy / BB_CELL);
    vec3 f = vec3(p.x, qy, p.z) - (vec3(cx, cy, cz) + 0.5) * BB_CELL;
    float toEdge = BB_HALF - max(max(abs(f.x), abs(f.y)), abs(f.z));

    vec3 bid = vec3(colId, cy);
    float hb = glassHash3(bid + vec3(3.1, 0.7, 9.4));
    float hr = glassHash3(bid + vec3(17.2, 4.4, 1.3));
    float hf = glassHash3(bid + vec3(8.6, 13.1, 2.2));
    vec2 jit = (vec2(glassHash(colId + 2.9), glassHash(colId + 7.7)) - 0.5) * 0.4;
    float sw = hb * GLASS_TAU + 0.1 * SND_CURVED_TIME;
    vec3 c = vec3((cx + 0.5) * BB_CELL + jit.x + gSway * sin(gT * 0.6 + sw),
                  (cy + 0.5) * BB_CELL + rise,
                  (cz + 0.5) * BB_CELL + jit.y + gSway * cos(gT * 0.47 + sw * 1.3));
    float life = smoothstep(BB_BORN, BB_BORN + 1.6, c.y) * (1.0 - smoothstep(BB_POP - 1.2, BB_POP, c.y));
    float r = (0.4 + 0.45 * hr) * gSize * gBreath * life * smoothstep(0.7, 2.6, length(c - gEye));
    bool live = hf < gFill && cx >= -3.0 && cx <= 2.0 && r > 0.02;
    float ds = live ? length(p - c) - r : 1e3;
    float d = min(ds, toEdge + BB_GAP);
    // Every bubble is inside the hall box: x in +-12.5, y from -7.6 to 8.2.
    vec2 hall = max(abs(p.xy - vec2(0.0, 0.3)) - vec2(12.5, 7.9), 0.0);
    d = max(d, length(hall));
    h.c = c;
    h.r = r;
    h.id = hb * 37.0 + cy * 0.13;

    h.mat = 0;
    float dm = d;
    if (dn < dm) {
        dm = dn;
        h.mat = 1;
    }
    if (df < dm) {
        dm = df;
        h.mat = 2;
    }
    h.d = dm;
    return h;
}

//#include lib_glass_field

/** Both walls of one bubble, front to back; `tr` returns the light it lets through. */
vec3 shadeBubble(vec3 ro, vec3 rd, vec2 hs, vec3 c, float r, float id, GlassEar ear, GlassLook lk, float baseNm, float marble, out float tr) {
    vec3 add = vec3(0.0);
    tr = 1.0;
    for (int w = 0; w < 2; w++) {
        float tw = w == 0 ? hs.x : hs.y;
        vec3 p = ro + rd * tw;
        vec3 local = (p - c) / r;
        vec3 n = w == 0 ? local : -local;
        // The shiver: a ripple running down the bubble on the snare.
        float rip = sin(local.y * 14.0 - gT * 9.0 + id) * ear.snare * 0.12;
        n = normalize(n + rip * vec3(local.x, 0.0, local.z));
        n = glassWobble(n, local * 2.0, glassChaos());
        float cosI = abs(dot(n, rd));
        // Thickness: drained toward the top, marbled by a slow swirl round the vertical.
        vec3 sp = local;
        sp.xz = glassRot(gT * 0.25 + local.y * 2.0) * sp.xz;
        float swirl = glassNoise3(sp * 1.8 + vec3(id * 4.0, -gT * 0.15, 0.0));
        float drain = 0.25 + 0.75 * smoothstep(-1.0, 0.85, -local.y);
        float nm = baseNm * drain * (1.0 + marble * swirl);
        nm *= 1.0 - smoothstep(0.7, 0.96, local.y);
        vec3 film = glassFilm(nm, cosI, 1.33);
        float fr = mix(0.16, 1.0, pow(1.0 - cosI, 3.0));
        vec3 rr = reflect(rd, n);
        vec3 refl = pal(gHue + 0.3 + 0.2 * n.y) * 0.1 + neonMirror(p, rr, ear.frost * 0.08) * 1.4;
        // The fractal world only on the near wall: the far one is mostly the arches.
        if (w == 0) refl += glassEnvRough(rr, lk, ear.frost * 0.05);
        float k = w == 0 ? 1.0 : 0.6;
        vec3 lit = refl * film * fr * 2.6 * k;
        if (w == 0) {
            lit += vec3(1.0) * glassGlint(local * 1.4 + id, 4.0, ear.hat) * 0.6;
            lit += glassRim(cosI, gHue + fract(id), 0.7) * film * 1.5;
        }
        add += tr * lit;
        tr *= 1.0 - fr * 0.3 * k;
    }
    return add;
}

void main() {
    vec2 uv = view();
    glassSetup();
    gT = glassTime();
    GlassEar ear = glassListen();
    float bass = min(ear.bass, 1.2);
    gHue = 0.05 * sin(gT * 0.017) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue) + 0.25 * (gGlassIn.tone - 0.5);
    GlassLook lk = glassLook(GLASS_ENV_FRACTAL, gHue, ear);
    float baseNm = 280.0 + 440.0 * ear.height;
    float marble = 0.35 * clamp(2.0 * gGlassIn.texture, 0.0, 2.0);

    gFill = 0.35 + 0.35 * glassComplexity();
    gSize = 0.8 + 0.4 * gGlassIn.space;
    gBreath = 0.92 + 0.08 * bass * clamp(2.0 * gGlassIn.pulse, 0.0, 2.0);
    gSway = 0.25 * (0.5 + gGlassIn.flow);
    gHalfX = 13.5 + 2.0 * ear.width;
    gRoll = 0.06 * clamp(ear.pan, -2.0, 2.0);
    gChase = glassGlide(SND_ONSET_COUNT, SND_ONSET_ENV);
    gNeonGain = (0.9 + 0.1 * ear.trend) * (1.0 + clamp(gGlassIn.build - 0.5, -0.5, 0.5));

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 64 (see the header), so this
    // shift is exact and invisible, and it keeps highp z small far down the travel.
    ro.z = mod(ro.z, BB_ZWRAP);
    gEye = ro;

    vec3 fogCol = pal(gHue + 0.7) * 0.03;
    vec3 acc = vec3(0.0);
    vec3 glow = vec3(0.0);
    float thr = 1.0;
    int layers = 0;
    int maxLayers = uSteps < GLASS_TWO_BOUNCE_STEPS ? 2 : BB_LAYERS;
    float t = 0.05;
    float endT = BB_FAR;
    int endMat = -1;
    float endArch = 0.0;
    for (int i = 0; i < BB_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * t;
        GlassHit at = glassField(p);
        float d = at.d;
        // The arches' haze in the air this step crosses, dimmed by the bubbles in front.
        glow += thr * neonColour(at.arch) * 0.05 / (1.0 + 40.0 * at.neonD * at.neonD) * min(d, 1.0) * exp(-t * BB_FOG);
        if (d < glassEps(t)) {
            if (at.mat != 0) {
                endT = t;
                endMat = at.mat;
                endArch = at.arch;
                break;
            }
            vec2 hs = glassSphere(ro, rd, at.c, at.r);
            // Leaving the bubble just shaded (or a graze): step past it.
            if (hs.y <= t + 0.02) {
                t += 0.05;
                continue;
            }
            float tr;
            vec3 add = shadeBubble(ro, rd, hs, at.c, at.r, at.id, ear, lk, baseNm, marble, tr);
            float fogF = exp(-hs.x * BB_FOG);
            acc += thr * add * fogF;
            thr *= mix(1.0, tr, fogF);
            layers++;
            t = hs.y + 0.05;
            if (layers >= maxLayers || thr < 0.05) break;
            continue;
        }
        t += d;
        if (t > BB_FAR) break;
    }

    vec3 surf;
    if (endMat == 1) {
        // A neon tube, hot in the core.
        surf = neonColour(endArch) * 1.8 + 0.25 * gNeonGain;
        surf = mix(fogCol, surf, exp(-endT * BB_FOG));
    } else if (endMat == 2) {
        // The wet floor: dark, mirroring the arches and a little of the sky.
        vec3 p = ro + rd * endT;
        vec3 rr = reflect(rd, vec3(0.0, 1.0, 0.0));
        float cosI = clamp(-rd.y, 0.0, 1.0);
        float fres = mix(0.08, 1.0, pow(1.0 - cosI, 4.0));
        float wet = 0.6 + 0.4 * glassNoise(p.xz * 0.9);
        surf = pal(gHue + 0.6) * 0.015 + neonMirror(p, rr, 0.05 + 0.1 * ear.frost) * 0.7 * fres * wet;
        surf = mix(fogCol, surf, exp(-endT * BB_FOG));
    } else {
        surf = mix(fogCol, glassEnv(rd, lk), 0.75);
    }
    vec3 col = acc + thr * surf + glow;

    col *= 0.85 + 0.15 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(glassTone(col)), 1.0);
}
