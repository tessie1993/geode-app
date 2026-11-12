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

// Glass Prism: a real triangular prism, a solid bar of glass standing in a
// dark hazy room. A white beam crosses the room, strikes its left face and
// leaves the right as a fan of rainbow that sweeps through the haze and lands
// on a screen wall as a spectrum band; the screen's colour spills onto a glossy
// floor that mirrors it. The eye walks round the bench and sees all of it in
// depth: the beam as a lit shaft, the fan as a sheet of coloured light, the
// glass as a body that refracts the room behind it.
//
// ---- the optics ---------------------------------------------------------------
//
// The beam travels in the prism's cross-section (the plane z = 0), so its
// optics stay exact in 2D, as before: the beam is clipped against the
// triangle's three faces, refracted in by Snell, crossed to the face it reaches
// first and refracted out, once per wavelength (glassIors). The fan's
// wavelength at any point of the plane comes from its angle about the fan's
// virtual apex, so the band is continuous for the cost of three traces. The
// exit face's Fresnel share reflects back inside and leaves by the third face
// as a faint ghost fan, as every real prism throws.
//
// In 3D the light is a thin slab about z = 0. A view ray crosses that slab
// once, so the fan, the ghost and the motes are read where the ray meets the
// plane, brightened by 1 / |rd.z| (the slab's line integral, clamped at
// grazing). The beam is a Gaussian tube: its glow is the closed-form line
// integral at the ray's closest approach to the segment. Both are exact,
// alias-free and cost nothing per step. The soft haze round the shafts is a
// short fixed-step march (below).
//
// The glass body is a convex polyhedron (three faces and two end caps), so
// the view ray meets it EXACTLY by clipping against its five planes, as Glass
// Gem does: entry, Fresnel reflection of the room, refraction across, exit per
// wavelength into the room behind (the screen, the floor, the fractal dark),
// and from Balanced up a second internal bounce (glassBounces). Where the
// refracted ray crosses the beam's plane inside the glass, the beam's own path
// glows. The edges catch the light, brightest on the faces turned to the beam.
//
// ---- camera -------------------------------------------------------------------
//
// camAround with no spin: the eye sits on the rig's sphere round the bench's
// centre, a quarter turn off the screen's normal so the fan runs into depth
// (and fits a portrait phone), drifting slowly from side to side, a little
// above the beam. The rig's look, dolly and shake move it as for every object
// style. No term depends on uCamTravel, so the rig's 8192 travel wrap cannot
// show.
//
// ---- audio --------------------------------------------------------------------
//
//   centroid       the fan's width: bright mixes spray a broad rainbow, dull ones a
//                  narrow ribbon (the IOR spread);
//   pitch height   the beam's angle of incidence: the fan swings up and down the
//                  screen with the melody;
//   snare          fires a bright packet of light down the beam, through the glass
//                  and out across the fan;
//   bass           the beam's width;
//   pan            rocks the prism on its axis;
//   flatness       thickens the haze (frost): noise fills the room with light;
//   hat            dust motes sparkle in the beams.
//
// ---- macros and knobs ------------------------------------------------------------
//
// Speed scales the rock and the camera's drift. Complexity raises the base
// IOR, bending the fan further. Chaos jitters the faces, so the fan shivers,
// and wobbles the glass's normals. Energy scales every reaction. Colour: the
// room and a share of the rainbow from pal(). Space is the rig.
// Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE the snare
// packet, k1 TEXTURE the smoke's contrast, k2 FLOW the soft haze glow, k3 TONE
// the hue, k4 BUILD the fan's brightness, k5 SPACE the dispersion.
//
// ---- why there is no distance estimate to break -------------------------------
//
// Nothing here is sphere-traced. The prism, floor and screen are planes met in
// closed form; the light sheets and the beam are integrated analytically. The
// only loop over distance is the haze march, which takes a fixed number of
// equal steps (Detail sets how many, PR_HAZE_MAX caps it) between the eye and
// the first solid hit, so it can neither overshoot a surface nor run long.
//
// ---- cost ---------------------------------------------------------------------
//
// Four 3-plane traces per pixel (three wavelengths and the ghost), one 5-plane
// clip, one to six room lookups (six only on the glass), 8 to 32 haze steps of
// a segment distance and an angle each. No noise texture, no SDF.

