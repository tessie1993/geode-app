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
//#include lib_nebula

// Nebula Spiral: a grand-design spiral galaxy hanging in space, now a real
// disk with thickness. A warm, overexposed bulge swells out of the middle; two
// blue arms wind out of it on a logarithmic spiral; a thin dark dust lane in
// the mid-plane trails the inner edge of each arm and silhouettes against the
// glow behind it; pink star-forming knots are strung along the arms; resolved
// disk stars speckle the plane; field stars lie beyond.
//
// The arms are still a DENSITY WAVE, which is how real spiral arms work and
// how this style moves: the arm pattern's phase advances slowly and its pitch
// breathes, while the clumpy gas (3D noise fixed in the galaxy's frame) and
// the stars stay where they are. The arms sweep through the material - knots
// brighten as an arm reaches them and fade as it passes. The arm count is a
// whole number (nebSpiralWave) so there is no seam along the atan cut.
//
// ---- the scene and the camera -------------------------------------------------
//
// The galaxy plane is tilted 51 degrees toward the eye (GS_N), so the rig's
// resting view sees it a third of the way off face on: arms readable, the
// disk's thickness and the bulge's swell visible. The camera is the shared rig
// around one subject (camAround) turning one full revolution per 1024 world
// units of travel (CAM_SPIN_UNIT, an integer rate, so the rig's 8192 wrap is a
// whole number of turns and invisible). As it turns, the disk swings through
// edge on, where the dust lane becomes the dark band through the bulge, and
// back. Nothing samples world z along the travel, so the wrap touches nothing
// else. The rig's look, dolly and FOV apply as usual.
//
// ---- audio ------------------------------------------------------------------
//
// A kick (SND_KICK_ENV) sends a ring of compression out through the disk: the
// ring travels outward as the kick level falls, the arm gas it passes
// brightens, the knots light up there, and the disk PUFFS UP in thickness
// along the ring - geometry, so in an edge-on view the kick is a bulge moving
// out along the plane. It brightens arm material only where the ring is, never
// the frame. Mids (sndShaped(SND_MIDDLE)) lift the star-forming knots. The key
// turns the arm hue. The bulge follows SND_LEVEL gently. The hats twinkle the
// disk and field stars.
//
// Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE the kick ring's
// strength; k1 TEXTURE the clumps' contrast; k2 FLOW broadens the arms; k3
// TONE shifts the arm hue; k4 BUILD the knots' fire; k5 SPACE opens the arms'
// pitch. All continuous, none a trigger.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: the arms' winding and the twinkle run on uTime, which already
// integrates Speed. Complexity: uMorph adds a noise octave to the clumps
// (nebVolOctaves, at most four). Chaos: uWarp/uTurbulence swirl the disk
// through nebWarp3 before the spiral is read, bending the arms out of the
// plane too; uTwist via view(). Colour: bulge, arms, knots and dust from pal().
// Energy: uIntensity scales the kick ring and the mid lift. Space: the rig.
//
// ---- why the march is safe -------------------------------------------------------
//
// This is a volume, not a surface, so there is no distance estimate to
// overshoot. The ray is clipped analytically to the slab |height| < GS_SLAB in
// the galaxy frame intersected with a bounding sphere (nebSphere): outside
// that interval every term is below a thousandth. Inside, ONE loop bounded by
// NEB_VOL_MAX breaks on uSteps (Detail) and on transmittance, and the step is
// the interval split into a fixed count (nebVolDt lengthens it on a short
// budget), so any view - face on or edge on - reaches the far side. Each
// step's opacity is exact for its length (1 - exp(-sigma dt)), so a coarse
// step draws the same disk, only grainier. The eye never enters the volume:
// the dolly's nearest eye (GS_CAM_R * 0.65) is outside the sphere.

#define GS_TAU 6.2831853
/** World units per unit of the 2D style's galaxy plane. */
#define GS_SCALE 2.0
/** Half-height of the slab that holds every term, world units. */
#define GS_SLAB 0.5
#define GS_BOUND 2.7
#define GS_CAM_R 5.2
/** Disk half-thickness scale at the centre, world units. */
#define GS_THICK 0.2
/** Emission per world unit, tuned so a face-on column matches the flat style. */
#define GS_EMIT 5.0
/** Dust extinction per world unit at full lane. */
#define GS_DUST 12.0
/** Steps the interval is cut into at full Detail. */
#define GS_STEPS 44.0

