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

// Nebula Pulsar: a young neutron star at the heart of its wind nebula, now a
// body in space you circle. Twin jets of blue synchrotron light shoot out
// along its spin axis as real cones of glowing gas, knotted and widening as
// they go; a glowing torus of hot, dusty gas girdles the star at right angles
// to them, its inner rim lit by the star and its far side shadowed by its own
// dust; a faint cage of warm filament sheets fills the sphere around; the star
// itself is a hard white point with a halo.
//
// Volume-marched, like the family's other 3D clouds (nebVolEmit, front to
// back). The spin axis leans 0.42 rad off +y and precesses a few degrees over
// a minute or so - motion inside the scene. The torus is built in the jets'
// own frame (distance to the axis, distance along it), so it always sits
// square to them, and its tilt toward the eye now comes from the geometry and
// the orbit, not from a squashed ellipse.
//
// ---- camera -----------------------------------------------------------------
//
// The shared rig, object-centred: camAround at PU_CAM_R, one turn per 1024
// world units of travel (CAM_SPIN_UNIT), so the rig's 8192 wrap is a whole
// number of turns and never shows. Nothing in the world depends on z or on
// travel, so there is no other wrap to fit. The dolly can pull the eye in to
// 0.65 * PU_CAM_R = 2.47, outside the PU_BOUND sphere the march runs in, so
// the eye never starts inside the gas and never meets the star.
//
// ---- audio ------------------------------------------------------------------
//
// As in the flat style: each beat (SND_KICK_ENV) FIRES A PULSE UP BOTH JETS:
// a bright knot leaves the star at the beat and races outward along the cones
// as the beat's envelope falls, fading as it goes. It brightens only the gas
// of the jet where it is, never the frame. Knots in the jets stream outward on
// uFlowTime, so loud passages run them faster. Bass (sndShaped(SND_LOW))
// thickens the torus. The hats twinkle the field stars. The key turns the
// jets' hue. Overall level follows SND_INTENSITY gently.
//
// ---- macros and knobs -------------------------------------------------------
//
// Speed is already in uTime and uFlowTime (nebTime, nebFlow): the knots'
// streaming and the precession. Complexity: uMorph adds octaves to the torus
// and the cage (nebVolOctaves, at most four). Chaos: uWarp/uTurbulence curl
// the whole nebula through nebWarp3, bending the jets; uTwist via view().
// Colour: jets, torus and filaments from pal(). Energy: uIntensity scales the
// beat pulse and the torus swell (nebEnergy). Space: the rig.
//
// Knobs (uKnob, exactly 0.5 = the tuned look): k0 PULSE the beat knot's
// brightness, k1 TEXTURE the noise scale of the torus and cage, k2 FLOW how
// far the beat's knot races out, k3 TONE the jets' hue, k4 BUILD the torus
// glow, k5 SPACE the jets' reach.
//
// ---- why the march is safe --------------------------------------------------
//
// There is no distance estimate to get wrong: the gas is a participating
// medium sampled at bounded steps (nebVolDt, growing with depth, lengthened
// when Detail is short), inside one ray-sphere bound (nebSphere), in one loop
// bounded by NEB_VOL_MAX and broken on uSteps. The only surface is the star,
// an exact ray-sphere test that ends the march where it is hit. Every density
// is a smooth falloff times noise in 0..1, so no step can blow up.

#define PU_CAM_R 3.8
/** The sphere every piece of gas lives in, world units. */
#define PU_BOUND 1.95
/** The neutron star's radius. */
#define PU_STAR_R 0.035
/** The torus's major radius. */
#define PU_RING_R 0.42

float gT;
float gFlow;
float gOct;
float gTex;
float gReach;
float gPulsePos;
float gPulse;
float gTorusW;
/** The spin axis and two unit vectors across it. */
vec3 gAxis;
vec3 gAcrossU;
vec3 gAcrossV;

/** The torus's smooth envelope at (distance to axis c, distance along it s): no noise, for the light tap. */
float torusShape(float c, float s) {
    vec2 d = vec2(c - PU_RING_R, s * 1.35);
    return exp(-dot(d, d) / (gTorusW * gTorusW));
}

