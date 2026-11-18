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

// Truchet Tunnel: a real tube flown through, its wall tiled with Smith's
// quarter-arc Truchet tiles. The old style painted the tiling on the screen in
// log-polar coordinates; now the tiling lives on the inside of a cylinder
// around the world's z axis. The two-colouring is the floor of the wall, and
// the arcs themselves stand out of it as rounded pipes (a height field raised
// along the boundary between the two colours), with a thin glowing line along
// each crest. A headlight on the eye lights the wall, fog fills the bore and
// the far end glows where the rays never land.
//
// The tube is a shell (|wall - r| < thickness), so from the rig's orbit and
// wide glide paths, where the eye leaves the bore, it reads as a pipe from
// the outside instead of a wall of black.
//
// ---- the cells, and why neither seam shows ----------------------------------
//
// A cell's coordinates are u = angle * N / TAU (N cells round the tube) and
// v = z / 2 (cells 2 world units long). atan() jumps by a full turn on the
// negative x axis, so u jumps by exactly N there: N is a whole EVEN number,
// the fract() in the cell is unchanged, id.x is wrapped mod N before it is
// hashed, and an even N keeps the checkerboard parity (which sets the
// two-colouring and the arcs' orientation). The eye's z is rebased by
// TT_ZWRAP = 64, which divides the rig's 8192 travel wrap: 32 cells, an even
// count, and id.y is wrapped mod 32. The resting spiral winds u by N per 64
// world units (one whole turn of the tiling per rebase), the drop's corkscrew
// is measured from the eye (so the rebase moves both sides alike), the stream
// is wrapped by 32 cells, and every other z or v term below has a period that
// divides 32 cells. Every angular term is a whole multiple of the angle.
//
// ---- camera ------------------------------------------------------------------
//
// camFly: the eye rides the rig down the bore (fly speed, the dolly's kick
// punches and drop pushes are already in uCamPos). The tiles also stream
// toward the eye on uFlowTime, as the old flight did, so loudness still sets
// the pace (never the direction) even when the rig is slow.
//
// ---- audio -------------------------------------------------------------------
//
// Onsets (SND_HIT_TIME, SND_ONSET_ENV)  re-roll a scattered third of the
//                         tiles: each turns a quarter turn to its new diagonal
//                         and its colouring cross-fades (lib_truchet's roll).
// Drop (SND_HELD_SURGE)   corkscrews the tube: a twist growing with distance
//                         from the eye, eased in and out over the drop (a
//                         bump, so it starts and ends at rest).
// Bass (SND_LOW, shaped)  breathes the tube's width.
// SND_INTENSITY           the overall level and the far end's glow.
// Brightness never moves with a hit; only geometry does, as in the family.
//
// ---- macros and knobs -----------------------------------------------------------
//
// Speed is in uTime and uFlowTime. Complexity: uMorph past 0.5 puts 16 cells
// round the tube instead of 12. Chaos: uWarp/uTurbulence ripple the cell grid
// with wrap-safe waves (truchetChaos); uTwist in view(). Energy: uIntensity
// scales the corkscrew and the bass breathing. Colour: both tones, the pipes
// and the lines are pal(). Space: view() and the rig.
// Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE the corkscrew's
// strength, k1 TEXTURE the pipes' width, k2 FLOW the crest lines' glow, k3
// TONE the hue, k4 BUILD the far glow, k5 SPACE the bore radius.
//
// ---- why the distance estimate is safe ----------------------------------------
//
// The wall is r_w = R - H * ridge(|a.d|), with a.d the distance to the tile's
// arc in cell units (1-Lipschitz in a cell) and ridge = (1 - (d / W)^2)^2,
// whose slope is at most 1.54 * H / W per cell unit. r_w depends on the angle
// and z only, so the radial gap's gradient is sqrt(1 + |grad r_w|^2), and
// |grad r_w| <= 1.54 H / W * G, where G bounds the cell map's Jacobian per
// world unit on the band (angle at the band's inner radius, the spiral, the
// corkscrew, the Chaos ripples), all recomputed per frame into gLip. Across a
// tile border the arc distance is continuous (Smith's arcs meet at the edge
// midpoints, square to the edge), so the height field has no cliff except for
// the 0.22 s a tile turns during a re-roll; the 0.8 step factor covers that.
// Off the band (inside its inner radius or outside its outer one) the field is
// the plain radial distance to the band, a true lower bound. A sphere round
// the eye is carved out of the shell, and the march starts where the ray
// leaves the empty inner cylinder.

