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

// Nebula Ring: a planetary nebula - the shell a dying star blew off - as a
// real cloud in space. A hot white dwarf burns at the centre; around it a
// thick, ragged shell of glowing gas, squat along its axis and thinned at
// both poles, so seen down that axis it reads as the ring; the cavity inside
// is filled with a faint blue-green haze; a dim red halo trails outside; and
// along the inner edge the shell breaks into small dark cometary knots that
// silhouette against the glow behind them.
//
// The shell is layered by ionisation, as the real ones are: the gas nearest
// the star glows in one palette hue, the outer rim in another, and the colour
// runs between them across the shell's thickness. The ragged edge is 3D noise
// read on the unit sphere of DIRECTIONS (q / |q|), not on angles, so there is
// no atan anywhere and nothing to seam.
//
// ---- the scene and the camera ---------------------------------------------
//
// Volume-marched with the family's one loop (nebVolEmit, bound NEB_VOL_MAX,
// broken on uSteps, the Detail control, and on transmittance), only inside
// the bounding sphere NR_BOUND. The eye is the shared rig, object-centred:
// camAround circles the nebula at CAM_SPIN_UNIT per unit of travel, a whole
// number of turns per 8192, so the travel wrap is invisible (nothing here
// reads world z for tiling: the cloud is one object at the origin). The
// orbit is undone (as Ring does) so the eye always sits near the axis, and
// spent instead on the azimuth of the axis's tip: the barrel leans
// NR_TILT off the line of sight and that lean sweeps round as the rig
// travels, so the ring always reads as a ring and its depth shows as the
// near and far rims sliding past each other. The dolly pulls the eye in;
// k5 SPACE sets the orbit radius; the rig's look, roll and shake reach the
// rays as on every rig style.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (sndShaped(SND_LOW) with the bass band region) BREATHES THE SHELL: its
// thickness swells on the low end and slims in quiet passages, the coloured
// layers stretching with it; k0 PULSE scales it. A drop (SND_HELD_SURGE, as
// a 0-1-0 hump) blows the shell outward and back, a shock front, scaled by
// k4 BUILD. The hats twinkle the field stars. The key turns both shell hues
// (k3 TONE offsets them). Overall level follows
// (0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)) gently. Hits move geometry
// and hue only; nothing lifts the frame.
//
// ---- macros -----------------------------------------------------------------
//
// Speed is in uTime (nebTime): the slow churn of the edge and the lean's
// drift. Complexity: uMorph adds octaves to the shell's texture
// (nebVolOctaves, at most four). Chaos: uWarp/uTurbulence swirl the gas
// through nebWarp3; uTwist via view(). Colour: both ionisation hues, the
// haze and the halo are pal(). Energy: uIntensity scales the bass breathing
// (nebEnergy). Space: the rig. k1 TEXTURE sharpens the shell's fine fray,
// k2 FLOW deepens its coarse lobes.
//
// ---- why the march is safe ---------------------------------------------------
//
// This is a medium, not a surface: there is no distance estimate to
// overshoot. Every sample is bounded (two fbm calls at most, the fine one
// skipped where the Gaussian shell has no weight, one knot tap only in the
// knot band), the loop has a constant bound, and nebVolDt lengthens steps
// when Detail is low so a short budget still crosses the whole sphere.
// The shell's radius is capped at NR_RMAX and its thickness at NR_WMAX, so
// shell + 2 thicknesses (Gaussian weight 0.002) stays inside NR_BOUND, and
// the closest eye (NR_CAM_MIN * 0.65, full dolly) stays outside it: the march
// never starts inside the cloud.

#define NR_BOUND 2.1
#define NR_CAM_MIN 3.4
#define NR_CAM_MAX 4.4
/** Base shell radius and thickness, world units. */
#define NR_R0 1.0
#define NR_W0 0.17
#define NR_RMAX 1.4
#define NR_WMAX 0.33
/** The shell is squashed along its axis by this. */
#define NR_SQUASH 1.35
/** How far the axis leans off the line of sight, radians. */
#define NR_TILT 0.42
/** Extinction of the glowing gas and of the cometary knots, per world unit. */
#define NR_SIG_GAS 0.7
#define NR_SIG_KNOT 11.0

float gT;
float gOct;
float gR;
float gW;
float gLobe;
float gFray;
vec3 gInner;
vec3 gOuter;

float knob(int i) {
    return nebKnobIn(i) - 0.5;
}

/** The gas at q (nebula frame, axis +z): emission per unit length, and extinction. */
void gas(vec3 q, out vec3 emit, out float sigma) {
    q = nebWarp3(q, gT);
    float len = max(length(q), 1e-4);
    vec3 dir = q / len;
    // The ellipsoidal radius: the shell is squat along the axis.
    float r = length(vec3(q.xy, q.z * NR_SQUASH));

    // Coarse lobes from the direction alone, so they are rigid on the sphere.
    float lobes = nebFbm3(dir * 2.2 + vec3(gT * 0.012, -gT * 0.009, gT * 0.007), 2.0);
    float rr = min(gR * (1.0 + (lobes - 0.5) * gLobe), NR_RMAX);
    float x = (r - rr) / gW;

    // Fine fray from the position, only where the shell has weight.
    float fine = 0.5;
    if (abs(x) < 2.6) fine = nebFbm3(q * 3.0 + vec3(0.0, gT * 0.02, 0.0), gOct);
    fine = clamp(0.5 + (fine - 0.5) * gFray, 0.0, 1.0);

    // Thin at the poles, so down the axis the eye looks through a hole.
    float polar = 1.0 - 0.8 * smoothstep(0.45, 0.95, abs(dir.z));
    float shell = exp(-x * x * 1.6) * (0.55 + 0.9 * fine) * polar;
    emit = mix(gInner, gOuter, smoothstep(-1.0, 1.2, x)) * shell * 1.15;

    // The cavity's haze and the outer halo.
    emit += gInner * 0.07 * (1.0 - smoothstep(0.0, rr, r)) * (0.6 + 0.4 * fine);
    emit += gOuter * 0.05 * exp(-max(r - rr, 0.0) * 3.0) * smoothstep(rr - gW, rr + gW, r) * fine;
    sigma = NR_SIG_GAS * shell;

    // Cometary knots: small dark heads along the inner edge, dust that
    // blocks the glow behind it and barely glows itself.
    if (x > -1.6 && x < 0.3) {
        float band = smoothstep(-1.6, -0.5, x) * (1.0 - smoothstep(-0.4, 0.3, x));
        float knot = smoothstep(0.72, 0.9, nebValue3(q * 9.0 + 31.7)) * band;
        sigma += NR_SIG_KNOT * knot;
        emit *= 1.0 - 0.5 * knot;
    }
}

