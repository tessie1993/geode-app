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
//#include lib_chrys

// Chrysanthemum Neon: a flower drawn only in outline, as bent glass tubes in
// the dark. Four nested petal rings all grow from the same small heart, each
// one longer than the last and each in its own colour, so their outlines
// cross and weave into a lattice whose pattern depends entirely on how many
// petals each ring has. Nothing is filled: a tube is a hot core and a wide,
// faint halo, with a little light chasing round every petal's rim.
//
// ---- audio ------------------------------------------------------------------
//
// The song's structure sets the PETAL COUNTS. Each ring's count (5 to 12)
// comes from a hash of SND_CUT_COUNT, so every section of the track has its
// own figure, held until the next. When a section changes, the old figure
// fades out while the new one fades in across the section envelope
// (SND_HELD_HARD_CUT), the rings swirling round as they trade places (chrysBump, so
// the swirl starts from rest). The old figure is the hash of the previous
// count, which is what the screen was already showing, so nothing pops. The
// presence band (sndSpectrum(0.80)) stretches the chasing lights into long trails.
// The rings counter-rotate on uDrift. Brightness follows (0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)) only; the
// crossfade keeps the total light constant.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the counter-rotation, the chasing lights and the hue
// clock. Complexity: uMorph adds up to two more, longer rings (six at most,
// twelve ring evaluations with the crossfade). Chaos: uTurbulence + uWarp
// bend the tubes into hand-made, wobbly glass; uTwist curls every petal into
// a spiral. Energy: uIntensity scales the section swirl and the trail
// stretch. Colour: pal() for every tube (white-hot cores are a fixed accent).
// Space: uZoom via view().

#define NE_MAX_RINGS 6

float neonCount(float section, float ring) {
    return 5.0 + floor(chrysHash(vec2(section * 1.37, ring * 4.1 + 2.0)) * 7.999);
}

float gT;
float gChaos;

// The light of one ring of tubes, at weight `w`.
vec3 neonRing(vec2 p, float count, float k, float trail, float hue, float w) {
    if (w < 0.002) return vec3(0.0);
    float len = 0.28 + 0.19 * k;
    vec4 pet = chrysPetal(p, count, 0.035 + 0.05 * k, len, max(0.5 - 0.06 * k, 0.18), uTwist, 0.25, 0.5);
    float d = abs(pet.x + gChaos * 0.012 * sin(pet.y * 9.0 + pet.w * 2.3 + gT * 0.7));
    vec3 tube = pal(hue + 0.24 * k);
    float core = chrysGlow(d, 0.0032);
    float halo = chrysGlow(d, 0.028);
    vec3 c = tube * (0.75 * core + 0.16 * halo) + vec3(1.0) * 0.35 * core * core;
    // A light chasing round the rim: position along the outline 0..1.
    float u = clamp(pet.y, 0.0, 1.0);
    float along = pet.z > 0.0 ? 0.5 * u : 1.0 - 0.5 * u;
    float id = mod(pet.w, count);
    float head = fract(gT * 0.3 + chrysHash1(id + 9.0 * k));
    float behind = fract(head - along);
    float chase = smoothstep(trail, 0.0, behind) * step(behind, trail);
    c += mix(tube, vec3(1.0), 0.6) * 0.5 * chase * chrysGlow(d, 0.005);
    return c * w;
}

void main() {
    vec2 uv = view();
    float swell = clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    float sectEnv = clamp(SND_HELD_HARD_CUT, 0.0, 1.0);
    gT = uTime * uSpeed;
    gChaos = clamp(uTurbulence + uWarp, 0.0, 2.0);
    float rings = 4.0 + floor(2.0 * clamp(uMorph, 0.0, 1.0) + 0.5);
    float trail = 0.04 + 0.3 * clamp(sndSpectrum(0.80) * uIntensity, 0.0, 1.5);
    float r = length(uv);
    float hue = 0.55 + gT * 0.004;

    vec3 col = vec3(0.004, 0.004, 0.012) + pal(hue + 0.5) * 0.02 * exp(-r * 1.3);
    // Faint guide circles on the backboard.
    col += pal(hue) * 0.012 * smoothstep(0.01, 0.0, abs(fract(r * 6.0) - 0.5) / 6.0 - 0.002);

    float wNew = smoothstep(0.85, 0.1, sectEnv);
    float swirl = chrysBump(sectEnv) * 0.9 * uIntensity;
    for (int i = 0; i < NE_MAX_RINGS; i++) {
        float k = float(i);
        if (k >= rings) break;
        float dir = mod(k, 2.0) < 0.5 ? 1.0 : -1.0;
        vec2 p = chrysTurn(dir * (uDrift * 0.12 + gT * 0.03 + swirl) + k * 0.4) * uv;
        col += neonRing(p, neonCount(SND_CUT_COUNT, k), k, trail, hue, wNew);
        col += neonRing(p, neonCount(SND_CUT_COUNT - 1.0, k), k, trail, hue, 1.0 - wNew);
    }
    // The heart: a small warm bulb.
    col += mix(pal(hue + 0.1), vec3(1.0), 0.5) * 0.5 * chrysGlow(r, 0.012);

    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
