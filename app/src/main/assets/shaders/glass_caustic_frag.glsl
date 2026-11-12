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

// Glass Caustic: a swim low over a sunlit sea floor. Rippled sand rolls away
// in long dunes into a blue haze, and glass spheres lie half sunk in it, one
// or none to every 8 x 8 patch. Across the sand crawls the caustic net of the
// waves overhead in three wavelengths (glassCausticRGB): the lines re-knot and
// split into thin rainbows where they fold. Each sphere throws its own
// focused, dispersed spot on the sand away from the sun, with a soft shadow
// round it, and shows the sea through it upside down, refracted with two
// bounces. As in the flat style the floor is alive: the family's FRACTAL world
// glows up through the sand in patches, like bioluminescence. Between eye and
// floor slanting shafts fall from the surface (a few taps along the ray, more
// at higher quality), and marine snow - real points in the water, three
// shells deep - drifts past as you swim. Look up and the rippled surface
// opens Snell's window onto the light; outside it, total internal reflection
// shows the dim sea.
//
// ---- camera -----------------------------------------------------------------
//
// camFly: the eye rides the rig's path down +z, its view pitched down toward
// the sand inside the rig's own frame (so yaw, roll and shake still turn it).
// The eye is held between SC_EYE_LO, above the tallest sphere, and SC_EYE_HI,
// under the surface, so the rig's wander never takes it into glass or air.
// Every z term has a period dividing 1024: the dunes, the sand ripples, the
// caustic waves (each wave's z wavenumber is a whole multiple of SC_KZ =
// 2pi/1024), the sphere cells (128 along z), the shaft and glow noise lattices (1280
// and 256 cells) and the snow lattice (1024). So the eye's z is rebased into 0..1024,
// exactly and invisibly, and the rig's 8192 wrap never shows.
//
// ---- audio ------------------------------------------------------------------
//
//   kick         a surge: the swell lifts and the net stretches, eased;
//   centroid     caustic sharpness: bright mixes cut fine bright lines;
//   flatness     turbidity: noise thickens the haze and softens the net;

//   pitch height the sun's height: shafts steepen as the melody climbs;
//   hat          the marine snow and the spheres glitter;
//   bass         the spheres' IOR, so their spots tighten on low notes;
//   snare        folds the fractal glow harder (GlassLook.warp).
//
// ---- macros and knobs -------------------------------------------------------
//
// Speed scales the waves and the current. Complexity adds finer sand ripples.
// Chaos stirs a cross-current and wobbles the glass. Energy scales every
// reaction. Knobs, each read as an offset from 0.5 (exactly this picture
// while Note & key reactions are off): k0 PULSE the kick's surge, k1 TEXTURE
// the sand ripples, k2 FLOW the cross-current, k3 TONE the hue, k4 BUILD the
// shafts, k5 SPACE how far you see (less haze).
//
// ---- why the estimate is safe ----------------------------------------------
//
// The sand is a heightfield, y = SC_FLOOR + dune(xz), and the march steps the
// vertical gap times SC_STEPK = 0.85 <= 1 / sqrt(1 + G^2), where G = 0.30
// bounds |grad dune| (summed term by term in duneH's comment): a true lower
// bound on the distance. The fine ripples are only in the normal, never the
// field. A sphere is measured exactly, and only in its own cell; every
// sphere stays 1.5 inside its cell (jitter 1.5, radius at most 1, half cell
// 4), so any other cell's sphere is at least toEdge + 1.5 away, and above
// SC_TOP no sphere reaches at all: the bound max(toEdge + 1.5, y - SC_TOP)
// caps the step. The march starts on the plane SC_TOP, above every sphere
// and dune, so rays that look up never march; a ray that runs out of Detail
// before it lands takes the flat floor.
//
// ---- cost -------------------------------------------------------------------
//
// At most SC_MAX_STEPS steps of two sines and one sphere, breaking on uSteps.
// Then one caustic wave sum, one fractal lookup on the sand or a two-bounce
// sphere trace, 4 / 6 / 8 shaft taps and three snow shells.

