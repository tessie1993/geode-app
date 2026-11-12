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

// Nebula Dust: the family's plain form, and its free style, now flown through.
// A deep wall of glowing gas fills the view; between it and the eye hang
// sheets of dark dust that wind through space, seen edge-on as long black
// lanes and face-on as a brown veil, with dense dark globules where the dust
// peaks; stars shine through wherever both thin out. The flat style faked
// depth with three drifting layers (stars slowest, gas faster, dust fastest);
// here the parallax is real: the dust is near and slides past quickly, the gas
// is large and slow, the stars are at infinity.
//
// Volume-marched on the rig's path (camFly). One loop (NEB_VOL_MAX, breaking
// on uSteps, the Detail control), two fbm reads of 3D value noise per sample:
// the gas at scale 1/4 (at most three octaves, it is soft), the dust at scale
// 1/2. The gas emits where its fbm crests, in two palette hues that follow its
// height, as the flat style's did. The dust is the same fbm read as ridges
// around its 0.5 contour, which in 3D is a winding SHEET rather than a line,
// plus globules where it peaks. Transmittance is carried per channel, so dust
// extinguishes through nebAbsorption and every lane reddens at its edges
// before it goes black, as real dust does. A bubble around the eye fades the
// dust in from 0.3 to 1.6 units so the camera never flies blind into a lane.
//
// WHY THE FLIGHT NEVER JUMPS: both noise scales are powers of two (1/4, 1/2)
// and nebFbm3's octaves double exactly, so the lattice z wraps with the rig's
// 8192-unit travel (lib_nebula, NEB_Z_WRAP); the drift offsets move only x
// and y; the hue's slow sway along the path has a 64-unit period, which
// divides 8192. There is no distance estimate to trust: a volume is sampled,
// never stepped by a bound, so the only safety needed is the constant loop
// bound and the transmittance break, both below.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (sndShaped(SND_LOW), steadied with the bass band region) DEEPENS THE
// COLOUR, as it did in 2D: the gas threshold drops so the glow spreads into
// more of the cloud, its hue saturates (nebRich), and the dust sheets thicken
// and darken - a heavy low end pulls the whole nebula denser and richer, a
// quiet passage thins it to wisps. The hats set the stars twinkling. The key
// turns the gas hue a little. Overall level follows SND_INTENSITY gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: already in uTime and uFlowTime, which drive the gas churn and the
// twinkle (the flight itself is the rig's). Complexity: uMorph adds noise
// octaves (nebVolOctaves, at most four). Chaos: uWarp/uTurbulence swirl gas
// and dust together through nebWarp3; uTwist via view(). Colour: gas, haze and
// dust tint are pal(). Energy: uIntensity scales the bass push. Space: the rig.
//
// Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE the gas glow,
// k1 TEXTURE how thin the dust sheets are, k2 FLOW how far the glow spreads,
// k3 TONE the hue, k4 BUILD the white heat of the gas crests, k5 SPACE the
// dust's opacity.

#define DU_FAR 28.0
/** Extinction of the dust per world unit at full ridge. */
#define DU_SIGMA 1.4
/** Hue sway along the path: a 64-unit period, which divides the rig's 8192 wrap. */
#define DU_Z_FREQ 0.09817477

/** The frame's medium settings, set in main() before the march: the drifts, the gas crest's threshold, the dust ridge's floor, the gas's octaves. */
vec3 gGasDrift;
vec3 gDustDrift;
float gThr;
float gRidgeLo;
float gGasOct;

/**
 * What the field holds at a point (lib_nebula_field): the dust's optical depth (its density here,
 * before the fade by the eye), the gas field and its glowing crests, and the warped point's z (the hue
 * sways along it).
 */
struct NebHit {
    float dens;
    float gas;
    float glow;
    float z;
};

NebHit nebMap(vec3 p, float oct) {
    p = nebWarp3(p, nebTime());
    NebHit h;
    h.z = p.z;
    // Emission: the crests of the gas field.
    h.gas = nebFbm3(p * 0.25 + gGasDrift, gGasOct);
    h.glow = smoothstep(gThr, gThr + 0.36, h.gas);
    // Dust: winding sheets along the field's mid contour, globules at its peaks.
    float dust = nebFbm3(p * 0.5 + gDustDrift, oct);
    float ridge = 1.0 - abs(2.0 * dust - 1.0);
    h.dens = smoothstep(gRidgeLo, 0.95, ridge) + 1.6 * smoothstep(0.66, 0.86, dust);
    return h;
}

