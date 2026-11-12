#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Morph, Warp and Turbulence are spent on the orbs (see map), not on the rays.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_shell

// Orb Lattice: the flat style's spherical kaleidoscope of neon dot lattices,
// stepped inside. The dots are real orbs now: three cubic lattices of glowing
// spheres (periods 4, 2 and 1 world units, the 2D style's three dot scales),
// folded into a kaleidoscope about the flight axis, so every wedge mirrors
// its neighbour and the lattices wheel round the eye like the old fold did
// round the screen centre. Each orb is lit neon glass: bright face-on, an RGB
// fringe on its rim (the old rgbSplit, now three channels reading the rim at
// staggered angles), over the same dark green field, which is the fog. A
// faint halo gathers along each ray where it grazes orbs, the neon bleed.
//
// The camera is the shared rig (camFly) down +z. A tube around the axis is
// carved out of the lattices as the flight channel and a sphere around the
// eye out of everything, so the eye never sits in an orb. Every z term has a
// period dividing 16 (lattices 4, 2, 1; the warp 8; the hue ripple 16), and
// the eye is rebased by mod 16, so the rig's 8192 travel wrap is invisible
// and z keeps its precision.
//
// ---- audio ------------------------------------------------------------------
//
// Kick (SND_KICK_ENV, weighted by k0 PULSE)   the big orbs swell: geometry, not light.
// Treble (sndShaped(SND_HIGH))                 the RGB fringe widens, as in 2D.
// Hat (SND_HAT_ENV)                            the small dots twinkle, each its own phase.
// Level (sndShaped(SND_LEVEL))                 neon bloom: bright colour feeds on itself.
// Bass presence (SND_BASS_PRESENCE)            the halo between the orbs thickens.
// Form phase (uFormPhase)                      the fold count, held until the next spike
//                                              (or the user's Symmetry with Kaleido on).
// Spawn seed / move dir                        the lattice slides to a new offset, grown in.
// SND_INTENSITY                                overall level.
//
// Morph rounds the orbs toward soft cubes, Warp stirs the lattice through an
// ABC flow, Turbulence varies the orb sizes cell by cell. Knobs: k1 TEXTURE
// the small dots' size, k2 FLOW the stir, k3 TONE which lattice leads (a
// high melody swells the mid orbs between the big ones and shrinks the big,
// so the lattice turns airy and fine; a low one the reverse; and a small hue
// offset; every radius keeps the bound below), k4 BUILD the halo, k5 SPACE
// the flight tube's width.
//
// ---- why the estimate is safe ----------------------------------------------
//
// Each lattice cell measures only its own orb, which can be smaller than a
// neighbour's (Turbulence) or a rounded box (Morph, whose corner reaches
// 1.55 * radius). So the cell's distance is clamped to the cell wall plus
// the gap the largest possible neighbour leaves; every radius below keeps
// 1.55 * radius under half the period even on a full kick. The polar fold is
// an isometry inside each wedge; only cells cut by a wedge wall can overstep,
// which the 0.8 step scale absorbs. The warp is divided by its Lipschitz bound
// (1 + 2 * amount * frequency, as fluidWarp3Lipschitz argues for one pass).

#define OL_MAX_STEPS 88
#define OL_FAR 34.0
#define OL_TAU 6.2831853
#define OL_ZWRAP 16.0
#define OL_BUBBLE 0.8
/** Warp frequency: period 8 world units on every axis, which divides 16. */
#define OL_WF 0.7853982
/** Box-corner reach of a fully morphed orb, per unit radius: 0.75 * sqrt(3) + 0.25. */
#define OL_REACH 1.55

float gFolds;
float gSpin;
vec2 gShift;
float gWarpA;
vec3 gDrift;
float gLip;
float gMorph;
float gTurb;
float gRadA;
float gRadC;
float gTube;
vec3 gEye;
/** Set by map(): 0 big, 1 mid, 2 small orb; the hit cell's hash; the folded point. */
/** k3 TONE: the mid lattice's orb radius (0.26 at rest; at most 0.39, so 1.55 r < 1). */
float gRadB;
float gMat;
float gHash;
vec3 gQ;

/**
 * One cubic lattice of orbs, period `c`, radius `rad`. `zCells` is how many
 * cells tile 16 along z, so the per-cell hash repeats exactly at the rebase.
 */
float orbLayer(vec3 q, float c, float rad, float zCells, out float h) {
    vec3 cell = floor(q / c);
    vec3 f = q - (cell + 0.5) * c;
    h = hashCell(ivec3(int(cell.x), int(cell.y), int(mod(cell.z, zCells))));
    float r = rad * (1.0 - 0.45 * gTurb * h);
    float ds = length(f) - r;
    float db = sdBox(f, vec3(r * 0.75)) - r * 0.25;
    // A convex blend of two 1-Lipschitz fields is 1-Lipschitz.
    float d = mix(ds, db, gMorph);
    vec3 af = abs(f);
    float toEdge = 0.5 * c - max(af.x, max(af.y, af.z));
    return min(d, toEdge + 0.5 * c - OL_REACH * rad);
}