#define TT_MAX_STEPS 96
#define TT_TAU 6.2831853
/** Rebase period of the eye's z; divides 8192. */
#define TT_ZWRAP 64.0
/** World units per cell along z. */
#define TT_CELLZ 2.0
/** Cells along z per rebase: TT_ZWRAP / TT_CELLZ, even. */
#define TT_ZCELLS 32.0
/** Bore radius at rest, world units: wider than the glide path's usual wander. */
#define TT_RADIUS 4.2
/** Half-thickness of the shell. */
#define TT_SHELL 0.22
/** How far the pipes stand out of the wall. */
#define TT_RIDGE_H 0.3
#define TT_FAR 44.0
#define TT_BUBBLE 0.7

float gCells;
float gSpiral;
float gScrew;
float gStream;
float gChaos;
float gT;
float gRadius;
float gRidgeW;
float gLip;
float gHits;
float gEnv;
vec3 gEye;
/** What the field holds at a point (lib_truchet_field): the distance and, on the band, the arc, the cell coordinate and the ridge profile. */
struct TruchetHit {
    float d;
    TruchetArc arc;
    vec2 q;
    float ridge;
};

/** The field off the band: the distance; the rest (never shaded there) at a plain wall's. */
TruchetHit tunnelOff(float d) {
    TruchetHit h;
    h.d = d;
    h.arc = TruchetArc(1.0, 0.0, 0.0, vec2(1.0, 0.0), 0.0);
    h.q = vec2(0.0);
    h.ridge = 0.0;
    return h;
}

/** The point's cell coordinate (u round the tube, v along it); see the header for the seams. */
vec2 cellAt(vec3 p, float ang) {
    float v = p.z * (1.0 / TT_CELLZ) + gStream;
    float u = ang * (gCells / TT_TAU) + gSpiral * p.z + gScrew * (p.z - gEye.z);
    if (gChaos > 1e-4) {
        u += 0.12 * gChaos * sin(v * (TT_TAU / 16.0) + gT * 0.2);
        v += 0.12 * gChaos * sin(ang * 3.0 + v * (TT_TAU / 32.0) - gT * 0.17);
    }
    return vec2(u, v);
}

TruchetHit truchetMap(vec3 p) {
    float r = length(p.xy);
    float dEye = TT_BUBBLE - length(p - gEye);
    float inner = gRadius - TT_RIDGE_H - TT_SHELL;
    float outer = gRadius + TT_SHELL;
    // Off the band: the radial distance to it, which no part of the shell beats.
    if (r < inner - 0.3) return tunnelOff(max(inner - r, dEye));
    if (r > outer + 0.3) return tunnelOff(max(r - outer, dEye));

    float ang = atan(p.y, p.x);
    vec2 q = cellAt(p, ang);
    vec2 id = floor(q);
    id = vec2(mod(id.x, gCells), mod(id.y, TT_ZCELLS));
    TruchetArc a = truchetSquareAt(id, fract(q) - 0.5, gHits, gEnv, 3.0);
    float x = clamp(abs(a.d) / gRidgeW, 0.0, 1.0);
    float c = 1.0 - x * x;
    float ridge = c * c;
    TruchetHit h;
    h.arc = a;
    h.q = q;
    h.ridge = ridge;
    float wall = gRadius - TT_RIDGE_H * ridge;
    float d = (abs(wall - r) - TT_SHELL) / gLip;
    h.d = max(d, dEye);
    return h;
}

//#include lib_truchet_field

