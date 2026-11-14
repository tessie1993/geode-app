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

// Truchet Flow: traffic on the paths, now flown through. Two dark boards face
// each other across a gap, a floor and a ceiling, and each carries the
// quarter-arc tiling as raised glass rails. Rounded capsules of light stream
// along every rail. They ride TruchetArc.s, the position along the arc in the
// path's own direction, which is consistent from tile to tile (lib_truchet:
// it follows the cell's checkerboard parity), and every arc holds a whole
// number of them, so each closed loop of the maze is still a circuit of moving
// lights, only now the circuits run above and below the eye into the fog.
// Each capsule is shaded from a bright head to a dim tail, and its light
// spills onto the board around the rail and hangs in the air as a glow.
//
// THE CAMERA. The shared rig, camFly: the eye rides the rig's path down +z
// between the boards (its height is held a little clear of either board, so
// the spiral path cannot bury it). The two boards are one board seen through
// a fold, |y|; the ceiling takes its tiles from a lattice shifted 64 cells
// in x (even, so the parity, and with it the flow direction, is kept).
//
// THE WRAP. The rig's travel wraps at 8192 along z. Every z term here has a
// period dividing 128 world units: the cell ids wrap at 128 cells along z
// (one cell is one world unit, 128 is even so the parity survives), and the
// Chaos bend and the colour field use only z frequencies that are whole
// multiples of TAU / 128. So the eye is rebased into 0..128 (power of two,
// exact) and highp z keeps its fraction however far the flight has gone.
//
// ---- audio ------------------------------------------------------------------
//
// The TEMPO sets the speed: the capsules advance exactly one spacing per beat,
// on the analyser's beat-clock phase (sndPhase(SND_CURVED_TIME, 2.0)), plus a
// slow drift so they never stop. The mids (SND_MID_PRESENCE) set the density:
// long capsules that close into a near-solid stream on a busy middle, short
// beads on a sparse one. Onsets swing a scattered quarter of the tiles a
// quarter turn (SND_HIT_TIME, SND_ONSET_ENV), so the traffic is re-routed on
// the hit. The bass (sndShaped(SND_LOW), slew-limited) breathes the gap: the
// boards close in on a heavy low end. Brightness follows
// (0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)) only.
//
// ---- macros and knobs -------------------------------------------------------
//
// Speed: uTime (already Speed-integrated) drives the drift and the hue.
// Complexity: uMorph puts more capsules on each arc. Chaos: uWarp/uTurbulence
// bend the boards' plan before it is tiled (lib_truchet's amounts, z-periodic
// frequencies), so the paths meander but stay joined. Energy: uIntensity
// scales the mid density and the bass breathing. Colour: boards, rails,
// heads and tails are pal(). Space: the camera rig. Knobs, each as an offset
// from 0.5 so off is the tuned look: k0 PULSE the head brightness, k1 TEXTURE
// the rails' sheen and girth (with the capsules'), k2 FLOW a phase lead of the stream, k3 TONE the hue,
// k4 BUILD the gap closing, k5 SPACE the fog.
//
// ---- why the distance estimate is safe ------------------------------------
//
// Inside a cell, the rails and capsules are a torus tube and capsules about
// that cell's own two arcs, and the arcs of the neighbours can only be closer
// once the point is near the cell's border. So inside the slab the objects
// can occupy, the distance is clamped to the cell border (plus a hair, so the
// march crosses it); above that slab the height over the tallest object is a
// true bound. The boards are planes, exact. The capsules measure length along
// the centreline, which stretches by at most (0.5 + d) / 0.5 off it, and the
// Chaos bend stretches the plan by up to ~1.9 (its slope stays under 1, so it
// never folds): the step is scaled down by both. The eye is held clear of the
// boards, so it never starts inside an object.

#define TF_MAX_STEPS 96
#define TF_FAR 34.0
/** Half the gap between the boards, world units, before the bass closes it. */
#define TF_GAP 2.3
/** Height of the rails' centreline above a board. */
#define TF_LIFT 0.075
#define TF_RAIL_R 0.045
#define TF_DASH_R 0.075
/** Cells along z before the tiling repeats; divides 8192. */
#define TF_ZWRAP 128.0
/** One turn per TF_ZWRAP world units: z frequencies are whole multiples of it. */
#define TF_ZK (6.2831853 / 128.0)

float gT;
float gHits;
float gGap;
float gPer;
float gPhase0;
float gDuty;
float gLip;
/** The rails' and the capsules' radii (TF_RAIL_R, TF_DASH_R at rest; TEXTURE thickens them). */
float gRailR;
float gDashR;

