#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// The shape controls are spent on the web itself (lib_neon), not on the plane.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_palette
//#include lib_scene_grade
//#include lib_neon

// Glow Vortex: a fractal-flame-like web of thin, glassy neon spirals winding
// into several spiral eyes, the largest at the centre, over a violet swirl.
//
// ---- what moves it -----------------------------------------------------------
//
// The style was drawn around six looks (core flare, filigree, swirl, hue
// drift, depth, spread); each is now driven by the shared controls (lib_neon)
// or the sound, so Customize, the macros, the director and the DNA all reach
// it:
//
// - core flare: SND_KICK_ENV flashes the eyes and the line cores white-hot;
//   SND_SNARE_ENV lights the third line family.
// - filigree (Morph, neonForm): the third line family and the glass
//   ribbons fill in. A hard cut (SND_CUT_COUNT) changes the arm count.
// - swirl (Warp, neonFlow): tighter winding. The web drifts inward on
//   SND_BASS_TIME as well as its own clock, so it pours into the eye faster
//   in a heavy passage and never runs backwards.
// - Turbulence (neonChaos) shakes the secondary eyes off their orbits.
// - hue drift: the key (neonKeyTurn) turns the ramp; SND_CHORD_HUE turns
//   the third family against the other two.
// - depth: SND_BUILD_UP zooms in toward the central eye; the drop
//   (SND_HELD_SURGE) dives in and wakes the outer eyes.
// - spread: the secondary eyes orbit on SND_MID_TIME and spread apart in
//   silence (neonQuiet). A hard cut (SND_CUT_COUNT) re-deals their orbits.
// - SND_HAT_ENV sparkles the ribbons. Intensity (neonGlow) is the emission.
//
// Provenance: written from scratch for MuzViz from general techniques - the
// complex logarithm of a Mobius ratio, w = log((z - a) / (z - b)), which maps
// the plane between two fixed points a and b onto a strip, so straight lines
// slanted across the strip come back as loxodromes (double spirals, as in
// Doyle spirals); line rendering with an ANALYTIC pixel footprint from
// |dw/dz| = |(a - b) / ((z - a)(z - b))|, so the lines stay one pixel thin at
// every scale and never tear on the log's branch cut (fwidth would); and an
// average-coverage fade where the lines crowd below a few pixels apart. No code
// was taken from any published shader.
//
// Each spiral pair contributes two crossing families of arms, so the overlap of
// four pairs gives the dense, translucent, curved lattice of a fractal flame.
// Everything is added, never occluded: lines read as lit glass.

#define GV_TAU 6.2831853
#define GV_PAIRS 6

float gPx;

vec3 neon(float t) {
    // Violet -> electric blue -> cyan -> acid green -> gold -> magenta, with a
    // pull toward the user's palette so the colour controls still move it.
    vec3 c = 0.5 + 0.5 * cos(GV_TAU * (t + vec3(0.60, 0.78, 0.98)));
    c = c * c * c * vec3(1.1, 1.0, 1.35) + vec3(0.02, 0.0, 0.06);
    return neonPal(c, t);
}

vec2 cdiv(vec2 a, vec2 b) {
    return vec2(a.x * b.x + a.y * b.y, a.y * b.x - a.x * b.y) / max(dot(b, b), 1e-9);
}

/**
 * A family of lines x = const (spacing 1) where x's screen gradient is
 * `grad` (units of x per uv unit). Returns (thin core, soft glow), faded to
 * their average where the lines are too close to resolve.
 */
vec2 lines(float x, float grad) {
    float spacingPx = 1.0 / max(grad * gPx, 1e-6);
    float d = abs(fract(x) - 0.5) * spacingPx; // pixels to the nearest line
    float core = exp(-d * d * 0.9);
    float glow = 1.0 / (1.0 + d * 0.35);
    glow *= glow;
    float resolve = smoothstep(1.5, 6.0, spacingPx);
    float avgCore = min(1.6 / spacingPx, 1.0);
    float avgGlow = min(5.0 / spacingPx, 1.0);
    return mix(vec2(avgCore, avgGlow), vec2(core, glow), resolve);
}

