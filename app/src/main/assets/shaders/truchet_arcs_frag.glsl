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
//#include lib_truchet

// Truchet Arcs: Smith's tiling taken into space, and the family's free style.
// The square tile's two quarter circles become the cube tile's three quarter
// tori: each cube of a lattice holds three tubes, each bent a quarter turn
// round one of three skew edges and joining the centres of the two faces that
// meet at that edge. Every face centre is used exactly once, so whichever tile
// a neighbour holds, its tube carries straight on through the face: the
// lattice fills with endless meandering pipes nobody drew, and the eye flies
// through them. The two colours stay: each tube is warm on the side that faces
// the slow key light and deep on the side turned away, with a thin dark seam
// running along every arc where the two meet. The seam follows the surface
// normal, which is continuous across every face, so it never draws the grid.
//
// ---- the tile --------------------------------------------------------------
//
// In cell coordinates (-0.5..0.5) the base tile's tubes ring the x edge at
// (y, z) = (+0.5, +0.5), the y edge at (x, z) = (+0.5, -0.5) and the z edge at
// (x, y) = (-0.5, -0.5), each of major radius 0.5. A quarter turn about z maps
// that tile onto another valid one (it equals a reflection of it), so a cell's
// tile is the base turned k quarter turns, k = 0..3 from its hash.
//
// ---- camera ----------------------------------------------------------------
//
// camFly: the eye rides the rig's path through the lattice, so the Camera
// tab's fly speed, the dolly, look and shake all apply. Cells are TA_CELL
// world units. The eye's z is rebased by TA_ZWRAP (256, which divides the
// rig's 8192 travel wrap): that is 128 cells, a multiple of the 64-cell id
// wrap, and every z term of the bend and the colour field has a period that
// divides 256, so neither the rebase nor the wrap ever shows.
//
// ---- audio -----------------------------------------------------------------
//
// Onsets REWIRE the lattice, as on the flat tiling: each hit re-rolls a
// scattered third of the cells (truchetRoll), and each one turns by the
// quarter turns between its old tile and its new one over the onset's envelope,
// so pipes break and re-join on the beat without a frame snapping. The bass
// (sndShaped(SND_LOW), under Energy and k0 PULSE) swells the tubes. The key
// (uKeyHue, weighted by uKeyStrength) drifts the palette. Brightness follows
// (0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)) only.
//
// ---- macros and knobs ------------------------------------------------------
//
// Speed: uTime drives the bend and the key light's drift. Complexity: uMorph
// slims the tubes, so the eye sees deeper into the lattice. Chaos: uWarp and
// uTurbulence bend space before it is tiled (as truchetWarp bends the plane),
// so the pipes meander but stay joined; uTwist twists the view. Energy:
// uIntensity scales the bass swell. Colour: both tones are pal(). Space: the
// camera rig. Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE the
// bass swell, k1 TEXTURE the seam width, k2 FLOW the glow the tubes cast in
// the air, k3 TONE the hue, k4 BUILD the far glow, k5 SPACE the fog depth.
//
// ---- why the distance estimate is safe -------------------------------------
//
// Each cell measures only its own three tori. Inside its cell a quarter torus
// is exactly the full torus (the rest of the ring lies in other cells), so the
// full-torus distance is exact there. A neighbour's tubes never come nearer a
// shared face than 0.5 - r >= 0.33 cells, except the one that joins this
// cell's tube at the face centre, which leaves square to the face just as
// this tube's continuation does: two radius-0.5 arcs tangent at the face part
// by under 0.13 within 0.25 of it. So the cell distance is clamped to the
// distance to the cell's walls plus TA_SLACK (0.12): a step can overshoot a
// face by no more than that. Tubes are capped at r = 0.17, so the three tubes
// of one cell (0.358 apart at their closest) never fuse. The bend is a smooth
// map applied before tiling; the distance is divided by its Lipschitz bound
// (the product of the four sine terms' 1 + amplitude * |gradient|), so the
// march never steps through a bent tube. A sphere round the eye is carved out
// of everything, so the eye is never inside a pipe. During a re-roll a turning
// cell is clipped at its walls for the envelope's 0.22 s; that is the rewire.