void main() {
    vec2 uv = view();
    truchetSetup();

    float kPulse = gTruchetIn.pulse - 0.5;
    float kTexture = gTruchetIn.texture - 0.5;
    float kFlow = gTruchetIn.flow - 0.5;
    float kTone = gTruchetIn.tone - 0.5;
    float kBuild = gTruchetIn.build - 0.5;
    float kSpace = gTruchetIn.space - 0.5;

    gT = truchetTime();
    float e = truchetEnergy();
    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float screw = 4.0 * drop * (1.0 - drop) * e;

    gCells = gTruchetIn.detail >= 0.5 ? 16.0 : 12.0;
    gSpiral = gCells / TT_ZWRAP;
    gScrew = 0.35 * screw * clamp(1.0 + kPulse, 0.5, 1.5);
    gChaos = truchetChaos();
    // Streams toward the eye; wrapped by a whole (even) number of cells, so the wrap never shows.
    gStream = mod(uFlowTime * 0.3, TT_ZCELLS);
    gHits = floor(SND_HIT_TIME);
    gEnv = SND_ONSET_ENV;
    gRadius = (TT_RADIUS + 1.0 * kSpace) / mix(1.0, truchetBreath(sndShaped(SND_LOW)), 0.5);
    gRidgeW = 0.17 * clamp(1.0 + 0.8 * kTexture, 0.6, 1.4);

    // Lipschitz bound of the band's field (header): the cell map's Jacobian
    // on the band's inner radius, then the ridge's slope through it.
    float rBand = max(gRadius - TT_RIDGE_H - TT_SHELL - 0.3, 1.0);
    float gv = (1.0 / TT_CELLZ) * (1.0 + 0.12 * gChaos * (TT_TAU / 32.0)) + 0.36 * gChaos / rBand;
    float gAng = gCells / (TT_TAU * rBand);
    float gz = gSpiral + abs(gScrew);
    float gu = sqrt(gAng * gAng + gz * gz) + 0.12 * gChaos * (TT_TAU / 16.0) * gv;
    float G = sqrt(gu * gu + gv * gv);
    float slope = 1.54 * TT_RIDGE_H / gRidgeW * G;
    gLip = 1.05 * sqrt(1.0 + slope * slope);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 64 (header), so this shift is
    // exact and invisible, and z keeps its precision far down the travel.
    ro.z = mod(ro.z, TT_ZWRAP);
    // Grow and layers (lib_grow) measure from the eye in the frame the world is read in.
    growEye(ro);
    gEye = ro;

    // Skip the empty middle: nothing of the shell comes nearer the axis than this.
    float rin = gRadius - TT_RIDGE_H - TT_SHELL - 0.05;
    float t = 0.03;
    float a = dot(rd.xy, rd.xy);
    float cc = dot(ro.xy, ro.xy) - rin * rin;
    if (rin > 0.5 && cc < 0.0) {
        if (a < 1e-6) {
            t = TT_FAR;
        } else {
            float b = dot(ro.xy, rd.xy);
            t = max(t, (-b + sqrt(max(b * b - a * cc, 0.0))) / a);
        }
    }

    float hitT = -1.0;
    float used = 0.0;
    for (int i = 0; i < TT_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (t > TT_FAR) break;
        used = float(i);
        float d = truchetField(ro + rd * t).d;
        float eps = 0.0008 * t + 0.0008;
        if (d < eps) {
            hitT = t;
            break;
        }
        t += max(d * 0.8, eps);
    }

    float hue = 0.04 * sin(gT * 0.02) + 0.1 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue) + 0.12 * kTone;
    float ahead = pow(max(dot(rd, normalize(uCamBasis[2])), 0.0), 10.0);
    vec3 fogCol = pal(hue + 0.3) * (0.02 + (0.18 + 0.2 * swell) * (1.0 + kBuild) * ahead) + vec3(0.003, 0.002, 0.006);
    vec3 col = fogCol;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = truchetNormal(p, max(0.0015, 0.0012 * hitT));
        // What the field holds at the hit: the arc, the cell coordinate, the ridge.
        TruchetHit hit = truchetField(p);

        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        // One pixel in cell units, widened toward grazing angles.
        float pxWorld = hitT * 2.0 / (max(uCamFocal, 0.1) * uResolution.y);
        float pxCell = max(pxWorld * G * (1.0 + 2.0 * (1.0 - ndv)), 1e-4);

        // The two tones trade along the tube, from a smooth field of v (period 16 cells).
        float along = 0.5 + 0.5 * sin(hit.q.y * (TT_TAU / 16.0));
        vec3 toneA = pal(hue + 0.05 + 0.2 * along) * 0.8;
        vec3 toneB = pal(hue + 0.55 + 0.2 * along) * 0.22;
        vec3 floorCol = mix(toneB, toneA, truchetTone(hit.arc, pxCell));
        vec3 pipeCol = pal(hue + 0.3) * 0.55;
        vec3 albedo = mix(floorCol, pipeCol, smoothstep(0.05, 0.6, hit.ridge));

        float fall = 1.0 / (1.0 + 0.025 * hitT * hitT);
        float fres = pow(1.0 - ndv, 3.0);
        float spec = pow(ndv, 28.0) * hit.ridge;
        // The floor right beside a pipe sits in its shade.
        float nook = 1.0 - 0.35 * (1.0 - hit.ridge) * (1.0 - smoothstep(1.0, 2.0, abs(hit.arc.d) / gRidgeW));
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 0.8, 0.35, 1.0) * nook;

        vec3 wall = albedo * (0.12 + 0.88 * ndv * fall) * ao
                  + pal(hue + 0.6) * fres * 0.1
                  + vec3(spec) * 0.35 * fall;

        // The glowing line along each crest: emissive, so it carries down the bore.
        float line = truchetLine(hit.arc.d, 0.035, pxCell);
        wall += pal(hue + 0.3) * 1.1 * line * clamp(1.0 + 1.2 * kFlow, 0.3, 2.0) * (0.75 + 0.25 * fall);

        col = mix(wall, fogCol, 1.0 - exp(-hitT * 0.05));
    }

    col *= 0.85 + 0.12 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.4, 2.2, length(uv) * uZoom));
    fragColor = vec4(grade(col), 1.0);
}