/**
 * A glass ribbon centred on the lines x = const: a translucent fill `width`
 * wide (fraction of the spacing) with bright rims, like a lit glass blade
 * seen edge-on. Fades out where it cannot be resolved.
 */
float ribbon(float x, float grad, float width) {
    float spacingPx = 1.0 / max(grad * gPx, 1e-6);
    float f = abs(fract(x) - 0.5);
    float halfW = width * 0.5;
    float rimPx = abs(f - halfW) * spacingPx;
    float rim = exp(-rimPx * rimPx * 0.6) + 0.25 / (1.0 + rimPx * 0.5);
    float fill = smoothstep(halfW + 0.5 / spacingPx, halfW - 0.5 / spacingPx, f) * (0.18 + 0.25 * f / max(halfW, 1e-3));
    return (rim + fill) * smoothstep(4.0, 14.0, spacingPx * width);
}

void main() {
    vec2 uv = view();
    gPx = 2.0 / uResolution.y;

    // The six looks the web was drawn around, each 0..1 with 0.5 as tuned,
    // now driven: see "what moves it" above.
    float surge = neonSurge();
    float quiet = neonQuiet();
    float glow = neonGlow();
    float kPulse = clamp(0.15 + 0.85 * neonKick() + 0.4 * surge, 0.0, 1.0) * (1.0 - 0.6 * quiet);
    float kTex = mix(0.5, 1.0, neonForm());
    float kFlow = clamp(0.5 + 0.5 * neonFlow(), 0.0, 1.0);
    float kTone = 0.5 + neonKeyTurn() / 0.6;
    float kBuild = clamp(0.4 + 0.5 * neonBuild() + 0.6 * surge, 0.0, 1.0);
    float kSpace = clamp(0.5 + 0.4 * quiet - 0.2 * surge, 0.0, 1.0);
    // Note & key reactions' style knobs ride on top (lib_neon).
    kPulse = clamp(kPulse + neonKnob(0), 0.0, 1.0);
    kTex = clamp(kTex + neonKnob(1), 0.0, 1.0);
    kFlow = clamp(kFlow + neonKnob(2), 0.0, 1.0);
    kBuild = clamp(kBuild + neonKnob(4), 0.0, 1.5);
    kSpace = clamp(kSpace + neonKnob(5), 0.0, 1.0);
    kTone += neonKnob(3);
    float chaos = neonChaos();
    float snare = sndShaped(clamp(SND_SNARE_ENV, 0.0, 1.0)) * (1.0 - quiet);
    float deal = floor(SND_CUT_COUNT + 0.5) * 1.7;

    // Depth: zoom toward the central eye.
    float zoom = mix(0.85, 0.42, kBuild);
    vec2 z = uv * zoom;
    float zScale = zoom;

    vec3 col = vec3(0.0);
    float eyes = 0.0;
    float t = uTime * 0.03 + SND_MID_TIME * 0.05 + deal;

    for (int i = 0; i < GV_PAIRS; i++) {
        float fi = float(i);
        // Fixed points: pair 0 is the central eye (b far away, so it is a plain
        // log spiral); the others orbit slowly, spread by SPACE.
        float spread = mix(0.55, 1.05, kSpace) * (0.5 + 0.2 * fi);
        float oa = t * (0.7 + 0.2 * fi) + fi * 2.1;
        vec2 a = i == 0 ? vec2(0.0) : spread * vec2(cos(oa), sin(oa) * 1.4);
        if (i > 0) a += chaos * 0.12 * vec2(sin(uTime * 2.3 + fi * 4.1), cos(uTime * 1.9 + fi * 2.7));
        vec2 b = i == 0 ? vec2(0.0, 40.0) : a * -0.35 + 0.25 * vec2(sin(oa * 1.3), cos(oa * 0.7));
        vec2 za = z - a;
        vec2 zb = z - b;
        vec2 ratio = cdiv(za, zb);
        vec2 w = vec2(0.5 * log(max(dot(ratio, ratio), 1e-12)), atan(ratio.y, ratio.x));
        // |dw/dz| in uv units (times the zoom).
        float grad = length(a - b) / max(length(za) * length(zb), 1e-6) * zScale;

        // A count, so it changes only on a hard cut (lib_neon: never a count on Morph).
        float arms = 12.0 + 3.0 * mod(floor(SND_CUT_COUNT + 0.5), 3.0) + fi * 3.0;
        float twist = mix(0.55, 1.6, kFlow) * (i == 0 ? 1.0 : 0.7);
        float flow = (uTime * 0.12 + SND_BASS_TIME * 0.3) * (1.0 + 0.24 * fi) + kFlow * 1.2;
        // Two crossing loxodrome families; whole arms per turn keep both
        // continuous across the branch cut of atan.
        float u = w.x * twist - flow;
        float n2 = floor(arms * 0.62 + 0.5);
        float x1 = (u * arms + w.y * arms) / GV_TAU;
        float x2 = (u * arms * 0.62 - w.y * n2) / GV_TAU;
        float g1 = grad * arms * length(vec2(twist, 1.0)) / GV_TAU;
        float g2 = grad * length(vec2(twist * arms * 0.62, n2)) / GV_TAU;
        vec2 l1 = lines(x1, g1);
        vec2 l2 = lines(x2, g2);
        vec2 l3 = vec2(0.0);
        {
            float f3 = arms * 1.5 / GV_TAU;
            l3 = lines(w.x * f3 - flow * 1.5, grad * f3) * (0.35 + 0.65 * kTex + 1.2 * snare);
        }

        // Flame-like variation along the arms: some stretches burn bright,
        // others fade to a ghost, slowly crawling.
        float vary = 0.35 + 0.65 * pow(0.5 + 0.5 * sin(w.y * 3.0 + w.x * 2.3 + uTime * 0.4 + fi * 1.7), 2.0);
        float vary2 = 0.35 + 0.65 * pow(0.5 + 0.5 * sin(-w.y * 2.0 + w.x * 3.1 - uTime * 0.3 + fi), 2.0);

        // Each pair fades with distance from its own eye, so the secondary
        // spirals read as separate vortices inside the main one.
        float near = i == 0 ? 1.0 : exp(-length(za) * mix(2.2, 1.2, kBuild)) * 1.4;
        float hue = 0.12 * fi + 0.07 * w.x + kTone * 0.6 + 0.02 * uTime;
        vec3 c1 = neon(hue);
        vec3 c2 = neon(hue + 0.33);
        vec3 c3 = neon(hue + 0.6 + 0.4 * SND_CHORD_HUE);
        vec3 c = c1 * (l1.x * 0.9 + l1.y * 0.22) * vary + c2 * (l2.x * 0.7 + l2.y * 0.18) * vary2 + c3 * (l3.x * 0.5 + l3.y * 0.12);
        // A few broad glass ribbons ride the first family at a coarser pitch.
        float nr = max(floor(arms / 3.0), 1.0);
        float rib = ribbon((u * nr + w.y * nr) / GV_TAU + 0.17 * fi, g1 * nr / arms, 0.22) * vary;
        c += neon(hue + 0.15) * rib * (0.55 + 0.8 * neonSparkle());
        col += c * near * (i == 0 ? 0.7 : 0.8) * glow;
        eyes += exp(-length(za) * 16.0 / zoom) * (i == 0 ? 1.0 : 0.5 * near);
    }

    // The violet swirl behind the web: soft bands in log-polar space.
    float r = max(length(z), 1e-3);
    float ang = atan(z.y, z.x);
    float swirl = 0.5 + 0.5 * sin(ang * 3.0 + log(r) * 4.0 - uTime * 0.2);
    swirl *= 0.5 + 0.5 * sin(ang * 5.0 - log(r) * 2.0 + uTime * 0.13);
    col += mix(vec3(0.035, 0.01, 0.08), vec3(0.0, 0.05, 0.06), swirl) * (0.6 + 0.8 * swirl);

    // The eyes, white-hot on the pulse.
    col += mix(neon(0.55 + kTone * 0.6), vec3(1.0, 0.95, 0.85), 0.4 + 0.4 * kPulse) * eyes * (0.15 + 1.0 * kPulse);
    col *= 0.9 + 0.6 * kPulse;

    fragColor = vec4(neonFinish(col, 1.2), 1.0);
}
