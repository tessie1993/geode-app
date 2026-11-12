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
//#include lib_touch
//#include lib_shell

// Merkaba Grid: a flight down a cathedral of sacred geometry. The hall is a
// barrel vault over a flat floor, and both are skinned in the same triangulated
// web (three line families at 60 degrees, the tiling every geodesic is built
// from). Wire-frame geodesic spheres sit in a row on the floor down each side,
// star tetrahedra (two interlocked tetrahedra, the merkaba) hang and turn in
// the air above them with a glint at every one of their eight points, and at
// the far end of the nave a column of light rises out of the vanishing point
// and bursts at the top, so the whole thing reads as a hall you travel down.
//
// The camera is the shared rig (camFly). The hall runs straight down +z. The
// spheres and merkabas sit in 8-unit cells hashed modulo 128 cells, the web's
// z rates are n / 2 and n / 4 lines per unit (whole numbers of lines in 1024),
// and every other z term has a period dividing 1024, so the eye is rebased by
// 1024 exactly and the rig's 8192 wrap is invisible.
//
// ---- audio ------------------------------------------------------------------
//
// The contract of the flat original, carried into depth:
// Bass (SND_BASS_PRESENCE)   the spheres breathe (radius) and the web glows.
// Treble (sndShaped(SND_HIGH)) the lit web nodes and the merkaba points glint.
// Level (SND_LEVEL)          the column of light widens and brightens.
// uSpawnSeed                 re-picks which web nodes are lit, grown in over a
//                            second (spawnGrow), never at once.
// uFormPhase                 steps the web between three densities and the
//                            spheres' ring count, and holds each plateau.
// uMoveDir / uFlowPhase      drift the motes in the air. The burst's spokes
//                            turn on uTime alone: nothing turns to the music.
// Knobs (uKnob, 0.5 = neutral): PULSE the bass breath, TEXTURE the web's line
// weight, FLOW how far the merkabas bob (it used to turn them, which turned
// them to the music), TONE how high they float (a high melody
// lifts the stars up toward the vault, a low one lowers them to hang over the
// floor's web; and a small hue offset), BUILD the column, SPACE the fog.
// Morph eases each star toward an octahedron, Warp sways the web lines,
// Turbulence shakes the motes and bobs the merkabas, Intensity scales the light.
// No term in this file can reach its peak inside one frame.
//
// ---- why the estimate is safe ----------------------------------------------
//
// Floor (plane) and vault (inside of a cylinder) are exact. A sphere is exact.
// The star is the union of two tetrahedra, each the max of its four face
// plane distances, which is a lower bound outside a convex solid and exact
// inside; the Morph blend is a convex mix of that and the exact octahedron, so
// it stays 1-Lipschitz. Rotations preserve distance. Each object is measured
// only in its own cell: every object stays a fixed margin inside its cell and
// its side of the hall (worked out at the defines), so the distance is clamped
// to (distance to the cell edge + margin) and (distance to the mirror plane +
// gap), which no neighbour can be closer than. A sphere around the eye is
// carved out of everything, so the eye never sits inside an object.

#define MG_MAX_STEPS 96
#define MG_FAR 48.0
#define MG_TAU 6.2831853
#define MG_FLOOR -3.0
/** Radius of the barrel vault, centred on the floor's mid line. */
#define MG_VAULT 15.0
/** Cell length along z for spheres and merkabas: 128 cells per 1024. */
#define MG_CELL 8.0
#define MG_NCELL 128.0
#define MG_ZWRAP 1024.0
/** Sphere lane: x 6.7..7.7, z jitter +-0.8, radius <= 2.3 * 1.12 = 2.58. */
#define MG_SPH_X 7.2
/** 4 - 0.8 - 2.58 = 0.62: the nearest a neighbour cell's sphere gets past this cell's edge. */
#define MG_SPH_MARGIN 0.6
/** 6.7 - 2.58 = 4.12: no sphere reaches nearer the mirror plane. */
#define MG_SPH_GAP 4.1
/** Merkaba lane: x 3.8..4.6, z jitter +-1.2, extent <= sqrt(3) * 0.81 = 1.40. */
#define MG_MK_X 4.2
/** 4 - 1.2 - 1.40 = 1.40. */
#define MG_MK_MARGIN 1.35
/** 3.8 - 1.40 = 2.40. */
#define MG_MK_GAP 2.35
#define MG_BUBBLE 0.8

