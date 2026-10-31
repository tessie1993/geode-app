#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_palette
//#include lib_scene_grade
//#include lib_nebula

// Nebula Pulsar: a young neutron star at the heart of its wind nebula. Twin
// jets of blue synchrotron light shoot out along its spin axis, knotted and
// widening as they go; a glowing tilted torus girdles the star at right
// angles to them; a faint cage of warm filaments fills the space around; the
// star itself is a hard white point.
//
// The jet axis is fixed in the frame, with a precession of a few degrees over
// a minute or so - motion inside the scene, never a turn of the frame. The
// torus is an ellipse in the jets' own frame (its major axis across the jets,
// squashed by the tilt), so it always sits square to them.
//
// ---- audio ------------------------------------------------------------------
//
// Each beat (SND_KICK_ENV) FIRES A PULSE UP BOTH JETS: a bright knot leaves the
// star at the beat and races outward along the jets as the beat's envelope
// falls, fading as it goes. It brightens only the jet where it is. Knots in
// the jets stream outward on uFlowPhase, so loud passages run them faster.
// Bass (sndShaped(SND_LOW)) thickens the torus. The hats twinkle the field stars.
// The key turns the jets' hue. Overall level follows SND_INTENSITY gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the knots' streaming and the precession. Complexity:
// uMorph adds octaves to the torus and the filament cage (nebOctaves). Chaos:
// uWarp/uTurbulence curl the whole nebula (nebWarp2), bending the jets;
// uTwist via view(). Colour: jets, torus and filaments from pal(). Energy:
// uIntensity scales the beat pulse and the torus swell. Space: view().

void main() {
    vec2 uv = view();
    float t = nebTime();
    float flow = nebFlow();
    float e = nebEnergy();
    float beat = clamp(SND_KICK_ENV, 0.0, 1.0);
    float bass = clamp(sndShaped(SND_LOW), 0.0, 1.3) * e;
    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float hats = clamp(SND_HAT_ENV, 0.0, 1.0);
    float oct = nebOctaves(3.0);
    float key = nebKeyShift(0.1);

    vec2 p = nebWarp2(uv, t);
    float r = length(p);

    // The jets' frame: s along the axis, c across it.
    float tilt = 0.42 + 0.06 * sin(t * 0.09);
    vec2 axis = vec2(sin(tilt), cos(tilt));
    vec2 perp = vec2(axis.y, -axis.x);
    float s = dot(p, axis);
    float c = dot(p, perp);
    float sa = abs(s);

    // Jets: a narrow cone, knotted, fading with distance.
    float width = 0.016 + 0.085 * sa;
    float core = exp(-(c * c) / (width * width));
    float knots = nebValue2(vec2(sa * 7.0 - flow * 0.6 - t * 0.15, c / width * 0.8 + step(0.0, s) * 5.0));
    float jet = core * exp(-sa * 1.5) * smoothstep(0.0, 0.05, sa) * (0.5 + 0.9 * knots);

    // The beat's pulse, racing out along both jets as its envelope falls.
    float pos = 0.05 + 1.25 * (1.0 - beat);
    float pulse = exp(-pow((sa - pos) / 0.07, 2.0)) * sqrt(beat) * e;

    vec3 jetCol = nebEmission(0.58 + key, 0.45);
    vec3 col = jetCol * jet * (0.95 + 1.8 * pulse);
    col += jetCol * pulse * exp(-(c * c) / (9.0 * width * width)) * 0.35;

    // The torus, square to the jets and tilted toward us.
    float tex = nebFbm2(p * 5.0 + vec2(t * 0.02, 0.0), oct);
    float er = length(vec2(c / 0.33, s / 0.1));
    float tw = 0.2 + 0.1 * bass;
    float torus = exp(-pow((er - 1.0) / tw, 2.0)) * (0.5 + 0.8 * tex);
    col += nebEmission(0.08 + key, 0.35) * (torus * 0.75 + exp(-er * 2.2) * 0.22);

    // The filament cage.
    float cage = nebFbm2(p * 2.4 + vec2(3.1, 7.7), oct);
    float cageMask = exp(-r * 1.6);
    col += nebEmission(0.95 + key, 0.1) * pow(smoothstep(0.45, 0.85, cage), 2.0) * cageMask * 0.35;

    // The neutron star.
    col += vec3(0.9, 0.95, 1.0) * (exp(-r * r * 2200.0) * 1.4 + exp(-r * 22.0) * 0.18);

    col += nebStars(uv, 8.0, 2, vec2(0.0), 1.0, 0.2, hats, t) * (1.0 - min(torus, 0.8));
    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
