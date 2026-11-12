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
//#include lib_sdf3
//#include lib_lucid

// Lucid Mirror: a flight down the inside of a three-mirror kaleidoscope tube.
// The 2D style folded the plane into reflected triangles; here the tube is
// real. Three mirrors stand along the flight axis at the sides of an
// equilateral triangle, and the space across the tube (x, y) is folded by the
// reflection group those three mirrors make: every point is sent to its one
// image inside the triangle prism, so whatever stands in the prism is seen
// reflected without end in every direction across the axis, the way a real
// kaleidoscope shows it, and with true depth along it.
//
// What stands in the prism is the 2D style's object in the tube, both halves
// of it: soft blobs, one to each 2-unit slice of the flight, placed by hash
// (where one leans on a mirror it merges with its own reflection into one
// lens, which is how a bead looks lying on the glass), and two woven threads
// that wind round each other along the prism's centre, so their reflections
// across the three mirrors become an endless woven lattice. The blobs wear the
// family's value-noise nest, three levels deep, walked like a spectrum and
// washed toward white, light through a prism in pastel; the threads glow in
// the complement. The mirror seams are thin dark lines wherever a surface
// meets a mirror plane, so the glass of the tube stays legible, and the far
// end of the tube is a pale glow the fog fades into.
//
// ---- camera and the wrap ----------------------------------------------------
//
// The shared rig flies straight down the tube (camFly). Everything that
// depends on z repeats every 64 world units, which divides the rig's 8192
// travel wrap: the blob slices are 2 long and read their hash by index mod 32,
// the threads wind with period 8, and the nest's colour is read round a circle
// in its plane, one loop per 64 units. The eye is rebased by mod(z, 64) before
// the march, so z stays precise and neither the rebase nor the wrap can show.
// x and y are folded, not wrapped, so they need no period at all.
//
// ---- audio ------------------------------------------------------------------
//
// No onset turns the barrel any more (owner 2026-11-12, no style spins to the
// music): the world rolls about the tube's axis on time alone. The blobs lurch sideways with
// lucidBump of the onset (geometry, never brightness). sndShaped(SND_HIGH)
// sharpens the woven threads: they thin a little and their glow tightens and
// brightens. Overall brightness follows SND_ENERGY_TREND gently.
//
// ---- macros and knobs -------------------------------------------------------
//
// Speed: uTime and uFlowTime (the nest's drift and the threads' winding).
// Complexity: uMorph shrinks the mirror triangle (more reflections) and, past
// 0.5, adds a nest level and an octave (lucidDetail). Chaos: uTurbulence/uWarp
// raise warp strength (lucidChaos) and jitter the nest (lucidJitter); uTwist,
// uZoom and uRotation reach the view through lucidView. Color: the spectrum is
// pal(). Energy: uIntensity scales the onset lurch. The user's kaleidoscope
// is skipped: the tube is its own. Knobs: k0 PULSE lurch, k1 TEXTURE marbling
// scale, k2 FLOW thread winding width, k3 TONE hue, k4 BUILD thread glow,
// k5 SPACE mirror size. All neutral at 0.5.
//
// ---- why the distance estimate is safe --------------------------------------
//
// The fold is the orbit map of the mirror group: translate into the hexagon
// of six triangles round a vertex (hexagons of circumradius `side` tile by the
// group's own translations), then fold that hexagon's three mirror lines into
// one 60-degree wedge. The fundamental triangle is a strict fundamental domain,
// so every point lands on the one image of itself in it, and the map is
// continuous and 1-Lipschitz (the distance between two folded points never
// exceeds the distance between them). The roll about the axis is an isometry.
// A 1-Lipschitz field composed with a 1-Lipschitz map stays 1-Lipschitz, so
// the march never steps through the surface it draws. Inside: each slice's
// blob stays within its own slice in z (centre in [0.5, 1.5] of the slice,
// radius at most 0.5), so measuring the own and the nearer neighbour slice and
// clamping by the distance to the farther slices is a lower bound; the
// polynomial smooth union only lowers it. A thread is a disc in (x, y) whose
// centre moves with z at slope A * w, so its distance is divided by
// sqrt(1 + (A w)^2). A sphere round the eye is carved out of everything
// through max, so the eye is never inside a blob or a thread.

#define LM_MAX_STEPS 96
#define LM_FAR 36.0
#define LM_ZWRAP 64.0
/** 2 pi / 64: the colour loop's z rate, one turn per wrap. */
#define LM_U64 0.09817477
/** 2 pi / 8: the threads' winding rate along z. */
#define LM_U8 0.78539816
#define LM_BUBBLE 0.45
#define LM_SQRT3 1.7320508