/** truchetWarp's bend with its z frequencies snapped to the 128 wrap (6 and 23 turns: 0.295 and 1.129, beside the lib's 0.31 and 1.13). */
vec2 flowWarp(vec2 p, float t) {
    float a = 0.8 * (2.0 * gTruchetIn.warp);
    float b = 0.18 * (2.0 * gTruchetIn.chaos);
    if (a + b <= 1e-4) return p;
    p += a * vec2(sin(p.y * (6.0 * TF_ZK) + t * 0.21), sin(p.x * 0.29 - t * 0.17 + 1.3));
    p += b * vec2(sin(p.y * (23.0 * TF_ZK) - t * 0.37 + 2.1), sin(p.x * 1.07 + t * 0.33));
    return p;
}

/** truchetHue with z-periodic frequencies: the smooth hue field, never the cell id. */
float flowField(vec2 p, float t) {
    float a = sin(p.x * 0.23 + 1.7 * sin(p.y * (3.0 * TF_ZK) + t * 0.05) + t * 0.03);
    float b = sin(p.y * (4.0 * TF_ZK) - p.x * 0.07 - t * 0.04 + 1.9);
    return 0.5 * (a + b);
}

/** What the field holds at a point (lib_truchet_field): the distance, and what the shading reads there. */
struct TruchetHit {
    float d;
    /** 0 board, 1 rail, 2 capsule. */
    float mat;
    float dash;
    float head;
    float tone;
    float arcD;
    float field;
};

TruchetHit truchetMap(vec3 p) {
    float ay = abs(p.y);
    float layer = p.y > 0.0 ? 64.0 : 0.0;
    float dBoard = gGap - ay;
    // Height of the point under the rails' centreline (positive toward the eye).
    float yy = (gGap - TF_LIFT) - ay;
    float topObj = yy - gDashR;

    vec2 w = flowWarp(p.xz, gT);
    vec2 id = floor(w);
    vec2 f = w - id - 0.5;
    id.y = mod(id.y, TF_ZWRAP);
    id.x += layer;
    TruchetArc a = truchetSquareAt(id, f, gHits, SND_ONSET_ENV, 4.0);

    float rad = length(vec2(a.d, yy));
    float dRail = rad - gRailR;

    // The capsule nearest along this arc, as in the flat style: whole number
    // per arc, offset from its centre wrapped across the fract seam.
    float seg = (TRUCHET_PI * 0.25) / gPer;
    float k = fract(a.s * gPer - gPhase0);
    float halfLen = 0.5 * gDuty * seg;
    float along = (k - 0.5 * gDuty) * seg;
    along = along - seg * floor(along / seg + 0.5);
    float dDash = length(vec2(max(abs(along) - max(halfLen - gDashR, 0.0), 0.0), rad)) - gDashR;

    float dObj = min(dRail, dDash);
    float toEdge = 0.5 - max(abs(f.x), abs(f.y));
    float d = min(dObj, max(topObj, toEdge + 0.02));
    d = min(d, dBoard);

    TruchetHit h;
    h.d = d * gLip;
    h.mat = dBoard < dObj ? 0.0 : (dDash < dRail + 0.004 ? 2.0 : 1.0);
    h.dash = dDash;
    // The stream moves toward rising s: the front of a capsule is its far end.
    h.head = clamp(along / max(2.0 * halfLen, 1e-4) + 0.5, 0.0, 1.0);
    h.tone = mix(1.0 - a.cap, a.cap, 1.0 - smoothstep(-0.02, 0.02, a.d));
    h.arcD = a.d;
    h.field = flowField(w, gT);
    return h;
}

//#include lib_truchet_field