#define TA_MAX_STEPS 96
#define TA_TAU 6.2831853
/** World units per cell. */
#define TA_CELL 2.0
/** Rebase period of the eye's z, world units: divides 8192, and is 128 cells. */
#define TA_ZWRAP 256.0
/** Cells along z before the tile hash repeats: even, and divides the 128 cells of TA_ZWRAP. */
#define TA_IDWRAP 64.0
#define TA_FAR 34.0
#define TA_BUBBLE 0.55
/** How far past a cell wall a step may reach, cell units (see the header). */
#define TA_SLACK 0.12

vec3 gEye;
float gT;
float gHits;
float gEnv;
float gR;
float gWarpA;
float gTurbB;
float gLip;

/** The base tile's three quarter tori, at f in the cell (-0.5..0.5), tube radius r. Cell units. */
float tubes(vec3 f, float r) {
    float d1 = length(vec2(length(f.yz - vec2(0.5, 0.5)) - 0.5, f.x));
    float d2 = length(vec2(length(f.xz - vec2(0.5, -0.5)) - 0.5, f.y));
    float d3 = length(vec2(length(f.xy - vec2(-0.5, -0.5)) - 0.5, f.z));
    return min(d1, min(d2, d3)) - r;
}

/**
 * Chaos: a slow bend of space across the flight, world units, before tiling.
 * Only x and y move, by sines whose z periods (64, 32, 8, 16) divide TA_ZWRAP.
 * Its Lipschitz bound is gLip (set in main). Identity at default.
 */
vec3 bend(vec3 p) {
    if (gWarpA + gTurbB <= 1e-4) return p;
    p.x += gWarpA * sin(p.z * (TA_TAU / 64.0) + p.y * 0.21 + gT * 0.21);
    p.y += gWarpA * sin(p.z * (TA_TAU / 32.0) + p.x * 0.19 - gT * 0.17 + 1.3);
    p.x += gTurbB * sin(p.y * 0.83 + p.z * (TA_TAU / 8.0) - gT * 0.37 + 2.1);
    p.y += gTurbB * sin(p.x * 0.79 - p.z * (TA_TAU / 16.0) + gT * 0.33);
    return p;
}