void main() {
    vec2 uv = view();
    nebSetup();
    gT = nebTime();
    gFlow = nebFlow();
    float e = nebEnergy();
    float beat = clamp(SND_KICK_ENV, 0.0, 1.0);
    float bass = clamp(sndShaped(SND_LOW), 0.0, 1.3) * e;
    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float hats = clamp(SND_HAT_ENV, 0.0, 1.0);
    gOct = nebVolOctaves(3.0);
    float key = nebKeyShift(0.1) + 0.12 * (gNebIn.tone - 0.5);
    gTex = 0.8 + 0.4 * gNebIn.texture;
    gReach = 1.7 * (0.8 + 0.4 * gNebIn.space);
    float build = 0.6 + 0.8 * gNebIn.build;

    // The spin axis: leaning off +y, precessing slowly about it.
    float tilt = 0.42 + 0.05 * sin(gT * 0.09);
    float ph = 0.3 + 0.5 * sin(gT * 0.05);
    gAxis = normalize(vec3(sin(tilt) * cos(ph), cos(tilt), sin(tilt) * sin(ph)));
    gAcrossU = normalize(cross(gAxis, vec3(0.0, 0.0, 1.0)));
    gAcrossV = cross(gAxis, gAcrossU);

    // The beat's knot, racing out along both jets as its envelope falls.
    gPulsePos = 0.06 + gReach * (0.55 + 0.5 * gNebIn.flow) * (1.0 - beat);
    gPulse = sqrt(beat) * e * (0.5 + gNebIn.pulse);
    gTorusW = 0.075 + 0.04 * bass;

    vec3 ro;
    vec3 rd;
    camAround(uv, PU_CAM_R, CAM_SPIN_UNIT, ro, rd);

    vec3 jetCol = nebEmission(0.58 + key, 0.45);
    vec3 torusCol = nebEmission(0.08 + key, 0.35);
    vec3 cageCol = nebEmission(0.95 + key, 0.1);

    // The star: an exact hit that ends the march there.
    vec2 hs = nebSphere(ro, rd, vec3(0.0), PU_STAR_R);
    bool starHit = hs.y > 0.0;

    vec4 acc = vec4(0.0, 0.0, 0.0, 1.0);
    vec2 tb = nebSphere(ro, rd, vec3(0.0), PU_BOUND);
    if (tb.y > 0.0) {
        float tEnd = starHit ? min(tb.y, hs.x) : tb.y;
        float tt = tb.x + nebDither() * 0.03;
        for (int i = 0; i < NEB_VOL_MAX; i++) {
            if (float(i) >= uSteps) break;
            if (tt > tEnd) break;
            vec3 p = ro + rd * tt;
            float dt = nebVolDt(tt - tb.x, 0.024, 0.014);
            vec3 q = nebWarp3(p, gT);
            float r2 = dot(q, q);

            // The jets' frame: s along the axis, c across it.
            float s = dot(q, gAxis);
            float sa = abs(s);
            vec3 across = q - gAxis * s;
            float c = length(across);
            // Near the narrow jets, take finer steps so the cones do not alias.
            dt *= mix(0.55, 1.0, smoothstep(0.0, 0.2, c));

            vec3 emit = vec3(0.0);
            float sigma = 0.0;

            // Jets: a cone, knotted, fading with distance from the star.
            float width = 0.03 + 0.085 * sa;
            float core = exp(-(c * c) / (width * width));
            float reachFade = 1.0 - smoothstep(0.7 * gReach, gReach, sa);
            float jetEnv = core * exp(-sa * 1.5) * smoothstep(0.03, 0.09, sa) * reachFade;
            if (jetEnv > 0.004) {
                float side = step(0.0, s) * 5.0;
                float knots = nebValue3(vec3(sa * 7.0 - gFlow * 0.6 - gT * 0.15,
                                             dot(across, gAcrossU) / width * 0.8,
                                             dot(across, gAcrossV) / width * 0.8 + side));
                float pulse = exp(-pow((sa - gPulsePos) / 0.08, 2.0)) * gPulse * reachFade;
                emit += jetCol * jetEnv * (0.5 + 0.9 * knots) * (0.95 + 1.8 * pulse) * 9.0;
                // The knot's wider glow just around the cone.
                emit += jetCol * pulse * exp(-(c * c) / (9.0 * width * width)) * 1.4;
            }

            // The torus, square to the jets, lit from the star at its centre.
            float tor = torusShape(c, s);
            if (tor > 0.01) {
                float tex = nebFbm3(q * (4.5 * gTex) + vec3(gT * 0.02, 0.0, 0.0), gOct);
                float dens = tor * (0.45 + 0.9 * tex);
                // One tap toward the star: the dust between it and this sample.
                vec3 inward = q - normalize(q) * 0.09;
                float sIn = dot(inward, gAxis);
                float lit = nebLightTap(torusShape(length(inward - gAxis * sIn), sIn), 1.6);
                emit += torusCol * dens * (0.35 + 0.9 * lit) * build * 5.0;
                sigma += dens * 3.0;
            }

            // The inner wind nebula: a soft glow round the star.
            emit += torusCol * exp(-r2 * 7.0) * 0.45;

            // The filament cage: thin sheets of warm gas, faint, round the star.
            float cageMask = exp(-sqrt(r2) * 1.6) * (1.0 - smoothstep(1.5, PU_BOUND, sqrt(r2)));
            if (cageMask > 0.02) {
                float n = nebFbm3(q * (1.9 * gTex) + vec3(3.1, 7.7, 1.3), min(gOct, 3.0));
                float sheet = pow(max(1.0 - abs(2.0 * n - 1.0) * 3.0, 0.0), 2.0);
                emit += cageCol * sheet * cageMask * 0.55;
            }

            nebVolEmit(acc, emit, sigma, dt);
            if (acc.a < NEB_TRANS_MIN) break;
            tt += dt;
        }
    }

    // The neutron star: the hard point, then its halo by the ray's closest
    // approach to the centre (seen through whatever gas is in front).
    float tc = max(-dot(ro, rd), 0.0);
    float dc = length(ro + rd * tc);
    vec3 star = vec3(0.9, 0.95, 1.0);
    vec3 col = acc.rgb;
    if (starHit) col += acc.a * star * 1.6;
    col += acc.a * star * (exp(-dc * dc * 1400.0) * 1.2 + exp(-dc * 16.0) * 0.16);

    vec3 bg = pal(0.68 + key) * 0.012 + nebStars(nebSky(rd), 8.0, 2, vec2(0.0), 1.0, 0.2, hats, gT);
    col += acc.a * bg * (starHit ? 0.0 : 1.0);
    col *= 0.85 + 0.15 * swell;
    col *= 0.75 + 0.25 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