/** What the field holds at a point (lib_nebula_field): the gas's extinction (its density here) and its emission per unit length. */
struct NebHit {
    float dens;
    vec3 emit;
};

NebHit nebMap(vec3 p, float oct) {
    NebHit h;
    gas(p, h.emit, h.dens);
    return h;
}

//#include lib_nebula_field

void main() {
    vec2 uv = view();
    nebSetup();
    gT = nebTime();
    float e = nebEnergy();

    float kPulse = knob(0);
    float kTexture = knob(1);
    float kFlow = knob(2);
    float kTone = knob(3);
    float kBuild = knob(4);
    float kSpace = knob(5);

    float bass = clamp((0.6 * sndShaped(SND_LOW) + 0.4 * sndSpectrum(0.23)) * e, 0.0, 1.3);
    float swell = clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    float hats = clamp(SND_HAT_ENV, 0.0, 1.0);
    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float dropBump = 4.0 * drop * (1.0 - drop);
    gOct = nebVolOctaves(3.0);
    float key = nebKeyShift(0.1) + 0.15 * kTone;

    gR = NR_R0 * (1.0 + 0.12 * dropBump * (1.0 + kBuild));
    gW = min(NR_W0 * (1.0 + 0.55 * bass * (1.0 + kPulse)), NR_WMAX);
    gLobe = 0.33 * (1.0 + 0.8 * kFlow);
    gFray = 1.0 + 0.8 * kTexture;
    gInner = nebEmission(0.5 + key, 0.2);
    gOuter = nebEmission(0.0 + key, 0.1);

    float spin = CAM_SPIN_UNIT;
    float orbit = uCamTravel * spin;
    vec3 ro;
    vec3 rd;
    camAround(uv, mix(NR_CAM_MIN, NR_CAM_MAX, kSpace + 0.5), spin, ro, rd);
    // Undo the orbit's turn about +y (the eye then sits near -z, on the
    // axis), then lean the axis NR_TILT toward an azimuth that the orbit and
    // a slow drift sweep round. All rotations, so the sphere bound keeps.
    float ca = cos(orbit);
    float sa = sin(orbit);
    mat3 unturn = mat3(ca, 0.0, sa, 0.0, 1.0, 0.0, -sa, 0.0, ca);
    float beta = NR_TILT + 0.06 * sin(gT * 0.031);
    float cb = cos(beta);
    float sb = sin(beta);
    mat3 lean = mat3(1.0, 0.0, 0.0, 0.0, cb, sb, 0.0, -sb, cb);
    float phi = orbit + gT * 0.017;
    float cp = cos(phi);
    float sp = sin(phi);
    mat3 roll = mat3(cp, sp, 0.0, -sp, cp, 0.0, 0.0, 0.0, 1.0);
    mat3 toNeb = lean * roll * unturn;
    ro = toNeb * ro;
    rd = toNeb * rd;

    vec4 acc = vec4(0.0, 0.0, 0.0, 1.0);
    // The white dwarf is behind whatever gas lies in front of it: keep the
    // transmittance where the ray passes it.
    float tStar = -dot(ro, rd);
    float aStar = 1.0;
    bool starSeen = false;
    vec2 tb = nebSphere(ro, rd, vec3(0.0), NR_BOUND);
    if (tb.y > 0.0) {
        float tt = tb.x + nebDither() * 0.04;
        for (int i = 0; i < NEB_VOL_MAX; i++) {
            if (float(i) >= uSteps) break;
            if (tt > tb.y) break;
            float dt = nebVolDt(tt - tb.x, 0.03, 0.01);
            if (!starSeen && tt + dt > tStar) {
                aStar = acc.a;
                starSeen = true;
            }
            NebHit at = nebField(ro + rd * tt, gOct);
            vec3 em = at.emit;
            float sg = at.dens;
            nebVolEmit(acc, em, sg, dt);
            if (acc.a < NEB_TRANS_MIN) break;
            tt += dt;
        }
    }
    if (!starSeen) aStar = acc.a;

    // The white dwarf: a point glow at the origin, at least a pixel wide.
    vec3 col = acc.rgb;
    if (tStar > 0.0) {
        float d = length(ro + rd * tStar);
        float px = tStar * 2.0 / (max(uCamFocal, 0.1) * uResolution.y);
        float s2 = 0.0016 + px * px;
        col += vec3(0.82, 0.9, 1.0) * aStar * (exp(-d * d / s2) * 1.3 * min(1.0, 0.0016 / s2 * 1.5) + exp(-d * 7.0) * 0.16);
    }

    vec3 bg = pal(0.68 + key) * 0.012 + nebStars(nebSky(rd), 8.0, 2, vec2(0.0), 1.0, 0.2, hats, gT);
    col += acc.a * bg;
    col *= 0.85 + 0.15 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
