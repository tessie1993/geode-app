#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_palette
//#include lib_scene_grade

// uKnob[6], the six style knobs read below, is declared in lib_scene_uniforms.

// Glow Vortex: a fractal-flame-like web of thin, glassy neon spirals winding
// into several spiral eyes, the largest at the centre, over a violet swirl.
//
// ---- knobs ------------------------------------------------------------------
//
//   k0  core flare        PULSE    the eyes and line cores flash white-hot
//   k1  filigree density  TEXTURE  more arms per spiral, a third line family
//   k2  swirl             FLOW     tighter winding, and the web drifts inward
//   k3  hue drift         TONE     rotates the neon ramp along the spirals
//   k4  depth             BUILD    zooms into the eye; outer eyes join in
//   k5  spread            SPACE    pushes the secondary eyes apart
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
    return mix(c, pal(t), 0.3);
}

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
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

    float kPulse = clamp(uKnob[0], 0.0, 1.0);
    float kTex = clamp(uKnob[1], 0.0, 1.0);
    float kFlow = clamp(uKnob[2], 0.0, 1.0);
    float kTone = clamp(uKnob[3], 0.0, 1.0);
    float kBuild = clamp(uKnob[4], 0.0, 1.0);
    float kSpace = clamp(uKnob[5], 0.0, 1.0);

    // Depth: zoom toward the central eye.
    float zoom = mix(0.85, 0.42, kBuild);
    vec2 z = uv * zoom;
    float zScale = zoom;

    vec3 col = vec3(0.0);
    float eyes = 0.0;
    float t = uTime * 0.06;

    for (int i = 0; i < GV_PAIRS; i++) {
        float fi = float(i);
        // Fixed points: pair 0 is the central eye (b far away, so it is a plain
        // log spiral); the others orbit slowly, spread by SPACE.
        float spread = mix(0.55, 1.05, kSpace) * (0.5 + 0.2 * fi);
        float oa = t * (0.7 + 0.2 * fi) + fi * 2.1;
        vec2 a = i == 0 ? vec2(0.0) : spread * vec2(cos(oa), sin(oa) * 1.4);
        vec2 b = i == 0 ? vec2(0.0, 40.0) : a * -0.35 + 0.25 * vec2(sin(oa * 1.3), cos(oa * 0.7));
        vec2 za = z - a;
        vec2 zb = z - b;
        vec2 ratio = cdiv(za, zb);
        vec2 w = vec2(0.5 * log(max(dot(ratio, ratio), 1e-12)), atan(ratio.y, ratio.x));
        // |dw/dz| in uv units (times the zoom).
        float grad = length(a - b) / max(length(za) * length(zb), 1e-6) * zScale;

        float arms = floor(mix(9.0, 20.0, kTex)) + fi * 3.0;
        float twist = mix(0.55, 1.6, kFlow) * (i == 0 ? 1.0 : 0.7);
        float flow = uTime * (0.25 + 0.06 * fi) + kFlow * 1.2;
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
            l3 = lines(w.x * f3 - flow * 1.5, grad * f3) * (0.35 + 0.65 * kTex);
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
        vec3 c3 = neon(hue + 0.6);
        vec3 c = c1 * (l1.x * 0.9 + l1.y * 0.22) * vary + c2 * (l2.x * 0.7 + l2.y * 0.18) * vary2 + c3 * (l3.x * 0.5 + l3.y * 0.12);
        // A few broad glass ribbons ride the first family at a coarser pitch.
        float nr = max(floor(arms / 3.0), 1.0);
        float rib = ribbon((u * nr + w.y * nr) / GV_TAU + 0.17 * fi, g1 * nr / arms, 0.22) * vary;
        c += neon(hue + 0.15) * rib * 0.55;
        col += c * near * (i == 0 ? 0.7 : 0.8);
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

    // A touch more saturation before the curve: overlaps should stay neon, not white.
    col = max(mix(vec3(dot(col, vec3(0.299, 0.587, 0.114))), col, 1.3), 0.0);
    col = aces(col * 1.2);
    fragColor = vec4(grade(col), 1.0);
}