/** The galaxy's normal: tilted toward the eye on the rig's resting axis (-z). Nx = 0, so GS_U is orthogonal to it. */
const vec3 GS_N = vec3(0.0, 0.6227, -0.7825);
const vec3 GS_U = vec3(1.0, 0.0, 0.0);

float gT;
float gOct;
float gWind;
float gPitch;
float gSharp;
float gTex;
float gFront;
float gRing;

/** World point to galaxy frame: (plane x, height, plane z). */
vec3 galaxy(vec3 p) {
    vec3 v = cross(GS_N, GS_U);
    return vec3(dot(p, GS_U), dot(p, GS_N), dot(p, v));
}

/** The arm density 0..1 at plane point g (2D-style units). */
float armAt(vec2 g) {
    return pow(0.5 + 0.5 * nebSpiralWave(g, 2.0, gPitch, gWind), gSharp);
}

/**
 * What the field holds at a point (lib_nebula_field): the arms' gas (its density here), the bulge's
 * glow, the ring's puff, the star-forming knots and the dust lane, each an amount the march colours.
 */
struct NebHit {
    float dens;
    float bulge;
    float ring;
    float knots;
    float dust;
};

NebHit nebMap(vec3 p, float oct) {
    vec3 q = galaxy(nebWarp3(p, gT));
    vec2 g = q.xz / GS_SCALE;
    float r = length(g);
    float disk = exp(-r * 2.5);
    NebHit h;

    // The ring puffs the disk up where it passes: geometry, not light.
    h.ring = exp(-pow((r - gFront) / 0.09, 2.0)) * gRing;
    float hgt = GS_THICK * (0.35 + 0.65 * exp(-r * 1.5)) * (1.0 + 0.7 * h.ring);
    float hy = q.y / hgt;
    float vert = exp(-hy * hy);
    // The bulge: an oblate ball, the 2D r*r/0.02 law in 3D.
    vec3 bq = vec3(g.x, q.y * 1.6 / GS_SCALE, g.y);
    float bb = dot(bq, bq);
    h.bulge = exp(-bb / 0.02) * 2.5 + exp(-sqrt(bb) * 6.0) * 0.35;

    h.dens = 0.0;
    h.knots = 0.0;
    h.dust = 0.0;
    if (vert * disk > 0.002) {
        float clump = nebFbm3(q * 1.6 + vec3(4.0, 1.3, 2.1), oct);
        clump = clamp(0.5 + (clump - 0.5) * gTex, 0.0, 1.0);
        float arm = armAt(g);
        float lane = pow(0.5 + 0.5 * nebSpiralWave(g, 2.0, gPitch, gWind + 0.8), 8.0);
        h.dens = disk * vert * (0.12 + 0.95 * arm * (0.6 + 0.6 * clump));
        // Star-forming knots: dense clumps that sit in an arm.
        h.knots = smoothstep(0.6, 0.82, clump) * arm * disk * vert;
        // The dust lane is thinner than the gas: it hugs the mid-plane.
        float hd = hy * 1.7;
        h.dust = lane * disk * exp(-hd * hd) * (0.6 + 0.6 * clump);
    }
    return h;
}

//#include lib_nebula_field