float olField(vec3 w) {
    vec3 p = w + gWarpA * abcFlow(w * OL_WF + gDrift);

    // The kaleidoscope: fold the plane round the axis into gFolds mirrored
    // wedges. atan(0, 0) is undefined in GLSL ES; the axis is inside the tube.
    float r = length(p.xy);
    float a = (r > 1e-5 ? atan(p.y, p.x) : 0.0) + gSpin;
    float wdg = OL_TAU / gFolds;
    a = abs(mod(a, wdg) - 0.5 * wdg);
    vec3 q = vec3(r * cos(a), r * sin(a), p.z);
    q.xy += gShift;
    gQ = q;

    float hA;
    float hB;
    float hC;
    float dA = orbLayer(q, 4.0, gRadA, 4.0, hA);
    // The mid and small lattices sit half a cell off the big one, between its orbs.
    float dB = orbLayer(q + vec3(1.0, 1.0, 1.0), 2.0, gRadB, 8.0, hB);
    float dC = orbLayer(q + vec3(0.25, 0.25, 0.25), 1.0, gRadC, 16.0, hC);

    float d = dA;
    gMat = 0.0;
    gHash = hA;
    if (dB < d) { d = dB; gMat = 1.0; gHash = hB; }
    if (dC < d) { d = dC; gMat = 2.0; gHash = hC; }
    d /= gLip;

    // The flight channel, blended so the lattice thins toward it rather than cut.
    d = smax(d, gTube - length(w.xy), 0.6);
    d = max(d, OL_BUBBLE - length(w - gEye));
    return d;
}

/** olField through lib_shell's hooks: exactly olField(p) at rest, and olField read once, in a loop the compiler cannot unroll. */
float olFieldGrown(vec3 p) {
    shellStart3(p, SHELL_SPAWN, SHELL_FRACTAL, SHELL_FRONT, SHELL_BACK);
    float r = 0.0;
    for (int i = SHELL_FROM; i < 4; i++) {
        if (shellPass3(i)) r = shellKeep3(i, olField(shellAt3(i)));
    }
    return r;
}

/**
 * The field every march, normal and occlusion tap reads: olField, the style's
 * own, through lib_shell's hook (the identity until the shell drives it).
 */