float map(vec3 p) {
    vec3 q = bend(p) * (1.0 / TA_CELL);
    vec3 id = floor(q);
    vec3 f = q - id - 0.5;
    // The cell walls, measured before the tile turns.
    vec3 af = abs(f);
    float walls = 0.5 - max(af.x, max(af.y, af.z));

    // One whole-number id per cell for the roll: z wrapped, packed under y.
    float iz = mod(id.z, TA_IDWRAP);
    vec3 roll = truchetRoll(vec2(id.x, id.y * TA_IDWRAP + iz), gHits, gEnv, 3.0);
    float kOld = floor(roll.x * 4.0);
    float kNew = floor(roll.y * 4.0);
    // The shortest way round: -1, 0, +1 or 2 quarter turns.
    float turn = mod(kNew - kOld + 1.0, 4.0) - 1.0;
    float a = (kOld + turn * roll.z) * (0.5 * TRUCHET_PI);
    f.xy = truchetRot(-a) * f.xy;

    float d = min(tubes(f, gR), walls + TA_SLACK);
    d *= TA_CELL / gLip;
    return max(d, TA_BUBBLE - length(p - gEye));
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** A smooth, slowly moving field of position, about -1..1, for hue variation; z periods divide TA_ZWRAP. */
float hueField(vec3 p, float t) {
    float a = sin(p.x * 0.23 + 1.7 * sin(p.y * 0.17 + t * 0.05) + p.z * (TA_TAU / 128.0) + t * 0.03);
    float b = sin(p.y * 0.21 - p.x * 0.07 + p.z * (TA_TAU / 64.0) - t * 0.04 + 1.9);
    return 0.5 * (a + b);
}

void main() {
    vec2 uv = view();
    float t = truchetTime();
    float e = truchetEnergy();

    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kFlow = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    float kSpace = clamp(uKnob[5], 0.0, 1.0) - 0.5;

    float swell = clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    float bass = clamp(sndShaped(SND_LOW), 0.0, 1.5);

    gT = t;
    gHits = floor(SND_HIT_TIME);
    gEnv = SND_ONSET_ENV;
    gWarpA = 0.9 * clamp(uWarp, 0.0, 2.0);
    gTurbB = 0.25 * clamp(uTurbulence, 0.0, 2.0);
    // 1 + amplitude * |gradient of the sine's argument| for each of bend()'s four terms.
    gLip = (1.0 + 0.232 * gWarpA) * (1.0 + 0.273 * gWarpA) * (1.0 + 1.14 * gTurbB) * (1.0 + 0.88 * gTurbB);
    float r0 = mix(0.16, 0.1, truchetMorph());
    gR = min(r0 * (1.0 + 0.3 * bass * e * clamp(1.0 + 2.0 * kPulse, 0.0, 2.0)), 0.17);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Exact and invisible (see the header); keeps z's precision far down the travel.
    ro.z = mod(ro.z, TA_ZWRAP);
    gEye = ro;

    float tm = 0.05;
    float hitT = -1.0;
    float used = 0.0;
    float haze = 0.0;
    for (int i = 0; i < TA_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (tm > TA_FAR) break;
        used = float(i);
        float d = map(ro + rd * tm);
        // The pipes glow faintly into the air the ray grazes.
        haze += exp(-max(d, 0.0) * 5.0) * 0.02;
        float eps = 0.001 + 0.001 * tm;
        if (d < eps) {
            hitT = tm;
            break;
        }
        tm += max(d * 0.85, eps);
    }

    float hue = 0.05 * sin(t * 0.03) + 0.12 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue) + 0.12 * kTone;
    vec3 warmBase = pal(hue + 0.08);
    vec3 deepBase = pal(hue + 0.58);
    // The key light: mostly overhead, drifting slowly; its side of each tube is the warm tone.
    vec3 keyDir = normalize(vec3(0.35 * sin(t * 0.05), 1.0, 0.3 * cos(t * 0.04)));

    float ahead = pow(max(dot(rd, normalize(uCamBasis[2])), 0.0), 8.0);
    vec3 fogCol = deepBase * 0.06 + warmBase * (0.10 + 0.08 * swell) * (1.0 + 1.2 * kBuild) * ahead
                + vec3(0.003, 0.002, 0.005);
    vec3 col = fogCol;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.0012 * hitT));
        float h = hue + 0.07 * hueField(p, t);
        vec3 warm = pal(h + 0.08) * 0.85;
        vec3 deep = pal(h + 0.58) * 0.28;

        // The two tones and the seam between them, from the normal against the
        // key light: continuous across every face, so the seam runs along the arcs.
        float side = dot(n, keyDir);
        float soft = 0.05 + 0.003 * hitT;
        float tone = smoothstep(-soft, soft, side);
        float seamW = 0.09 * clamp(1.0 + 1.6 * kTexture, 0.2, 2.0);
        float seam = 1.0 - smoothstep(seamW - soft, seamW + soft, abs(side));
        vec3 albedo = mix(deep, warm, tone) * (1.0 - 0.75 * seam);

        float occ = clamp(map(p + n * 0.35) / 0.35, 0.0, 1.0);
        float ao = (0.45 + 0.55 * occ) * clamp(1.0 - used / max(uSteps, 1.0) * 0.7, 0.35, 1.0);
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        // Headlight on the eye, falling off into the lattice, plus the key light.
        float fall = 1.0 / (1.0 + 0.012 * hitT * hitT);
        float dif = max(side, 0.0);
        float spec = pow(max(dot(reflect(rd, n), keyDir), 0.0), 32.0) * (1.0 - seam);
        float fres = pow(1.0 - ndv, 3.0);

        vec3 lit = albedo * (0.10 + 0.85 * ndv * fall + 0.45 * dif) * ao
                 + mix(vec3(1.0), warm, 0.4) * spec * 0.35 * ao
                 + warm * fres * 0.12;
        float density = 0.055 * (1.0 - 0.8 * kSpace);
        col = mix(lit, fogCol, 1.0 - exp(-hitT * density));
    }

    col += warmBase * haze * 0.25 * clamp(1.0 + 1.6 * kFlow, 0.2, 2.0);
    col *= 0.85 + 0.12 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