void main() {
    vec2 uv = view();
    truchetSetup();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 128 (see THE WRAP): exact.
    ro.z = mod(ro.z, TF_ZWRAP);

    float t = truchetTime();
    float e = truchetEnergy();
    float swell = clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    float mids = clamp(SND_MID_PRESENCE * e, 0.0, 1.0);
    float morph = gTruchetIn.detail;
    float kPulse = gTruchetIn.pulse - 0.5;
    float kTexture = gTruchetIn.texture - 0.5;
    float kFlow = gTruchetIn.flow - 0.5;
    float kTone = gTruchetIn.tone - 0.5;
    float kBuild = gTruchetIn.build - 0.5;
    float kSpace = gTruchetIn.space - 0.5;

    gT = t;
    gHits = floor(SND_HIT_TIME);
    // The gap breathes on a slew-limited bass, never a hit, so it never jumps.
    float bass = clamp(sndShaped(SND_LOW), 0.0, 1.5) * e;
    gGap = TF_GAP * (1.0 - 0.09 * bass - 0.12 * kBuild);
    gPer = 3.0 + floor(morph * 2.0 + 0.5);
    gPhase0 = sndPhase(SND_CURVED_TIME, 2.0) + t * 0.12 + 0.5 * kFlow;
    gDuty = 0.22 + 0.55 * mids;
    // TEXTURE thickens the rails and the capsules (the shell's texture input), a third either way.
    gRailR = truchetAt(gTruchetIn.texture, TF_RAIL_R, 0.3);
    gDashR = truchetAt(gTruchetIn.texture, TF_DASH_R, 0.3);
    // The capsules' arc-length stretch (0.85), and the Chaos stretch: flowWarp's
    // largest slope, under 0.9 at the top of both sliders, bounds how far it
    // can stretch the plan, with margin.
    float chaos = 0.8 * (2.0 * gTruchetIn.warp) * 0.30 + 0.18 * (2.0 * gTruchetIn.chaos) * 1.13;
    gLip = 0.85 / (1.0 + 1.2 * chaos);

    // Keep the eye clear of both boards (the spiral path reaches |y| = 2).
    ro.y = clamp(ro.y, -gGap + 0.45, gGap - 0.45);
    // Grow and layers (lib_grow) measure from the eye in the frame the world is read in.
    growEye(ro);

    float hue = 0.05 * sin(t * 0.02) + 0.1 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue) + 0.12 * kTone;
    vec3 haze = mix(pal(hue + 0.7) * 0.03, pal(hue + 0.55) * 0.09, exp(-abs(rd.y) * 4.0));
    float fogK = 0.075 * (1.0 + 0.8 * kSpace);

    // The glow's colours, hoisted out of the loop (the march is the hot path).
    vec3 glowTail = pal(hue + 0.55) * 0.5;
    vec3 glowHead = mix(pal(hue + 0.15), vec3(1.0), 0.35);
    float tm = 0.02;
    float hitT = -1.0;
    vec3 glow = vec3(0.0);
    for (int i = 0; i < TF_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * tm;
        TruchetHit at = truchetField(p);
        float d = at.d;
        // Light hanging round the capsules: weighted by the step, so Detail
        // does not change how bright it is.
        glow += mix(glowTail, glowHead, at.head) * exp(-max(at.dash, 0.0) * 16.0) * min(d, 0.3) * exp(-tm * fogK);
        if (d < 0.0012 + 0.0007 * tm) {
            hitT = tm;
            break;
        }
        tm += d;
        if (tm > TF_FAR) break;
    }

    vec3 col = haze;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = truchetNormal(p, max(0.0012, 0.0008 * hitT));
        // What the field holds at the hit.
        TruchetHit hit = truchetField(p);
        float field = 0.15 * hit.field;
        vec3 tail = pal(hue + 0.55 + field);
        vec3 head = mix(pal(hue + 0.15 + field), vec3(1.0), 0.35);
        vec3 dashCol = mix(tail * 0.6, head * (1.0 + 0.6 * kPulse), hit.head);
        // The nearest capsule's light on whatever was hit.
        float spill = exp(-max(hit.dash, 0.0) * 9.0);

        vec3 L = normalize(vec3(0.3, 0.8, -0.45) * vec3(1.0, sign(-p.y + 1e-4), 1.0));
        float dif = max(dot(n, L), 0.0);
        float fres = pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 3.0);
        float spec = pow(max(dot(reflect(rd, n), L), 0.0), 32.0);

        if (hit.mat > 1.5) {
            col = dashCol * 1.25 + vec3(1.0) * spec * 0.3;
        } else if (hit.mat > 0.5) {
            vec3 glass = pal(hue + 0.6) * (0.06 + 0.10 * dif);
            col = glass + pal(hue + 0.6) * fres * (0.25 + 0.3 * kTexture) + vec3(0.8) * spec * (0.25 + 0.4 * kTexture);
            col += dashCol * spill * 0.6;
        } else {
            // The two-coloured board, with the rails' halo and the capsules' spill.
            col = mix(pal(hue + 0.7) * 0.03, pal(hue + 0.75) * 0.06, hit.tone) * (0.6 + 0.4 * dif);
            col += pal(hue + 0.55 + field) * exp(-abs(hit.arcD) * 18.0) * 0.07;
            col += dashCol * spill * 0.45;
        }
        col = mix(col, haze, 1.0 - exp(-hitT * fogK));
    }
    col += glow * 0.9;

    col *= 0.85 + 0.12 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