float map(vec3 p) {
    return olFieldGrown(p);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

// The user's fold count when the kaleidoscope is on; otherwise the arm count
// the last spike chose. uFormPhase is a plateau: a spike picks the next arm
// count and it stays there until the next one (never stepping down a decay).
float foldCount() {
    if (uKaleido > 0.5 && uSymmetry >= 2.0) return uSymmetry;
    return 4.0 + 2.0 * floor(uFormPhase * 3.0);
}

void main() {
    vec2 uv = view();

    // The slew-limited companions, not the raw envelopes, for the fringe and the glow.
    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float bassA = clamp(SND_BASS_PRESENCE, 0.0, 1.3);
    float trebA = clamp(sndShaped(SND_HIGH), 0.0, 1.3);
    float enA = clamp(sndShaped(SND_LEVEL), 0.0, 1.3);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0) * clamp(0.5 + uKnob[0], 0.0, 1.5);
    float hat = clamp(SND_HAT_ENV, 0.0, 1.0);

    gFolds = max(foldCount(), 2.0);
    // A slow rigid turn of the whole fold; the catalogue's spin rolls the view on top.
    gSpin = uTime * 0.03;
    // The new spawn grows its offset in over a second rather than cutting to it.
    gShift = 0.6 * (uSpawnSeed - 0.5) * spawnGrow(1.1) * uMoveDir
           + 0.25 * vec2(sin(uTime * 0.19), cos(uTime * 0.23));
    // ---- the shell: this style's shape as normalized inputs ---------------
    //
    // Every tunable of the shape is a 0..1 input here, set from the params and
    // the six knobs and 0.5 at their defaults, and mapped to its range in this
    // one place: lib_shell's shellMap(n, lo, rest, hi) puts 0.5 exactly on the
    // value the style was tuned at, so the default look is unchanged, and the
    // shell's hooks can later fractal out, move and manipulate any of them.
    float nWarp = clamp(0.5 + (0.32 * uWarp + 0.2 * (uKnob[2] - 0.5)) / 0.9, 0.0, 1.0);
    float nMorph = 0.5 + 0.5 * smoothstep(0.6, 1.0, uMorph);
    float nTone = clamp(uKnob[3], 0.0, 1.0);
    float nDots = clamp(uKnob[1], 0.0, 1.0);
    float nTube = clamp(uKnob[5], 0.0, 1.0);
    float nHalo = clamp(uKnob[4], 0.0, 1.0);
    // The ABC stir's depth (none at rest, 0.45 its ceiling).
    gWarpA = shellMap(nWarp, 0.0, 0.0, 0.45);
    gDrift = vec3(uMoveDir * (uFlowPhase * 0.30), uFlowPhase * 0.18);
    gLip = 1.0 + 2.0 * gWarpA * OL_WF;
    // Spheres toward soft cubes.
    gMorph = shellMap(nMorph, 0.0, 0.0, 0.6);
    gTurb = clamp(uTurbulence, 0.0, 1.0);
    // Full kick at k0 = 1.0 gives 0.62 * 1.53 = 0.95, and 0.95 * 1.55 < 2.
    // The three lattices' orb radii (big, mid, small dots) and the flight tube.
    gRadA = shellMap(nTone, 0.806, 0.62, 0.434) * (1.0 + 0.35 * kick);
    gRadB = shellMap(nTone, 0.13, 0.26, 0.39);
    gRadC = shellMap(nDots, 0.08, 0.10, 0.12);
    gTube = shellMap(nTube, 1.7, 2.3, 2.9);
    // The halo between the orbs.
    float haloGain = shellMap(nHalo, 0.7, 1.0, 1.3);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 16 (see the header), so this
    // rebase is exact; without it a highp z far down the 8192 travel has too
    // little fraction left for the cells and the normals.
    ro.z = mod(ro.z, OL_ZWRAP);
    gEye = ro;
    // The grow hooks (lib_shell) open round this eye, in the rebased frame the world is read in.
    shellSetup3(ro, 3.0, 2.0, false);

    float t = 0.04;
    float hitT = -1.0;
    float halo = 0.0;
    for (int i = 0; i < OL_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (t > OL_FAR) break;
        float d = map(ro + rd * t);
        // Glow where the ray grazes an orb, faded by distance so far rows stay dim.
        halo += exp(-10.0 * max(d, 0.0)) * exp(-0.09 * t);
        float eps = 0.0012 + 0.0008 * t;
        if (d < eps) {
            hitT = t;
            break;
        }
        t += max(d * 0.8, eps);
    }
    // The halo is a per-step sum, so normalise by the budget the Detail gave it.
    halo *= 24.0 / max(uSteps, 24.0);

    float tone = 0.2 * (uKnob[3] - 0.5);
    vec3 fogCol = vec3(0.0, 0.08, 0.04) + pal(0.55 + tone) * 0.025 * pow(max(rd.z, 0.0), 6.0);
    vec3 col = fogCol;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.001 * hitT));
        // Restore the hit cell's globals after the normal taps.
        map(p);
        vec3 q = gQ;
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);

        // The old orb shading: bright face-on (its 0.35 + 0.65 * rim), the
        // hue rippling across the lattice. The z ripple has period 16.
        float hue = 0.55 + tone + 0.08 * sin(q.x * 0.7 + uTime * 0.2) + 0.04 * sin(p.z * (OL_TAU / 16.0));
        vec3 body = pal(hue) * (0.35 + 0.65 * ndv);
        if (gMat > 0.5) body = mix(body, pal(0.68 + tone) * 0.8, 0.45);
        if (gMat > 1.5) {
            // The small dots twinkle on the hats, each its own phase.
            float tw = 0.5 + 0.5 * sin(uTime * (6.0 + 9.0 * gHash) + gHash * 40.0);
            body *= 0.55 + 1.6 * hat * tw;
        }

        // RGB fringe: three channels read the rim at staggered angles, so red
        // leads and blue trails, wider as the treble rises.
        float split = mix(0.04, 0.16, clamp(trebA, 0.0, 1.0));
        float g = 1.0 - ndv;
        vec3 rim = vec3(smoothstep(0.55 - split, 1.0 - split, g),
                        smoothstep(0.55, 1.0, g),
                        smoothstep(0.55 + split, 1.0 + split, g));
        vec3 fringe = rim.g * pal(hue) + rim.b * pal(0.68 + tone) * 0.7
                    + rim.r * (1.0 - rim.g) * pal(0.02 + tone) * 0.8;

        vec3 L = normalize(vec3(-0.3, 0.8, -0.5));
        float spec = pow(max(dot(reflect(rd, n), L), 0.0), 40.0);
        vec3 orb = body * 0.55 + fringe * 0.9 + vec3(spec) * 0.35;
        col = mix(orb, fogCol, 1.0 - exp(-hitT * 0.075));
    }

    col += pal(0.55 + tone) * halo * (0.018 + 0.03 * bassA) * haloGain;
    col += enA * 0.35 * col * col;
    col *= 0.8 + 0.25 * swell;
    col *= 0.6 + 0.4 * smoothstep(2.2, 0.5, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