float gBass;
/** FLOW's turn of every merkaba, radians (an offset, never a rate). */
float gMorph;
float gBob;
/** k3 TONE: how far the merkabas are lifted (0 at rest). */
float gLift;
vec3 gEye;
/** 0 floor, 1 vault, 2 sphere, 3 merkaba: which surface map() found nearest. */
float gMat;
/** Object-local point (sphere: offset from centre; merkaba: rotated frame). */
vec3 gLocal;
float gSize;
float gSeed;
/** Distance to the nearest merkaba point in this cell, for the march's glow. */
float gVert;

/** Regular tetrahedron with vertices on four corners of the cube +-s. */
float tetra(vec3 q, float s) {
    float m = max(max(-q.x - q.y - q.z, q.x + q.y - q.z), max(-q.x + q.y + q.z, q.x - q.y + q.z));
    return (m - s) * 0.57735027;
}

float mgField(vec3 p) {
    float side = p.x < 0.0 ? 1.0 : 0.0;
    vec3 a = vec3(abs(p.x), p.y, p.z);

    float dFloor = p.y - MG_FLOOR;
    float dVault = MG_VAULT - length(vec2(p.x, p.y - MG_FLOOR));
    float d = min(dFloor, dVault);
    gMat = dFloor < dVault ? 0.0 : 1.0;
    gLocal = p;

    // ---- geodesic spheres, one per cell per side ----
    float zc = floor(p.z / MG_CELL);
    float ez = 0.5 * MG_CELL - abs(p.z - (zc + 0.5) * MG_CELL);
    uint u = uhash3(uvec3(uint(mod(zc, MG_NCELL)), uint(side), 7u));
    float h1 = uhashUnit(u);
    float h2 = uhashUnit(uhash(u));
    float h3 = uhashUnit(uhash(u ^ 0x9e37u));
    float r = (1.5 + 0.8 * h1) * (1.0 + 0.08 * gBass);
    vec3 c = vec3(MG_SPH_X + (h2 - 0.5), MG_FLOOR + r * 0.82, (zc + 0.5) * MG_CELL + (h3 - 0.5) * 1.6);
    vec3 sq = a - c;
    float dS = length(sq) - r;
    dS = min(dS, min(ez + MG_SPH_MARGIN, a.x + MG_SPH_GAP));
    if (dS < d) {
        d = dS;
        gMat = 2.0;
        gLocal = sq;
        gSize = r;
        gSeed = h1;
    }

    // ---- star tetrahedra, cells offset by half so they sit between spheres ----
    float zm = floor(p.z / MG_CELL + 0.5);
    float em = 0.5 * MG_CELL - abs(p.z - zm * MG_CELL);
    uint v = uhash3(uvec3(uint(mod(zm, MG_NCELL)), uint(side), 13u));
    float k1 = uhashUnit(v);
    float k2 = uhashUnit(uhash(v));
    float k3 = uhashUnit(uhash(v ^ 0x51edu));
    float s = 0.55 + 0.2 * k1;
    vec3 mc = vec3(MG_MK_X + (k2 - 0.5) * 0.8, 0.6 + 2.0 * k3 + gBob * sin(uTime * 0.31 + k1 * 40.0) + gLift,
                   zm * MG_CELL + (k1 - 0.5) * 2.4);
    vec3 q = a - mc;
    // A fixed rate times uTime, plus FLOW as a bounded offset: a rate that moved
    // with the knob would jump the angle by (rate change) * uTime.
    float spin = uTime * 0.6 * (0.35 + 0.4 * k2) + k3 * MG_TAU;
    q.xz = rot2(spin) * q.xz;
    q.yz = rot2(spin * 0.61 + k1 * 3.0) * q.yz;
    float dM = min(tetra(q, s), tetra(-q, s));
    dM = mix(dM, sdOctahedron(q, s * 1.7), gMorph);
    gVert = length(q - sign(q) * s);
    dM = min(dM, min(em + MG_MK_MARGIN, a.x + MG_MK_GAP));
    if (dM < d) {
        d = dM;
        gMat = 3.0;
        gLocal = q;
        gSize = s;
        gSeed = k2;
    }

    d = max(d, MG_BUBBLE - length(p - gEye));
    return d;
}