#define SC_MAX_STEPS 112
#define SC_FAR 38.0
#define SC_FLOOR -4.0
/** Above every dune (0.47) and sphere top (0.47 + 1.3): nothing to march above it. */
#define SC_TOP -2.2
#define SC_EYE_LO -1.7
#define SC_EYE_HI 2.6
/** The water surface overhead. */
#define SC_SURF 3.4
/** 0.9 / sqrt(1 + 0.30^2), rounded down: the heightfield's step factor. */
#define SC_STEPK 0.85
/** 2pi / 1024: every z wavenumber is a whole multiple of it. */
#define SC_KZ 0.0061359232
#define SC_ZWRAP 1024.0
/** Sphere cells: 8 world units, 128 of them along z before the hash repeats. */
#define SC_CELL 8.0
#define SC_ZCELLS 128.0

/** What the field holds at a point (lib_glass_field): the distance, the cell's ball (centre xyz, radius w; w <= 0: none) and the material (1 ball, 0 sand). */
struct GlassHit {
    float d;
    vec4 ball;
    float mat;
};

/**
 * Long dunes across the current, slowly swelling, plus shorter ridges.
 * |grad| bound: term one 0.35 * (0.21, 0.264 + 0.4 * 11 KZ) and term two
 * 0.12 * (0.8 * 1.3, 163 KZ), summed per axis: (0.199, 0.222), length 0.30.
 */
float duneH(vec2 xz) {
    float a = 0.35 * sin(xz.x * 0.21 + xz.y * (43.0 * SC_KZ) + 0.7) * (0.6 + 0.4 * sin(xz.y * (11.0 * SC_KZ)));
    return a + 0.12 * sin(xz.y * (163.0 * SC_KZ) + 0.8 * sin(xz.x * 1.3));
}

/** The sphere in `cell`, if it has one: centre and radius (w = 0 when empty). */
vec4 ballIn(vec2 cell) {
    vec2 key = vec2(cell.x, mod(cell.y, SC_ZCELLS));
    float h = glassHash(key + 0.37);
    if (h < 0.42) return vec4(0.0);
    vec2 j = glassHash2(key + vec2(17.0, 5.0));
    float r = 0.45 + 0.55 * fract(h * 7.31);
    vec2 c = (cell + 0.5) * SC_CELL + (j - 0.5) * 3.0;
    // Half sunk: the centre 0.3 r above the sand under it.
    return vec4(c.x, SC_FLOOR + duneH(c) + 0.3 * r, c.y, r);
}

GlassHit glassMap(vec3 p) {
    float dSand = (p.y - SC_FLOOR - duneH(p.xz)) * SC_STEPK;
    vec2 cell = floor(p.xz / SC_CELL);
    vec4 b = ballIn(cell);
    float dBall = b.w > 0.0 ? length(p - b.xyz) - b.w : 1e5;
    vec2 f = p.xz - (cell + 0.5) * SC_CELL;
    vec2 af = abs(f);
    float toEdge = 0.5 * SC_CELL - max(af.x, af.y);
    float d = min(dSand, max(toEdge + 1.5, p.y - SC_TOP));
    GlassHit h;
    h.mat = dBall < d ? 1.0 : 0.0;
    h.ball = b;
    h.d = min(d, dBall);
    return h;
}

//#include lib_glass_field

/**
 * The caustic waves: lib_glass's five golden-angle waves (glassWaves) with
 * each wavevector's z part rounded to a whole multiple of SC_KZ, so the net
 * repeats exactly over the z rebase. Phases take world xz; amplitudes and the
 * Hessian are in the lib's scaled space (world x 2.2), where glassCausticRGB's
 * focus depths were tuned. Returns the slope (for the surface normal).
 */
vec2 seaWaves(vec2 xz, float t, float chop, out vec3 hess) {
    vec2 g = vec2(0.0);
    vec3 hs = vec3(0.0);
    for (int i = 0; i < 5; i++) {
        vec2 kw;
        float k;
        if (i == 0) { kw = vec2(1.8157, 202.0 * SC_KZ); k = 1.0; }
        else if (i == 1) { kw = vec2(-3.1145, 72.0 * SC_KZ); k = 1.43; }
        else if (i == 2) { kw = vec2(2.8551, -567.0 * SC_KZ); k = 2.0449; }
        else if (i == 3) { kw = vec2(0.3479, 1047.0 * SC_KZ); k = 2.9242; }
        else { kw = vec2(-6.572, -1049.0 * SC_KZ); k = 4.1816; }
        vec2 kq = kw / 2.2;
        float amp = chop * 0.32 / (k * k);
        float ph = dot(kw, xz) + sqrt(k) * 1.6 * t + float(i) * 1.7;
        float s = sin(ph);
        g += amp * cos(ph) * kq;
        hs -= amp * s * vec3(kq.x * kq.x, kq.y * kq.y, kq.x * kq.y);
    }
    hess = hs;
    return g;
}