void main() {
    vec2 uv = view();
    nebSetup();
    gT = nebTime();
    float e = nebEnergy();

    float kPulse = gNebIn.pulse - 0.5;
    float kTexture = gNebIn.texture - 0.5;
    float kFlow = gNebIn.flow - 0.5;
    float kTone = gNebIn.tone - 0.5;
    float kBuild = gNebIn.build - 0.5;
    float kSpace = gNebIn.space - 0.5;

    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float mid = clamp(sndShaped(SND_MIDDLE), 0.0, 1.3) * e;
    float hats = clamp(SND_HAT_ENV, 0.0, 1.0);
    float key = nebKeyShift(0.1) + 0.12 * kTone;
    gOct = nebVolOctaves(3.0);

    // The density wave: pattern phase and pitch move, the material does not.
    gWind = gT * 0.035;
    gPitch = 0.36 + 0.05 * sin(gT * 0.021) + 0.12 * kSpace;
    gSharp = 2.5 * (1.0 - 0.8 * kFlow);
    gTex = 1.0 + 1.2 * kTexture;
    // The kick ring: where the compression front is right now, in plane units.
    gFront = 0.08 + 1.1 * (1.0 - kick);
    gRing = kick * e * (1.0 + 1.2 * kPulse);

    vec3 ro;
    vec3 rd;
    camAround(uv, GS_CAM_R, CAM_SPIN_UNIT, ro, rd);

    vec3 armCol = nebEmission(0.56 + key, 0.25);
    vec3 knotCol = nebEmission(0.92 + key, 0.2);
    vec3 bulgeCol = nebEmission(0.08 + key, 0.85);
    vec3 absorb = nebAbsorption(0.3 + key);
    float knotGain = (0.7 + 0.6 * mid) * (1.0 + 1.2 * kBuild);

    // The march interval: bounding sphere meets the slab.
    // The bounding sphere, widened by growSlack while a grow control is on (lib_grow: a twin, children, a
    // layer opened); as it was at rest.
    vec2 tb = nebSphere(ro, rd, vec3(0.0), GS_BOUND + growSlack(GS_BOUND, 1.0, 0.2));
    float rn = dot(rd, GS_N);
    float on = dot(ro, GS_N);
    // The slab, widened as the sphere is.
    float slabH = GS_SLAB + growSlack(GS_SLAB, 1.0, 0.2);
    vec2 ts;
    if (abs(rn) < 1e-4) {
        ts = abs(on) < slabH ? vec2(0.0, 1e6) : vec2(1.0, -1.0);
    } else {
        float a = (-slabH - on) / rn;
        float b = (slabH - on) / rn;
        ts = vec2(min(a, b), max(a, b));
    }
    float t0 = max(tb.x, ts.x);
    float t1 = min(tb.y, ts.y);
    // Where the ray crosses the mid-plane, for the resolved disk stars.
    float tp = abs(rn) < 1e-4 ? -1.0 : -on / rn;

    vec3 acc = vec3(0.0);
    vec3 trans = vec3(1.0);
    float starTrans = -1.0;
    if (tb.y > 0.0 && t1 > t0) {
        float dt = nebVolDt(0.0, (t1 - t0) / GS_STEPS, 0.0);
        float tt = t0 + nebDither() * dt;
        for (int i = 0; i < NEB_VOL_MAX; i++) {
            if (float(i) >= uSteps) break;
            if (tt > t1) break;
            if (starTrans < 0.0 && tt >= tp) starTrans = (trans.r + trans.g + trans.b) / 3.0;

            NebHit at = nebField(ro + rd * tt, gOct);
            vec3 emit = bulgeCol * at.bulge * (0.85 + 0.2 * swell);
            vec3 sigma = vec3(0.0);
            if (at.dens > 0.0) {
                emit += armCol * at.dens * (1.0 + 0.9 * at.ring) * GS_EMIT;
                emit += knotCol * at.knots * (knotGain + 1.2 * at.ring) * GS_EMIT;
                sigma = absorb * at.dust * GS_DUST + vec3(0.8 * at.dens);
            }

            // Emission added in full, extinction per channel (dust reddens).
            acc += trans * emit * dt;
            trans *= exp(-sigma * dt);
            if (max(trans.r, max(trans.g, trans.b)) < NEB_TRANS_MIN) break;
            tt += dt;
        }
    }

    // Resolved disk stars on the mid-plane, behind whatever dust lies in front.
    if (tp > 0.0) {
        if (starTrans < 0.0) starTrans = (trans.r + trans.g + trans.b) / 3.0;
        vec3 q = galaxy(ro + rd * tp);
        vec2 g = q.xz / GS_SCALE;
        float r = length(g);
        float disk = exp(-r * 2.5);
        float arm = armAt(g);
        float face = smoothstep(0.05, 0.3, abs(rn));
        acc += nebStars(g, 16.0, 1, vec2(0.0), 0.7, (0.15 + 0.5 * arm) * disk * 1.6, hats, gT) * 0.8 * starTrans * face;
    }

    // Field stars, beyond everything.
    vec3 bg = nebStars(nebSky(rd), 6.0, 2, vec2(0.0), 1.0, 0.18, hats, gT);
    vec3 col = acc + trans * bg;
    col *= 0.75 + 0.25 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
