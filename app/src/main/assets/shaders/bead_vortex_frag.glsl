#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_palette
//#include lib_scene_grade
//#include lib_touch

// Bead Vortex: looking down a tunnel whose walls are strands of segmented
// beads - red, chartreuse and violet - winding clockwise into a white-hot
// core through a purple haze of drifting motes. Two layers of strands, the
// nearer one fatter and brighter, so the walls read as woven rather than
// tiled. uv -> (angle, log depth) tunnel space -> strands -> haze -> core.
// The tube shading is a fake: a normal built from the strand's cross-section
// and the bead's slope, lit once, which is what makes the beads look ribbed
// without a raymarch.

#define BV_TAU 6.2831853

float hash11(float x) { return fract(sin(x * 127.1) * 43758.5453); }

struct Strand {
    vec3 col;
    float cover;
};

// One layer of strands in tunnel space: t.x runs around the tunnel (0..1),
// t.y runs into it. Each strand is a tube whose radius swells once per bead.
Strand strands(vec2 t, float count, float beadPulse, vec3 light) {
    float u = t.x * count;
    float id = floor(u);
    float x = fract(u) - 0.5;
    float seed = hash11(id + 1.0);
    float w = 0.30 + 0.12 * hash11(id + 7.0);
    float bead = fract(t.y * 3.2 + seed * 3.0);
    float bulge = 0.5 + 0.5 * cos(BV_TAU * bead);
    float wr = w * (0.62 + 0.38 * bulge) * (1.0 + 0.15 * beadPulse);
    float q = x / wr;
    float inside = 1.0 - q * q;
    if (inside <= 0.0) return Strand(vec3(0.0), 0.0);
    float cover = smoothstep(0.0, 0.1, inside);
    // Normal of a tube whose radius changes along its length, with a second
    // row of bumps across its width so each bead reads as a kernel on a cob.
    float cob = 0.25 * sin(q * 9.0) * (1.0 - abs(q));
    vec3 n = normalize(vec3(q + cob, -0.6 * sin(BV_TAU * bead) * (1.0 - abs(q)), sqrt(inside)));
    float diff = max(dot(n, light), 0.0);
    float spec = pow(max(dot(n, normalize(light + vec3(0.0, 0.0, 1.0))), 0.0), 18.0);
    // Crimson, olive-chartreuse or violet, each toned down toward the shade
    // it sits in: the reference is lit from the core, not from the viewer.
    float pick = floor(seed * 3.0);
    vec3 col = pick < 1.0 ? pal(0.0) * vec3(0.85, 0.45, 0.5) : (pick < 2.0 ? pal(0.75) * vec3(0.85, 0.8, 0.35) : pal(0.20) * 0.9);
    col *= 0.15 + 0.8 * diff;
    col += vec3(0.5) * spec * 0.35;
    col *= 0.45 + 0.55 * bulge;  // dark necks between beads
    return Strand(col, cover);
}

float disc(vec2 g, float radius) { return smoothstep(radius, radius * 0.4, length(g)); }

void main() {
    // Advected before anything is measured off it, so the whole vortex leans the way
    // the last spike aimed rather than sitting dead centre.
    vec2 uv = fluidWarp(view(), 1.0, 0.06);
    float r = max(length(uv), 1e-4);
    float a = atan(uv.y, uv.x);
    float energy = clamp(uEnergySmooth, 0.0, 1.5);
    float beat = clamp(uSpike * uBeatResponse, 0.0, 1.0);
    float beadPulse = clamp(uBassSmooth * uBeatResponse, 0.0, 1.5) + touchFalloff(uv, 0.5);
    // Was `uTime * (0.9 + 0.5 * energy)`: the whole strand length is a function of
    // travel, so a change in the rate slid every bead down the tunnel at once. The
    // integrated phase changes speed without moving anything.
    float travel = uFlowPhase * 4.0 + uTime * 0.9;
    // 1/r depth, on a log scale so a bead near the rim and one near the core
    // shrink with perspective instead of stretching.
    float depth = log(0.32 / r);
    float spin = travel * 0.12;
    vec3 light = normalize(vec3(0.35, 0.55, 0.75));

    vec2 front = vec2(fract((a + 1.1 * depth + spin) / BV_TAU), depth * 3.0 + travel * 0.8);
    vec2 back = vec2(fract((a - 0.8 * depth - spin * 0.6) / BV_TAU + 0.37), depth * 4.2 + travel * 1.1);
    Strand f = strands(front, 24.0, beadPulse, light);
    Strand b = strands(back, 36.0, beadPulse, light);
    b.col *= 0.6;

    // The haze fills the far end; motes drift through it toward the viewer.
    float hazeAmt = smoothstep(0.78, 0.12, r);
    float rings = 0.5 + 0.5 * sin(depth * 12.0 - travel * 3.0);
    vec3 haze = pal(0.24) * (0.24 + 0.28 * rings) + pal(0.36) * 0.2;
    vec2 mote = vec2(a / BV_TAU * 48.0, depth * 9.0 + travel * 2.0);
    float moteSeed = hash11(floor(mote.x) + 31.0 * floor(mote.y));
    float motes = disc(fract(mote) - 0.5, 0.2) * step(0.45, moteSeed);
    haze += mix(pal(0.5), pal(0.88), step(0.8, moteSeed)) * motes * 1.2 * hazeAmt;

    vec3 gap = mix(vec3(0.01, 0.0, 0.02), haze, hazeAmt);
    vec3 col = mix(gap, b.col, b.cover);
    col = mix(col, f.col, f.cover);
    col = mix(col, haze, hazeAmt * 0.7);

    // The core: a small white-hot sun ringed like a sunflower, with a warm
    // halo that flares on a hit.
    float core = exp(-r * r * 260.0);
    float sunRings = 0.6 + 0.4 * sin(r * 110.0 - travel * 2.0);
    float halo = exp(-r * 11.0) * sunRings;
    col += vec3(1.0, 0.96, 0.75) * core * 2.6 + vec3(1.0, 0.82, 0.4) * halo * (0.5 + 0.5 * beat);
    col += vec3(1.0, 0.9, 0.7) * fluidMotes(uv * 1.1, 6.0, 0.14) * 0.25 * hazeAmt;
    fragColor = vec4(grade(col), 1.0);
}