/** Value noise, -1..1, whose lattice repeats every `period` cells along y: 1024 world units over the scale it is read at. */
float seaNoise(vec2 p, float period) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float y0 = mod(i.y, period);
    float y1 = mod(i.y + 1.0, period);
    float a = glassHash(vec2(i.x, y0));
    float b = glassHash(vec2(i.x + 1.0, y0));
    float c = glassHash(vec2(i.x, y1));
    float d = glassHash(vec2(i.x + 1.0, y1));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 2.0 - 1.0;
}

/** Sand ripples for the normal only: long ones across the current, finer ones with Complexity and TEXTURE. */
float ripH(vec2 sp, float fine) {
    float r = sin(sp.y * (815.0 * SC_KZ) + 0.8 * sin(sp.x * 1.3)) * 0.5 + 0.5;
    r += fine * (sin(sp.y * (2119.0 * SC_KZ) + sp.x * 3.0) * 0.5 + 0.5);
    return r;
}

void main() {
    vec2 uv = view();
    glassSetup();
    float t = glassTime();
    GlassEar ear = glassListen();

    float k0 = gGlassIn.pulse;
    float k1 = gGlassIn.texture;
    float k2 = gGlassIn.flow;
    float k3 = gGlassIn.tone;
    float k4 = gGlassIn.build;
    float k5 = gGlassIn.space;

    float hue = 0.05 * sin(t * 0.02) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue) + 0.25 * (k3 - 0.5);
    GlassLook lk = glassLook(GLASS_ENV_FRACTAL, hue, ear);
    // The spheres look into the sea they lie in.
    GlassLook lkSea = lk;
    lkSea.env = GLASS_ENV_DEEP;
    vec3 key = glassKeyDir(lk);
    vec3 sunDir = normalize(vec3(key.x, max(key.y, 0.4), key.z));

    // The rig's ray, pitched down toward the sand in the rig's own frame.
    vec3 v = vec3(uv, uCamFocal);
    float pitch = 0.34;
    v.yz = mat2(cos(pitch), sin(pitch), -sin(pitch), cos(pitch)) * v.yz;
    vec3 rd = normalize(uCamBasis * v);
    vec3 ro = uCamPos;
    // Exact, invisible rebase (see the header): far down the 8192 travel a
    // highp z keeps ~0.001 of fraction, and the caustics would speckle.
    ro.z = mod(ro.z, SC_ZWRAP);
    ro.y = clamp(ro.y, SC_EYE_LO, SC_EYE_HI);

    vec3 water = mix(vec3(0.03, 0.3, 0.42), pal(hue + 0.5), 0.35);
    float haze = (0.07 + 0.1 * ear.frost) * (1.5 - k5);
    vec3 fogCol = water * 0.12;
    vec3 col = fogCol;
    float tHit = SC_FAR;

    float chop = 0.75 + 0.5 * ear.kick * (0.5 + k0) + 0.2 * clamp(ear.bass, 0.0, 1.2);
    float sharp = mix(0.06, 0.014, ear.sharp) * (1.0 + 1.5 * ear.frost);
    float ior = glassIor(1.45 + 0.15 * clamp(ear.bass, 0.0, 1.0));
    vec2 drift = vec2(0.05 * t, 0.0);

    if (rd.y < -0.001) {
        float tm = max((ro.y - SC_TOP) / -rd.y, 0.0);
        float hit = -1.0;
        float mat = 0.0;
        vec4 ball = vec4(0.0);
        for (int i = 0; i < SC_MAX_STEPS; i++) {
            // uSteps is the user's Detail; this loop's own bound is the constant.
            if (float(i) >= uSteps) break;
            if (tm > SC_FAR) break;
            GlassHit at = glassField(ro + rd * tm);
            float d = at.d;
            if (d < glassEps(tm)) {
                hit = tm;
                mat = at.mat;
                ball = at.ball;
                break;
            }
            tm += d;
        }
        // Out of Detail before landing: the flat floor, where the ray meets it.
        if (hit < 0.0) {
            float tf = (ro.y - SC_FLOOR) / -rd.y;
            if (tf < SC_FAR) hit = tf;
        }

        if (hit > 0.0 && mat > 0.5) {
            tHit = hit;
            vec3 p = ro + rd * hit;
            vec3 n = glassWobble(normalize(p - ball.xyz), p, glassChaos());
            float cosI = max(dot(n, -rd), 0.0);
            float fr = glassFresnel(cosI, ior);
            vec3 tint = mix(vec3(1.0), water * 2.0, 0.3);
            vec3 through = glassSphereTrace(p, n, rd, ball.xyz, ball.w, ior, glassSpread(0.02 + 0.03 * ear.sharp), tint, 0.4, glassBounces(), lkSea);
            vec3 rr = reflect(rd, n);
            col = through * (1.0 - fr) * 1.3 + glassEnv(rr, lkSea) * fr;
            col += vec3(1.0, 0.97, 0.9) * 3.0 * pow(max(dot(rr, sunDir), 0.0), 180.0);
            col += glassRim(cosI, hue, 1.0);
            col += mix(water, vec3(1.0), 0.5) * pow(1.0 - cosI, 3.0) * 0.4;
            col += vec3(0.9, 0.97, 1.0) * glassGlint(p, 9.0, ear.hat) * (0.3 + fr);
        } else if (hit > 0.0) {
            tHit = hit;
            vec3 fp = ro + rd * hit;
            vec2 sp = fp.xz + drift;
            // The normal: the dunes, then the ripples on them (bump only).
            float e = 0.02 + 0.001 * hit;
            float fine = (glassComplexity() * 0.4 + 0.1) * (0.5 + k1);
            float rw = 0.025 * (0.5 + k1) * (1.0 - smoothstep(8.0, 24.0, hit));
            float h0 = duneH(fp.xz) + rw * ripH(sp, fine);
            float hx = duneH(fp.xz + vec2(e, 0.0)) + rw * ripH(sp + vec2(e, 0.0), fine);
            float hz = duneH(fp.xz + vec2(0.0, e)) + rw * ripH(sp + vec2(0.0, e), fine);
            vec3 n = normalize(vec3(h0 - hx, e, h0 - hz));
            float rip = ripH(sp, fine);

            // Sand: pale and grained, a few dark stones (cells 1.5 a unit: 1536 along z).
            vec2 sc = sp * 1.5;
            vec2 sci = floor(sc);
            float stone = smoothstep(0.86, 0.9, glassHash(vec2(sci.x, mod(sci.y, 1536.0))) * (1.0 - length(fract(sc) - 0.5)));
            vec3 sand = mix(vec3(0.75, 0.68, 0.52), pal(hue + 0.15), 0.2) * (0.55 + 0.25 * rip / (1.0 + fine));
            sand = mix(sand, vec3(0.08, 0.07, 0.07), stone * 0.8);
            float dif = 0.55 + 0.45 * max(dot(n, sunDir), 0.0);
            sand *= dif;
            // The sea floor is alive: the FRACTAL world glows through the sand in
            // patches. Mirrored by sines so the lookup stays bounded and z-periodic.
            vec2 fq = vec2(sin(sp.x * 0.31), sin(sp.y * (51.0 * SC_KZ))) * 1.4;
            float patchy = smoothstep(-0.2, 0.7, seaNoise(sp * 0.25, 256.0));
            sand = sand * 0.4 + glassFractal2(fq, lk, 0.0) * (0.5 + 1.1 * patchy);

            // The net: waves at the point above, shifted toward the sun so it leans with the light.
            vec3 hess;
            float cur = glassChaos() * 0.3 + 0.2 * (k2 - 0.5);
            vec2 curr = cur * vec2(sin(sp.y * (163.0 * SC_KZ) + t), cos(sp.x - t));
            seaWaves(fp.xz + (sunDir.xz * 1.5 + curr) / 2.2, t * 0.9, chop, hess);
            vec3 caus = glassCausticRGB(hess, 2.4, 0.03 + 0.04 * ear.sharp, sharp);
            // Far lines are finer than a pixel: let them settle to their mean instead of shimmering.
            caus = mix(caus, vec3(0.3), smoothstep(10.0, 30.0, hit));

            // The sphere in this patch: its shadow and its focused spot, cast away from the sun.
            vec4 b = ballIn(floor(fp.xz / SC_CELL));
            float shadow = 1.0;
            vec3 spot = vec3(0.0);
            if (b.w > 0.0) {
                float lift = b.y - (SC_FLOOR + duneH(b.xz));
                vec2 fc = b.xz - sunDir.xz / sunDir.y * lift;
                vec2 rel = fp.xz - fc + sunDir.xz * 0.8 * b.w;
                float s2 = 1.0 / (b.w * b.w);
                float r2 = dot(rel, rel) * s2;
                vec2 dc = fp.xz - fc;
                shadow = 1.0 - 0.6 * exp(-dot(dc, dc) * s2 * 0.8);
                float k = (14.0 + 30.0 * (ior - 1.45)) * 0.49;
                spot = vec3(exp(-r2 * k * 0.8), exp(-r2 * k), exp(-r2 * k * 1.25)) * 2.2;
            }
            vec3 sun = mix(vec3(1.0, 0.95, 0.8), water * 2.0, 0.25);
            col = sand * (0.45 + 1.8 * caus * sun * shadow + spot * sun);
        }
    } else if (rd.y > 0.001) {
        // The surface overhead: Snell's window onto the light, the dim sea mirrored outside it.
        float ts = (SC_SURF - ro.y) / rd.y;
        if (ts < SC_FAR) {
            tHit = ts;
            vec3 sp = ro + rd * ts;
            vec3 hess;
            vec2 g = seaWaves(sp.xz, t * 0.9, chop, hess);
            vec3 nDown = -normalize(vec3(-g.x * 0.8, 1.0, -g.y * 0.8));
            vec3 tr = refract(rd, nDown, 1.33);
            if (dot(tr, tr) < 1e-6) {
                col = water * 0.22 + glassEnv(reflect(rd, nDown), lkSea) * 0.15;
            } else {
                vec3 sky = mix(water, vec3(1.0), 0.6) * 0.9 + glassEnv(tr, lk) * 0.6;
                sky += vec3(1.0, 0.96, 0.85) * 3.0 * pow(max(dot(tr, sunDir), 0.0), 60.0);
                // The window's rim burns brightest, as the refracted horizon crowds into it.
                float edge = 1.0 - clamp(dot(tr, vec3(0.0, 1.0, 0.0)), 0.0, 1.0);
                col = sky * (0.7 + 0.8 * edge * edge);
            }
        }
    }

    // The water between: absorption, then the shafts.
    float fog = 1.0 - exp(-tHit * haze);
    col = mix(col, fogCol, fog);
    float q = glassQuality();
    int taps = q < 0.3 ? 4 : (q < 0.6 ? 6 : 8);
    float shafts = 0.0;
    float span = min(tHit, 14.0);
    float j = glassHash(uv * 400.0);
    for (int i = 0; i < 8; i++) {
        if (i >= taps) break;
        float fi = (float(i) + j) / float(taps);
        vec3 x = ro + rd * span * fi;
        // Follow the sun's ray up to the surface: the shaft is the light that came through there.
        vec2 s = x.xz - sunDir.xz / sunDir.y * (SC_SURF - x.y);
        float sh = seaNoise(s * 1.25 + t * 0.15, 1280.0) * 0.5 + 0.5;
        shafts += pow(sh, 4.0) * exp(-span * fi * 0.15);
    }
    col += mix(water, vec3(1.0), 0.4) * shafts / float(taps) * 0.9 * lk.glow * (0.5 + k4);

    // Marine snow: real specks in the water, gathered from three shells round
    // the eye, each speck faded by how far it sits from its shell.
    for (int l = 0; l < 3; l++) {
        float dl = 1.3 + 1.4 * float(l);
        if (dl > tHit) break;
        vec3 sp = ro + rd * dl + vec3(0.0, t * 0.05, 0.0);
        vec3 ci = floor(sp);
        vec3 key3 = vec3(ci.x, ci.y, mod(ci.z, SC_ZWRAP));
        float h = glassHash3(key3 + float(l) * 7.0);
        if (h > 0.9) {
            vec3 o = vec3(glassHash3(key3 + 3.1), glassHash3(key3 + 5.7), glassHash3(key3 + 9.3)) - 0.5;
            vec3 c = ci + 0.5 + o * 0.7 - vec3(0.0, t * 0.05, 0.0);
            vec3 w = c - ro;
            float tc = dot(w, rd);
            if (tc > 0.2) {
                float dp = length(w - rd * tc) / (0.012 * tc);
                float shell = exp(-(tc - dl) * (tc - dl) * 12.0);
                float glit = 0.15 + 0.9 * ear.hat * step(0.6, fract(h * 17.0 + t));
                col += vec3(0.85, 0.95, 1.0) * exp(-dp * dp) * shell * glit * 0.6;
            }
        }
    }

    col *= 0.85 + 0.15 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(glassTone(col)), 1.0);
}