/**
 * The field every march, normal and occlusion tap reads: mgField, the style's
 * own, through lib_shell's hook (the identity until the shell drives it).
 */
float map(vec3 p) {
    return SHELL_FIELD3(p, mgField);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** One line of a wire frame, `dist` and `width` in the same world units, `px` the pixel footprint. */
float wire(float dist, float width, float px) {
    float w = width + px;
    return smoothstep(w, w * 0.25, abs(dist)) * min(1.0, width / w * 1.5);
}

/**
 * The triangulated web on a surface with coordinates (x, z), row spacing h.
 * All three families are spaced h apart; their z rates 1/h and 1/(2h) are
 * whole lines per 1024 for h = 2/n; `rows` is 1024 / h, passed whole so the
 * row id wraps on an exact integer. Returns the line coverage; `nodeDist` is the
 * distance to the nearest vertex, `id` that vertex's wrap-safe id.
 */
float web(vec2 xz, float h, float rows, float width, float px, out float nodeDist, out vec2 id) {
    float a = h * 1.1547005;
    float f1 = xz.y / h;
    float f2 = xz.x / a + xz.y / (2.0 * h);
    float f3 = xz.x / a - xz.y / (2.0 * h);
    vec3 f = 0.5 - abs(fract(vec3(f1, f2, f3)) - 0.5);
    float lines = wire(min(f.x, min(f.y, f.z)) * h, width, px);
    float n = floor(f1 + 0.5);
    float m = floor(xz.x / a - n * 0.5 + 0.5);
    nodeDist = length(xz - vec2(a * (m + n * 0.5), n * h));
    // m + floor(n / 2) and n mod (1024 / h) are unchanged by the rebase.
    id = vec2(m + floor(n * 0.5), mod(n, rows));
    return lines;
}

/** Rings about one icosahedral axis: distance (in cosine units) to the nearest. */
float ringDist(vec3 n, vec3 axis, float k) {
    float c = dot(n, axis) * k;
    return abs(fract(c + 0.5) - 0.5) / k;
}

/** Geodesic wires on a sphere: rings about the six five-fold axes of the icosahedron. */
float geodesic(vec3 n, float k, float width, float px) {
    float d = ringDist(n, vec3(0.0, 0.5257311, 0.8506508), k);
    d = min(d, ringDist(n, vec3(0.0, -0.5257311, 0.8506508), k));
    d = min(d, ringDist(n, vec3(0.5257311, 0.8506508, 0.0), k));
    d = min(d, ringDist(n, vec3(-0.5257311, 0.8506508, 0.0), k));
    d = min(d, ringDist(n, vec3(0.8506508, 0.0, 0.5257311), k));
    d = min(d, ringDist(n, vec3(-0.8506508, 0.0, 0.5257311), k));
    return wire(d, width, px);
}

/** The second largest of four. */
float second4(vec4 v) {
    return max(min(max(v.x, v.y), max(v.z, v.w)), max(min(v.x, v.y), min(v.z, v.w)));
}

/** The far end of the hall: dark nave, the rising column of light and its burst. */
vec3 skyColour(vec3 rd, float hue, float swell, float build) {
    vec3 col = pal(hue + 0.6) * 0.025 + vec3(0.004, 0.003, 0.008);
    float ahead = max(rd.z, 0.0);
    col += pal(hue + 0.08) * 0.10 * pow(ahead, 12.0);
    // The column: a vertical streak over the vanishing point, rising toward the top.
    vec2 sp = rd.xy / max(rd.z, 0.05);
    float shaft = exp(-sp.x * sp.x * (160.0 - 60.0 * swell)) * smoothstep(-0.25, 0.4, sp.y)
                * smoothstep(0.0, 0.5, ahead);
    col += pal(hue + 0.02) * shaft * (0.30 + 0.45 * gBass + 0.3 * swell) * build;
    // The burst where it leaves the vault: a soft disc with turning spokes.
    vec2 b = sp - vec2(0.0, 0.55);
    float br = length(b);
    float spokes = 0.55 + 0.45 * cos(atan(b.y, b.x) * 12.0 + uTime * 0.5);
    col += mix(vec3(1.0), pal(hue), 0.4) * exp(-br * 6.0) * spokes * 0.35 * build
         * smoothstep(0.2, 0.7, ahead);
    col += vec3(1.0) * exp(-br * br * 260.0) * 1.2 * build * smoothstep(0.3, 0.8, ahead);
    return col;
}

void main() {
    vec2 uv = view();

    // ---- the shell: this style's shape as normalized inputs ---------------
    //
    // Every tunable of the shape is a 0..1 input here, set from the params and
    // the six knobs and 0.5 at their defaults, and mapped to its range in this
    // one place: lib_shell's shellMap(n, lo, rest, hi) puts 0.5 exactly on the
    // value the style was tuned at, so the default look is unchanged, and the
    // shell's hooks can later fractal out, move and manipulate any of them.
    float nBreath = clamp(uKnob[0], 0.0, 1.0);
    float nWeb = clamp(uKnob[1], 0.0, 1.0);
    float nBob = clamp(uKnob[2], 0.0, 1.0);
    float nFloat = clamp(uKnob[3], 0.0, 1.0);
    float nColumn = clamp(uKnob[4], 0.0, 1.0);
    float nFog = clamp(uKnob[5], 0.0, 1.0);
    float nStar = 0.5 + 0.5 * clamp(uMorph, 0.0, 1.0);
    // The bass breath's depth, the web's and the stars' line weights, how far
    // the merkabas bob and how high they float, the column, the fog.
    float breathK = shellMap(nBreath, 0.6, 1.0, 1.4);
    float webW = shellMap(nWeb, 0.02, 0.035, 0.05);
    float edgeW = shellMap(nWeb, 0.02, 0.03, 0.04);
    float bobK = shellMap(nBob, 0.4, 1.0, 1.6);
    float build = shellMap(nColumn, 0.4, 1.0, 1.6);
    float hue = shellMap(nFloat, 0.455, 0.58, 0.705);
    float fogK = shellMap(nFog, 0.063, 0.045, 0.027);

    float bass = clamp(SND_BASS_PRESENCE * breathK, 0.0, 1.5);
    float treb = clamp(sndShaped(SND_HIGH), 0.0, 1.5);
    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    float intensity = clamp(uIntensity, 0.0, 2.0);
    float warp = clamp(uWarp, 0.0, 1.0);
    float turb = clamp(uTurbulence, 0.0, 1.0);
    // A spike steps the density between three plateaus and holds it there.
    float plateau = floor(clamp(uFormPhase, 0.0, 0.999) * 3.0);
    float h = 2.0 / (1.0 + plateau);
    float rows = 512.0 * (1.0 + plateau);

    gBass = bass;
    gMorph = shellMap(nStar, 0.0, 0.0, 0.6);
    gBob = (0.15 + 0.35 * turb) * bobK;
    gLift = shellMap(nFloat, -1.1, 0.0, 1.1);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 1024, so this rebase is exact
    // and invisible, and z keeps its precision far down the 8192 travel.
    ro.z = mod(ro.z, MG_ZWRAP);
    gEye = ro;


    float t = 0.05;
    float hitT = -1.0;
    float glow = 0.0;
    for (int i = 0; i < MG_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * t;
        float d = map(p);
        // The merkaba points shine into the air around them, not only on the surface.
        glow += exp(-t * fogK) / (1.0 + 90.0 * gVert * gVert);
        if (d < 0.0012 + 0.0008 * t) {
            hitT = t;
            break;
        }
        t += d * 0.9;
        if (t > MG_FAR) break;
    }

    vec3 sky = skyColour(rd, hue, swell, build);
    vec3 col = sky;
    float sceneT = hitT > 0.0 ? hitT : MG_FAR;
    float px = sceneT * 2.0 / (max(uCamFocal, 0.1) * uResolution.y);

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.001 * hitT));
        float occ = 0.0;
        float sc = 1.0;
        for (int k = 0; k < 3; k++) {
            float hk = 0.12 + 0.18 * float(k);
            occ += (hk - map(p + n * hk)) * sc;
            sc *= 0.6;
        }
        float ao = clamp(1.0 - 1.4 * occ, 0.25, 1.0);
        // Restore the hit's globals after the normal and AO taps.
        map(p);

        vec3 L = normalize(vec3(0.15, 0.9, 0.4));
        float dif = max(dot(n, L), 0.0);
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = pow(1.0 - ndv, 3.0);
        float spec = pow(max(dot(reflect(rd, n), L), 0.0), 40.0);
        float width = webW * (1.0 + 0.25 * bass);
        float lit = spawnGrow(1.2);
        float seedOff = floor(uSpawnSeed * 97.0);
        // The column at the end of the hall lights the near side of everything.
        float back = max(dot(n, vec3(0.0, 0.2, -1.0)), 0.0) * 0.12 * build;

        if (gMat < 1.5) {
            // Floor or vault: the web, with a sparse set of lit nodes.
            vec2 xz;
            if (gMat < 0.5) {
                xz = p.xz;
            } else {
                xz = vec2(atan(p.x, p.y - MG_FLOOR) * MG_VAULT, p.z);
            }
            xz.x += warp * 0.6 * sin(xz.y * (MG_TAU / 16.0) + uTime * 0.3);
            float nodeDist;
            vec2 id;
            float lines = web(xz, h, rows, width, px, nodeDist, id);
            float on = step(0.82, hashCell(ivec3(int(id.x), int(id.y), int(seedOff))));
            float node = (1.0 - smoothstep(0.0, 0.10 * h + px, nodeDist)) * on * lit;
            float skin = (0.015 + 0.04 * dif) * ao;
            vec3 lineCol = pal(hue + (gMat < 0.5 ? 0.0 : -0.06)) * (0.22 + 0.30 * bass);
            col = pal(hue + 0.5) * skin + lineCol * lines * (0.5 + 0.5 * ao)
                + mix(pal(hue - 0.14), pal(hue + 0.28), hashCell(ivec3(int(id.x), int(id.y), 3)))
                * node * (0.4 + 1.2 * treb);
            // Lit nodes also light the web lines around them.
            col += pal(hue - 0.1) * lines * on * lit * (0.15 + 0.4 * treb)
                 * (1.0 - smoothstep(0.0, 0.9 * h, nodeDist));
            col += pal(hue) * back * 0.3;
        } else if (gMat < 2.5) {
            // A wire-frame geodesic sphere over a soft glass skin.
            vec3 sn = normalize(gLocal);
            sn.xz = rot2(uTime * 0.05 * (gSeed - 0.5)) * sn.xz;
            float rings = 2.0 + plateau;
            float wires = geodesic(sn, rings, width / gSize, px / gSize);
            vec3 tint = mix(pal(hue - 0.03), pal(hue - 0.18), gSeed);
            col = tint * (0.04 + 0.12 * dif) * ao + tint * fres * 0.25
                + mix(tint, vec3(1.0), 0.25) * wires * (0.45 + 0.35 * bass) * (0.6 + 0.4 * ao)
                + vec3(spec) * 0.25 + tint * back;
        } else {
            // A star tetrahedron: dark glass with bright edges and glinting points.
            vec3 q = gLocal;
            vec4 va = vec4(-q.x - q.y - q.z, q.x + q.y - q.z, -q.x + q.y + q.z, q.x - q.y + q.z);
            vec4 vb = -va;
            float ma = max(max(va.x, va.y), max(va.z, va.w));
            float mb = max(max(vb.x, vb.y), max(vb.z, vb.w));
            // The surface belongs to the tetrahedron whose largest plane value is smaller.
            float gap = ma < mb ? ma - second4(va) : mb - second4(vb);
            float edge = wire(gap * 0.57735027, edgeW, px);
            vec3 tint = mix(pal(hue - 0.28), pal(hue + 0.32), gSeed);
            float tip = 1.0 - smoothstep(0.0, gSize * 0.35, gVert);
            col = tint * (0.03 + 0.10 * dif) * ao + tint * fres * 0.4
                + mix(tint, vec3(1.0), 0.3) * edge * 0.9
                + mix(vec3(1.0), tint, 0.35) * tip * (0.3 + 0.8 * treb)
                + vec3(spec) * 0.5;
        }

        col = mix(col, sky, 1.0 - exp(-hitT * fogK));
    }

    // The points' halo gathered along the ray.
    col += mix(vec3(1.0), pal(hue + 0.3), 0.5) * min(glow * 0.02, 0.6) * (0.4 + 0.6 * treb);

    // Motes: points on world z slices every 2 units ahead of the eye, one
    // hashed candidate per 1.4-unit cell. Slices sit at fixed world z (index
    // wrapped by 1024), so they hold still under the rebase and parallax is true.
    if (abs(rd.z) > 0.08) {
        float dir = rd.z > 0.0 ? 1.0 : -1.0;
        float base = rd.z > 0.0 ? floor(ro.z * 0.5) + 1.0 : ceil(ro.z * 0.5) - 1.0;
        vec3 moteCol = mix(pal(hue - 0.1), vec3(1.0), 0.35);
        vec2 drift = flowOffset(0.30);
        for (int i = 0; i < 6; i++) {
            float zi = (base + dir * float(i)) * 2.0;
            float ti = (zi - ro.z) / rd.z;
            if (ti < 0.5 || ti > sceneT) continue;
            vec2 xy = ro.xy + rd.xy * ti - drift;
            vec2 cell = floor(xy / 1.4);
            int slice = int(mod(zi, MG_ZWRAP) * 0.5 + 0.5);
            float hm = hashCell(ivec3(int(cell.x), int(cell.y), slice));
            if (hm < 0.6) continue;
            float hm2 = hashCell(ivec3(int(cell.x), int(cell.y), slice + 131));
            float j = 0.12 + 0.3 * turb;
            vec2 mcen = (cell + 0.5 + (vec2(hm, hm2) - 0.5) * 0.6) * 1.4
                      + j * vec2(sin(uTime * 0.4 + hm * 30.0), cos(uTime * 0.33 + hm2 * 30.0));
            float pxi = ti * 2.0 / (max(uCamFocal, 0.1) * uResolution.y);
            float rr = 0.03 + pxi;
            float mm = (1.0 - smoothstep(0.0, rr, length(xy - mcen))) * min(1.0, 0.03 / rr);
            col += moteCol * mm * (0.25 + 0.5 * treb) * exp(-ti * fogK);
        }
    }

    if (!touchIdle()) {
        col += pal(hue + 0.15 * sin(uTime * 0.05)) * min(touchWake(uv), 3.0) * 0.05;
    }
    col *= (0.75 + 0.25 * intensity) * (0.85 + 0.2 * swell);
    // Vignette, so the hall falls into the dark at the corners.
    col *= 0.68 + 0.32 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