#define PR_IOR 1.5
#define PR_INRADIUS 0.4
/** Half-length of the bar along z. */
#define PR_LEN 0.9
#define PR_SCREEN 1.55
#define PR_SCREEN_HALF 1.5
#define PR_SCREEN_TOP 1.35
#define PR_FLOOR -1.05
#define PR_CAM_R 4.2
/** Compile-time ceiling on the haze march; the body breaks on uSteps. */
#define PR_HAZE_MAX 32
#define PR_FAR 1e4

vec2 gN[3];
vec2 gC;
vec4 gPl[5];

// The light's path in the plane z = 0, set once in main.
vec2 gA;
vec2 gD;
vec2 gPin;
vec2 gPeG;
vec2 gDG;
vec2 gDR;
vec2 gApex;
float gAngB;
vec2 gOut;
vec2 gGDir;
float gGhostK;
float gSpread;
float gR;
float gHue;
GlassLook gLk;

float cross2(vec2 a, vec2 b) {
    return a.x * b.y - a.y * b.x;
}

vec2 refract2(vec2 i, vec2 n, float eta) {
    vec2 r = refract(i, n, eta);
    return dot(r, r) < 1e-6 ? reflect(i, n) : r;
}

/** The ray a + t d against the triangle: (t in, t out) and the faces it enters and leaves by. */
vec2 prismClip(vec2 a, vec2 d, out int fin, out int fout) {
    float tin = -1e5;
    float tout = 1e5;
    fin = 0;
    fout = 0;
    for (int i = 0; i < 3; i++) {
        float den = dot(gN[i], d);
        float num = PR_INRADIUS - dot(gN[i], a - gC);
        if (abs(den) < 1e-6) {
            if (num < 0.0) tout = -1e5;
        } else {
            float tt = num / den;
            if (den < 0.0) {
                if (tt > tin) {
                    tin = tt;
                    fin = i;
                }
            } else if (tt < tout) {
                tout = tt;
                fout = i;
            }
        }
    }
    return vec2(tin, tout);
}

/** One wavelength through the prism: entry point, exit point and exit direction. */
void prismTrace(vec2 a, vec2 d, float ior, out vec2 pin, out vec2 pout, out vec2 dir) {
    int fi;
    int fo;
    vec2 tt = prismClip(a, d, fi, fo);
    pin = a + d * tt.x;
    vec2 d1 = refract2(d, gN[fi], 1.0 / ior);
    vec2 start = pin + d1 * 1e-3;
    vec2 t2 = prismClip(start, d1, fi, fo);
    pout = start + d1 * t2.y;
    dir = normalize(refract2(d1, -gN[fo], ior));
}