//#include lib_nebula_field

void main() {
    vec2 uv = view();
    nebSetup();
    float t = nebTime();
    float flow = nebFlow();
    float e = nebEnergy();
    float bass = clamp(0.6 * sndShaped(SND_LOW) + 0.4 * sndSpectrum(0.23), 0.0, 1.3) * e;
    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float hats = clamp(SND_HAT_ENV, 0.0, 1.0);
    float oct = nebVolOctaves(3.0);
    gGasOct = min(oct, 3.0);
    float key = nebKeyShift(0.12);

    float kPulse = gNebIn.pulse - 0.5;
    float kTexture = gNebIn.texture - 0.5;
    float kFlow = gNebIn.flow - 0.5;
    float kTone = gNebIn.tone - 0.5;
    float kBuild = gNebIn.build - 0.5;
    float kSpace = gNebIn.space - 0.5;
    key += 0.12 * kTone;

    // The gas crest threshold: bass and FLOW spread the glow lower.
    gThr = 0.46 - 0.08 * bass - 0.06 * kFlow;
    // The dust ridge: TEXTURE thins the sheets, bass thickens them.
    gRidgeLo = 0.58 + 0.12 * kTexture - 0.06 * bass;
    float dustK = DU_SIGMA * (1.0 + 0.8 * bass) * (1.0 + 0.8 * kSpace);
    float gain = 0.16 * (0.85 + 0.25 * bass) * (1.0 + 0.6 * kPulse);
    float heat = 0.18 + 0.3 * kBuild;
    vec3 absorb = nebAbsorption(0.4 + key);

    // The drift of the flat style, kept in x and y only so the z wrap holds:
    // the dust drifts faster than the gas, as its nearer layer did.
    vec2 travel = vec2(0.9, 0.3) * (flow * 0.012 + t * 0.006);
    gGasDrift = vec3(travel * 0.6, 0.0);
    gDustDrift = vec3(travel + vec2(7.3, -2.1), 0.0);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);

    vec3 col = vec3(0.0);
    vec3 trans = vec3(1.0);
    float tt = 0.1 + nebDither() * 0.12;
    for (int i = 0; i < NEB_VOL_MAX; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        if (tt > DU_FAR) break;
        NebHit at = nebField(ro + rd * tt, oct);
        float dt = nebVolDt(tt, 0.12, 0.035);

        // Emission: the crests of the gas field, two hues by height.
        float glow = at.glow;
        float hue = 0.12 + 0.55 * at.gas + key + 0.06 * sin(at.z * DU_Z_FREQ + 0.7);
        vec3 tint = nebRich(nebEmission(hue, heat * glow), 0.9 * bass);
        vec3 emit = tint * glow * gain * exp(-tt * 0.035);

        // Dust: the field's sheets and globules, faded in past the eye.
        float tau = at.dens;
        tau *= smoothstep(0.3, 1.6, tt);
        // Dust faintly scatters the gas light back, reddened: lanes are brown, not holes.
        emit += tint * absorb.zyx * 0.012 * tau * glow;

        vec3 sigma = absorb * (tau * dustK) + 0.12 * glow;
        col += trans * emit * dt;
        trans *= exp(-sigma * dt);
        if (max(trans.r, max(trans.g, trans.b)) < NEB_TRANS_MIN) break;
        tt += dt;
    }

    // Beyond the cloud: a faint haze in the palette, and stars the emission veils.
    float veil = clamp(dot(col, vec3(0.3, 0.5, 0.2)) * 2.0, 0.0, 1.0);
    vec3 bg = pal(0.62 + key) * 0.035;
    bg += nebStars(nebSky(rd), 7.0, 3, vec2(0.0), 1.0, 0.22, hats, t) * (1.0 - 0.6 * veil);
    col += trans * bg;

    col *= 0.85 + 0.15 * swell;
    col *= 0.8 + 0.2 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