float gSide;
float gSeam;
float gMat;
float gThA;
float gThR;
float gThLip;
float gWind;
vec2 gLurch;
vec2 gAxis;
mat2 gRoll;
vec3 gEye;
/** The folded point of the last map() call, for colour and seams. */
vec3 gF;

/**
 * The three-mirror fold across the tube. Returns the image of `p` in the
 * fundamental triangle with vertices (0, 0), (side, 0) and
 * side * (0.5, 0.866); `seam` gets its distance to the nearest mirror.
 */
vec2 triFold(vec2 p, float side, out float seam) {
    float spacing = side * LM_SQRT3;
    // lucidHex's cells have their vertices on y; swap the axes so ours sit on x.
    vec4 hx = lucidHex(p.yx / spacing);
    vec2 l = hx.yx * spacing;
    float seg = LUCID_TAU / 3.0;
    float a = atan(l.y, l.x);
    // A turn by 120 degrees, then the mirror at 60: 0..60 stays, 60..120 reflects.
    a = 0.5 * seg - abs(mod(a, seg) - 0.5 * seg);
    l = vec2(cos(a), sin(a)) * length(l);
    float s0 = l.y;
    float s1 = 0.8660254 * l.x - 0.5 * l.y;
    float s2 = 0.5 * spacing - dot(l, vec2(0.8660254, 0.5));
    seam = max(min(s0, min(s1, s2)), 0.0);
    return l;
}

/** The blob of slice `cell` (2 units long, index read mod 32), measured from the folded point. */
float blob(vec2 f, float z, float cell) {
    uint u = uhash(uint(int(mod(cell, 32.0))) + 0x51du);
    float h0 = uhashUnit(u);
    float h1 = uhashUnit(uhash(u));
    float h2 = uhashUnit(uhash(u ^ 0x2c1bu));
    float h3 = uhashUnit(uhash(u + 0x9e3u));
    // A uniform point in the triangle, pulled a third of the way to its centre.
    vec2 b = vec2(h0, h1);
    if (b.x + b.y > 1.0) b = 1.0 - b;
    vec2 c = gSide * (vec2(1.0, 0.0) * b.x + vec2(0.5, 0.8660254) * b.y);
    c = mix(c, gSide * vec2(0.5, 0.28867513), 0.33) + gLurch * (h2 - 0.5) * 2.0;
    float cz = cell * 2.0 + 0.5 + h3;
    float r = gSide * (0.13 + 0.09 * h2) * (1.0 + 0.06 * sin(uTime * 0.6 + h0 * 40.0));
    return length(vec3(f - c, z - cz)) - min(r, 0.5);
}