float segDist(vec2 p, vec2 a, vec2 b) {
    vec2 ab = b - a;
    float h = clamp(dot(p - a, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
    return length(p - a - ab * h);
}

/** Signed distance-like measure to the triangle in the plane: > 0 outside. */
float insideTri(vec2 p) {
    float m = -1e3;
    for (int i = 0; i < 3; i++) m = max(m, dot(gN[i], p - gC) - PR_INRADIUS);
    return m;
}

vec3 rainbow(float s) {
    return mix(glassSpectrum(clamp(s, 0.0, 1.0)), pal(gHue + 0.7 * s), 0.25);
}

/** The fan at a point of the plane: its weight, the wavelength s (0 red, 1 violet) and the distance along it. */
float fanAt(vec2 p, out float s, out float along) {
    vec2 u = p - gApex + vec2(1e-5, 0.0);
    float angU = atan(cross2(gDR, u), dot(gDR, u));
    s = angU / gAngB;
    along = dot(p - gPeG, gDG);
    float band = smoothstep(-0.08, 0.06, s) * (1.0 - smoothstep(0.94, 1.08, s));
    return band * smoothstep(0.0, 0.05, along) * step(0.0, dot(u, gDG)) * step(p.x, PR_SCREEN);
}

/** The ghost fan's ray at a point of the plane: a soft line widening with distance. */
float ghostAt(vec2 p) {
    vec2 gu = p - gOut;
    float ga = dot(gu, gGDir);
    float gd = length(gu - gGDir * max(ga, 0.0));
    float gw = 0.004 + 0.03 * max(ga, 0.0) * gSpread * 4.0;
    return gGhostK * exp(-gd * gd / (gw * gw)) * step(0.0, ga) * step(p.x, PR_SCREEN);
}

/** The screen wall's colour at a point on it: the spectrum band where the fan lands, the ghost's dot. */
vec3 screenAt(vec3 h) {
    float s;
    float along;
    vec2 p = vec2(PR_SCREEN - 1e-3, h.y);
    float band = fanAt(p, s, along);
    float wz = gR * (1.0 + 0.3 * max(along, 0.0));
    float zf = exp(-h.z * h.z / (wz * wz)) * gR / wz;
    float halo = exp(-abs(h.z) / (wz * 6.0)) * 0.12;
    vec3 col = pal(gHue + 0.6) * 0.035;
    col += rainbow(s) * band * (zf * 1.8 + halo);
    col += mix(vec3(1.0), pal(gHue + 0.3), 0.3) * ghostAt(p) * zf * 0.8;
    return col;
}

/** Ray against the screen panel: the hit distance or PR_FAR. */
float screenHit(vec3 ro, vec3 rd) {
    if (rd.x <= 1e-4) return PR_FAR;
    float t = (PR_SCREEN - ro.x) / rd.x;
    if (t <= 0.0) return PR_FAR;
    vec3 h = ro + rd * t;
    if (abs(h.z) > PR_SCREEN_HALF || h.y > PR_SCREEN_TOP || h.y < PR_FLOOR) return PR_FAR;
    return t;
}

/** The room without the glass: screen, glossy floor, and the family's fractal dark beyond. `th` is the hit distance. */
vec3 stage(vec3 ro, vec3 rd, out float th) {
    float tS = screenHit(ro, rd);
    float tF = PR_FAR;
    if (rd.y < -1e-4) tF = (PR_FLOOR - ro.y) / rd.y;
    if (tF <= 0.0) tF = PR_FAR;
    th = min(tS, tF);
    if (tS < tF) return screenAt(ro + rd * tS);
    if (tF < PR_FAR) {
        vec3 h = ro + rd * tF;
        vec3 col = pal(gHue + 0.55) * 0.02;
        // Faint tiles for depth, fading with distance.
        vec2 g = abs(fract(h.xz * 2.0) - 0.5);
        col += pal(gHue + 0.6) * 0.025 * (1.0 - smoothstep(0.0, 0.03, min(g.x, g.y))) * exp(-tF * 0.25);
        // The screen's colour spills on the floor in front of it.
        vec3 spill = rainbow(0.5) * 0.5 + vec3(0.12);
        col += spill * 0.16 * exp(-max(PR_SCREEN - h.x, 0.0) * 1.8) * exp(-h.z * h.z * 1.5) * step(h.x, PR_SCREEN);
        // The prism's glow pooled under it.
        vec2 dp = h.xz - vec2(gC.x, 0.0);
        col += vec3(0.9, 0.95, 1.0) * 0.05 * exp(-dot(dp, dp) * 3.0);
        // Gloss: the screen mirrored in the floor, with Schlick's falloff.
        vec3 rr = vec3(rd.x, -rd.y, rd.z);
        float tm = screenHit(h, rr);
        if (tm < PR_FAR) {
            float fr = 0.04 + 0.96 * pow(1.0 - clamp(-rd.y, 0.0, 1.0), 5.0);
            col += screenAt(h + rr * tm) * (0.12 + 0.6 * fr) * exp(-tm * 0.6);
        }
        return col;
    }
    return glassEnv(rd, gLk) * 0.4;
}

/** The glass bar: (t near, t far) and the entry face's normal. near > far is a miss. */
vec2 barEnter(vec3 ro, vec3 rd, out vec3 n) {
    float tn = -1e4;
    float tf = 1e4;
    n = vec3(0.0, 1.0, 0.0);
    for (int i = 0; i < 5; i++) {
        vec3 pn = gPl[i].xyz;
        float den = dot(rd, pn);
        float num = gPl[i].w - dot(ro, pn);
        if (abs(den) < 1e-6) {
            if (num < 0.0) return vec2(1.0, -1.0);
            continue;
        }
        float t = num / den;
        if (den < 0.0) {
            if (t > tn) {
                tn = t;
                n = pn;
            }
        } else {
            tf = min(tf, t);
        }
    }
    return vec2(tn, tf);
}

/** From a point inside, the distance to the face the ray leaves by, and that face's outward normal. */
float barExit(vec3 p, vec3 rd, out vec3 n) {
    float tf = 1e4;
    n = vec3(0.0, 1.0, 0.0);
    for (int i = 0; i < 5; i++) {
        vec3 pn = gPl[i].xyz;
        float den = dot(rd, pn);
        if (den > 1e-6) {
            float t = (gPl[i].w - dot(p, pn)) / den;
            if (t < tf) {
                tf = t;
                n = pn;
            }
        }
    }
    return max(tf, 0.0);
}

/** The beam's glow along a ray: the Gaussian tube's line integral at closest approach, before `tEnd`. */
vec2 beamAlong(vec3 ro, vec3 rd, float tEnd, out float bAlong) {
    vec3 A = vec3(gA, 0.0);
    vec3 D = vec3(gD, 0.0);
    float len = length(gPin - gA);
    vec3 w0 = ro - A;
    float b = dot(rd, D);
    float dd = dot(rd, w0);
    float e = dot(D, w0);
    float den = max(1.0 - b * b, 1e-4);
    float s = clamp((e - b * dd) / den, 0.0, len);
    float t = max(dot(A + D * s - ro, rd), 0.0);
    float dist = length(ro + rd * t - A - D * s);
    bAlong = s - len;
    float sinA = max(sqrt(den), 0.25);
    float vis = step(t, tEnd);
    return vec2(exp(-dist * dist / (gR * gR)), exp(-dist * 9.0 / (gR * 30.0))) * vis / sinA;
}

void main() {
    vec2 uv = view();
    glassSetup();
    float t = glassTime();
    GlassEar ear = glassListen();
    float k0 = gGlassIn.pulse - 0.5;
    float k1 = gGlassIn.texture - 0.5;
    float k2 = gGlassIn.flow - 0.5;
    float k3 = gGlassIn.tone - 0.5;
    float k4 = gGlassIn.build - 0.5;
    float k5 = gGlassIn.space - 0.5;

    gSpread = glassSpread((0.015 + 0.045 * ear.sharp) * (1.0 + 0.9 * k5));
    float ior = glassIor(PR_IOR + 0.12 * glassComplexity());
    gHue = 0.05 * sin(t * 0.02) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue) + 0.3 * k3;
    float chaos = glassChaos();
    float haze = (0.6 + 0.8 * ear.frost) * (1.0 + 0.8 * k1);
    float bw = 1.0 + 1.5 * clamp(ear.bass, 0.0, 1.2);
    gR = 0.028 * bw;
    float fanGain = 1.0 + 0.9 * k4;
    gLk = glassLook(GLASS_ENV_FRACTAL, gHue, ear);

    // ---- the 2D optics in the plane z = 0 ----
    gC = vec2(-0.45, -0.05);
    float rock = 0.42 + 0.07 * sin(t * 0.11) + 0.12 * ear.pan;
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        float a = rock - 1.5707963 + fi * 2.0943951 + chaos * 0.05 * sin(t * 0.9 + fi * 2.3);
        gN[i] = vec2(cos(a), sin(a));
        gPl[i] = vec4(gN[i], 0.0, PR_INRADIUS + dot(gN[i], gC));
    }
    gPl[3] = vec4(0.0, 0.0, 1.0, PR_LEN);
    gPl[4] = vec4(0.0, 0.0, -1.0, PR_LEN);

    vec2 aim = gC + gN[2] * PR_INRADIUS;
    gD = normalize(vec2(1.0, 0.3 + 0.32 * ear.height + 0.03 * sin(t * 0.09)));
    gA = aim - gD * 6.0;

    vec3 iors = glassIors(ior, gSpread);
    vec2 peR;
    vec2 peB;
    vec2 dB;
    prismTrace(gA, gD, iors.x, gPin, peR, gDR);
    prismTrace(gA, gD, iors.y, gPin, gPeG, gDG);
    prismTrace(gA, gD, iors.z, gPin, peB, dB);

    // The ghost: the exit face's reflected share crosses to the third face and leaves.
    int fi0;
    int fo0;
    vec2 d1p = refract2(gD, gN[2], 1.0 / ior);
    prismClip(gPin + d1p * 1e-3, d1p, fi0, fo0);
    vec2 dIn = reflect(d1p, -gN[fo0]);
    vec2 gStart = gPeG + dIn * 1e-3;
    int fi1;
    int fo1;
    vec2 t2 = prismClip(gStart, dIn, fi1, fo1);
    gOut = gStart + dIn * t2.y;
    gGDir = normalize(refract2(dIn, -gN[fo1], ior));
    gGhostK = glassFresnel(abs(dot(d1p, gN[fo0])), ior) * 4.0;

    // The fan's virtual apex and angular width.
    float cr = cross2(gDR, dB);
    gApex = gPeG - gDG;
    if (abs(cr) > 1e-4) gApex = peR + gDR * (cross2(peB - peR, dB) / cr);
    float angB = atan(cr, dot(gDR, dB));
    gAngB = angB < 0.0 ? min(angB, -1e-3) : max(angB, 1e-3);

    // ---- the eye ----
    vec3 ro;
    vec3 rd;
    camAround(uv, PR_CAM_R, 0.0, ro, rd);
    float yaw = 0.55 + 0.22 * sin(t * 0.04);
    float pitch = 0.16 + 0.05 * sin(t * 0.03 + 1.0);
    mat3 ry = mat3(cos(yaw), 0.0, -sin(yaw), 0.0, 1.0, 0.0, sin(yaw), 0.0, cos(yaw));
    mat3 rx = mat3(1.0, 0.0, 0.0, 0.0, cos(pitch), sin(pitch), 0.0, -sin(pitch), cos(pitch));
    mat3 turn = ry * rx;
    vec3 centre = vec3(0.45, -0.1, 0.0);
    ro = centre + turn * ro;
    rd = normalize(turn * rd);

    // ---- what the ray meets ----
    vec3 nIn;
    vec2 bar = barEnter(ro, rd, nIn);
    bool onGlass = bar.x < bar.y && bar.y > 0.0 && bar.x > 0.0;
    float tStage;
    vec3 col = stage(ro, rd, tStage);
    float tEnd = onGlass ? bar.x : tStage;
    float pulseAt = (1.0 - clamp(SND_SNARE_ENV, 0.0, 1.0)) * 9.0 - 6.0;
    float pulseK = ear.snare * (1.0 + 1.4 * k0);

    if (onGlass) {
        vec3 p = ro + rd * bar.x;
        vec3 n = glassWobble(nIn, p, 0.5 * chaos);
        float cosI = max(dot(n, -rd), 0.0);
        float fr = glassFresnel(cosI, ior);
        float th;
        vec3 acc = stage(p + n * 1e-3, reflect(rd, n), th) * fr;
        vec3 dIn3 = glassRefract(rd, n, 1.0 / ior);
        vec3 nx;
        float tx = barExit(p, dIn3, nx);
        vec3 q = p + dIn3 * tx;
        vec3 tint = mix(vec3(1.0), pal(gHue + 0.55), 0.15);
        vec3 absorb = glassBeer(tint, tx, 0.6);
        float fExit = glassFresnel(max(dot(dIn3, nx), 0.0), ior);
        vec3 er = glassRefract(dIn3, -nx, iors.x);
        vec3 eg = glassRefract(dIn3, -nx, iors.y);
        vec3 eb = glassRefract(dIn3, -nx, iors.z);
        vec3 qo = q + nx * 1e-3;
        vec3 behind = vec3(stage(qo, er, th).r, stage(qo, eg, th).g, stage(qo, eb, th).b);
        acc += behind * absorb * (1.0 - fExit) * 1.15;
        if (glassBounces() > 1) {
            vec3 d2 = reflect(dIn3, nx);
            vec3 n3;
            float t3 = barExit(q + d2 * 1e-3, d2, n3);
            vec3 q3 = q + d2 * (t3 + 1e-3);
            vec3 e2 = glassRefract(d2, -n3, ior);
            acc += stage(q3 + n3 * 1e-3, e2, th) * absorb * absorb * fExit * 1.6;
        }
        // The beam's path inside the glass, where the refracted view ray crosses its plane.
        if (abs(dIn3.z) > 1e-4) {
            float tz = -p.z / dIn3.z;
            if (tz > 0.0 && tz < tx) {
                vec2 c = (p + dIn3 * tz).xy;
                float inner = segDist(c, gPin, gPeG);
                float ginner = segDist(c, gPeG, gOut);
                float sg = 1.0 / max(abs(dIn3.z), 0.3);
                float ia = dot(c - gPin, gD);
                float ip = pulseK * exp(-(ia - pulseAt) * (ia - pulseAt) * 8.0) * 2.0;
                acc += mix(vec3(1.0), glassSpectrum(0.5), 0.3) * (exp(-inner * inner * 3000.0) * (0.8 + ip) + exp(-inner * 18.0) * 0.12) * sg;
                acc += vec3(0.8, 0.85, 1.0) * exp(-ginner * ginner * 5000.0) * gGhostK * 0.5 * sg;
            }
        }
        // Edges: how near the entry point is to each other face; the side faces turned to the beam shine most.
        float edge = 0.0;
        for (int i = 0; i < 5; i++) {
            float e = gPl[i].w - dot(gPl[i].xyz, p);
            float notEntry = step(1e-3, length(gPl[i].xyz - nIn));
            // Caps have no in-plane normal, so they read 0.3 here.
            float facing = 0.3 + 0.7 * max(dot(gPl[i].xy, -gD), 0.0);
            edge += notEntry * (exp(-e * 90.0) * 0.25 + exp(-e * 140.0) * 0.6 * facing);
        }
        acc += vec3(0.85, 0.9, 1.0) * edge;
        acc += glassRim(cosI, gHue + 0.55, 0.2);
        acc += vec3(1.0) * glassGlint(p, 9.0, ear.hat * 0.5);
        col = acc;
    }

    // ---- the light in the air, in front of the first solid hit ----
    // The beam: a lit shaft, with the snare's packet running down it.
    float bAlong;
    vec2 beam = beamAlong(ro, rd, tEnd, bAlong);
    float bPulse = pulseK * exp(-(bAlong - pulseAt) * (bAlong - pulseAt) * 8.0) * 3.0;
    float dustK = 1.0 + 0.6 * k1;
    col += vec3(1.0, 0.98, 0.94) * (beam.x * (1.1 + bPulse) * 0.6 + beam.y * 0.05 * haze);

    // The sheet: fan, ghost and motes where the ray crosses z = 0.
    if (abs(rd.z) > 1e-4) {
        float tq = -ro.z / rd.z;
        if (tq > 0.0 && tq < tEnd) {
            vec2 qp = (ro + rd * tq).xy;
            float sheet = 1.0 / max(abs(rd.z), 0.3);
            vec2 sw = vec2(glassNoise(qp * 2.0 + vec2(t * 0.07, 0.0)), glassNoise(qp * 2.3 + vec2(0.0, -t * 0.09) + 5.0));
            float smoke = 0.5 + 0.5 * glassNoise(qp * 3.5 + 1.6 * sw + vec2(-t * 0.12, t * 0.05));
            float dust = 0.25 + 1.5 * dustK * smoke * smoke;
            float out2 = step(0.0, insideTri(qp));
            float s;
            float along;
            float fan = fanAt(qp, s, along) * out2;
            float pulse = pulseK * exp(-(along - pulseAt) * (along - pulseAt) * 8.0) * 2.0;
            col += rainbow(s) * fan * (0.45 * haze * dust * fanGain + pulse) * sheet * 0.8 / (1.0 + 0.5 * max(along, 0.0));
            col += mix(vec3(1.0), pal(gHue + 0.3), 0.3) * ghostAt(qp) * out2 * (0.4 + 0.6 * haze * dust) * sheet * 0.8;
            // Dust motes in the light, sparkling on the hat.
            vec2 mg = qp * 30.0 + vec2(t * 0.3, sin(t * 0.2));
            vec2 mid = floor(mg);
            float mh = glassHash(mid);
            float bd = segDist(qp, gA, gPin);
            float lightHere = clamp(fan * 4.0 + exp(-bd * 12.0), 0.0, 1.0);
            if (mh > 0.9) {
                float md = length(fract(mg) - 0.5 - (glassHash2(mid) - 0.5) * 0.5);
                col += vec3(1.0) * exp(-md * md * 120.0) * lightHere * (0.2 + 1.5 * ear.hat);
            }
        }
    }

    // The soft haze round the shafts: a fixed-step march to the first solid hit.
    int steps = int(clamp(uSteps * 0.3, 8.0, float(PR_HAZE_MAX)));
    float tMax = min(tEnd, PR_CAM_R * 2.0);
    float dt = tMax / float(steps);
    float jit = glassHash(gl_FragCoord.xy);
    vec3 glow = vec3(0.0);
    for (int i = 0; i < PR_HAZE_MAX; i++) {
        if (i >= steps) break;
        vec3 p = ro + rd * (dt * (float(i) + jit));
        float bd = segDist(p.xy, gA, gPin);
        float b3 = sqrt(bd * bd + p.z * p.z);
        float wisp = 0.6 + 0.4 * sin(p.x * 2.1 + 1.5 * sin(p.z * 1.7 + t * 0.1) + t * 0.07) * sin(p.y * 2.7 - t * 0.05 + sin(p.x * 1.3));
        vec3 l = vec3(1.0, 0.97, 0.92) * exp(-b3 * 7.0);
        float s;
        float along;
        float f = fanAt(p.xy, s, along) * step(0.0, insideTri(p.xy));
        l += rainbow(s) * f * exp(-abs(p.z) * 6.0) / (1.0 + 0.7 * max(along, 0.0));
        glow += l * wisp;
    }
    col += glow * dt * haze * 0.22 * (1.0 + 0.9 * k2);

    // The entry and exit spots flare where the beam meets the glass.
    vec3 pin3 = vec3(gPin, 0.0) - ro;
    vec3 pe3 = vec3(gPeG, 0.0) - ro;
    float dPin = length(pin3 - rd * max(dot(pin3, rd), 0.0));
    float dPe = length(pe3 - rd * max(dot(pe3, rd), 0.0));
    col += vec3(1.0, 0.97, 0.92) * exp(-dPin * 30.0) * (0.5 + bPulse * 0.2);
    col += rainbow(0.5) * exp(-dPe * 26.0) * 0.35;

    // Haze fog toward the room's dark.
    col = mix(col, pal(gHue + 0.6) * 0.02, 1.0 - exp(-min(tEnd, 30.0) * 0.015 * haze));

    fragColor = vec4(grade(glassTone(col)), 1.0);
}