float map(vec3 p) {
    vec3 w = p;
    w.xy = gAxis + gRoll * (p.xy - gAxis);
    float seam;
    vec2 f = triFold(w.xy, gSide, seam);
    gSeam = seam;
    gF = vec3(f, w.z);

    // Blobs: the own slice and the nearer neighbour, clamped by the farther ones.
    float zc = floor(w.z * 0.5);
    float fz = w.z - zc * 2.0;
    float nb = fz < 1.0 ? zc - 1.0 : zc + 1.0;
    float db = smin(blob(f, w.z, zc), blob(f, w.z, nb), 0.2 * gSide);
    db = min(db, max(fz, 2.0 - fz));

    // Two threads winding round each other along the prism's centre.
    vec2 cen = gSide * vec2(0.5, 0.28867513);
    float ph = w.z * LM_U8 + gWind;
    vec2 o = gThA * vec2(cos(ph), sin(ph));
    float dt = min(length(f - cen - o), length(f - cen + o)) - gThR;
    dt /= gThLip;

    gMat = dt < db ? 1.0 : 0.0;
    float d = min(db, dt);
    return max(d, LM_BUBBLE - length(p - gEye));
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

void main() {
    vec2 uv = lucidView(false);
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)
    float flow = uFlowTime;

    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTex = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kFlow = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    float kSpace = clamp(uKnob[5], 0.0, 1.0) - 0.5;

    float treb = clamp(sndShaped(SND_HIGH), 0.0, 1.5);
    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float onset = clamp(SND_ONSET_ENV, 0.0, 1.0);

    int depth = lucidDetail(3, 2);
    int oct = lucidDetail(3, 2);

    // The mirror triangle: smaller with Complexity, sized by SPACE.
    gSide = (1.5 - 0.6 * clamp(uMorph, 0.0, 1.0)) * (1.0 + 0.5 * kSpace);

    // The barrel: a slow roll on time alone.
    float barrel = t * 0.015;
    gRoll = lucidRot(barrel);

    float lurch = lucidBump(onset) * 0.35 * uIntensity * (1.0 + kPulse);
    gLurch = gSide * 0.3 * vec2(lurch, -0.6 * lurch);

    float trebK = clamp(treb, 0.0, 1.0);
    gThA = gSide * 0.13 * (1.0 + 0.6 * kFlow);
    gThR = gSide * 0.032 * (1.0 - 0.3 * trebK);
    gThLip = sqrt(1.0 + gThA * gThA * LM_U8 * LM_U8);
    gWind = flow * 0.4 + t * 0.1;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term repeats every 64 units (see the header): exact and invisible.
    ro.z = mod(ro.z, LM_ZWRAP);
    gEye = ro;
    gAxis = ro.xy;

    float tm = 0.03;
    float hitT = -1.0;
    float used = 0.0;
    for (int i = 0; i < LM_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (tm > LM_FAR) break;
        used = float(i);
        float d = map(ro + rd * tm);
        float eps = 0.0008 * tm + 0.0005;
        if (d < eps) {
            hitT = tm;
            break;
        }
        tm += max(d * 0.9, eps);
    }

    float hue = 0.4 * kTone + 0.03 * sin(uTime * 0.019);
    float fogK = 0.075;
    float ahead = pow(max(rd.z, 0.0), 6.0);
    // The far end of the tube: pale light through the glass.
    vec3 fogCol = mix(pal(hue + 0.15), vec3(1.0), 0.45) * (0.05 + 0.22 * ahead)
                + vec3(0.006, 0.005, 0.009);
    vec3 col = fogCol;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 nrm = normalAt(p, max(0.0012, 0.001 * hitT));
        // Restore the hit's globals after the normal taps.
        map(p);
        vec3 f = gF;
        float seam = gSeam;
        float mat = gMat;

        float ndv = clamp(dot(nrm, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.1, 0.3, 1.0);
        vec3 L = normalize(vec3(0.3, 0.45, 0.85));
        float dif = 0.55 * max(dot(nrm, L), 0.0) + 0.45 * ndv;
        float spec = pow(max(dot(reflect(rd, nrm), L), 0.0), 36.0);

        // The nest, read once at the hit: the folded point across the tube
        // and a circle in z (one loop per wrap), so the colour is mirrored
        // like the geometry and closes over the rebase.
        float th = f.z * LM_U64;
        vec2 q = (f.xy / gSide) * 2.4 * (1.0 + 0.8 * kTex)
               + 1.6 * vec2(cos(th), sin(th))
               + vec2(t * 0.07 + flow * 0.1, -t * 0.05)
               + vec2(lurch, -0.6 * lurch);
        q += lucidJitter(q, t);
        vec4 n = lucidNest(q, depth, 1.3 * lucidChaos(), t * 0.22, LUCID_VALUE, oct);
        float s = 0.5 + 0.5 * n.z;

        // Fake dispersion: each channel reads the palette a step further on.
        float kp = hue + s + 0.4 * fres;
        vec3 prism = vec3(pal(kp).r, pal(kp + 0.07).g, pal(kp + 0.14).b);

        vec3 surf;
        if (mat < 0.5) {
            vec3 spectrum = pal(s * 1.4 + 0.25 * n.w + hue);
            vec3 base = mix(spectrum, vec3(1.0), 0.32) * (0.35 + 0.65 * smoothstep(0.1, 0.9, s));
            surf = base * (0.18 + 0.82 * dif) * ao + prism * fres * 0.35 + vec3(spec) * 0.35 * ao;
            surf = lucidChannelTurn(surf, 0.6 * n.w);
            surf = lucidCubic(surf, 0.5);
        } else {
            // The threads: lit from inside in the complement, the woven lattice
            // of the 2D style. The treble tightens and brightens the glow.
            float core = pow(ndv, 1.5 + 2.5 * trebK);
            vec3 tc = mix(pal(s + 0.5 + hue), vec3(1.0), 0.25);
            surf = tc * (0.12 + 0.3 * dif) * ao
                 + tc * core * (0.45 + 0.5 * trebK) * (1.0 + 0.9 * kBuild)
                 + prism * fres * 0.25;
        }

        // The glass: thin dark seams where a surface meets a mirror.
        float px = hitT * 2.0 / (max(uCamFocal, 0.1) * uResolution.y);
        float sw = 0.012 * gSide + 1.5 * px;
        surf *= mix(0.25, 1.0, smoothstep(0.0, sw, seam));

        col = mix(surf, fogCol, 1.0 - exp(-hitT * fogK));
    }

    col *= 0.85 + 0.12 * swell;
    col *= 0.75 + 0.25 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
