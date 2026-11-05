#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// The shape controls are spent on the world (core/viz/world/MorphDriver), not on the rays.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_world

// Morph World: Geode's own morphing 3D environment. One space that is many worlds: a nave of
// bubbles, a girder yard, plates of light, folded quartz, a furnace of rings, a fractal gas, veils
// of fog, endless arches, a lattice in bloom, a drifting reef, and every world between them. The
// space is lib_world's chain of domain operators; what the chain is set to is the World Genome
// (uWorld), which the C++ driver eases from the world it is in toward the next one the music
// picks: a drop rushes into a new world in a second and a half, a new section may glide into one
// over many seconds, and a calm song may keep its world all song.
//
// The camera is the shared rig (lib_camera, camFly): no path of our own. The world is open in
// every direction around it and the corridor is carved out of the distance field, so a morph never
// closes on the eye.
//
// Three renderings of the same world, crossfaded by the genome's mode weights: a hard surface, a
// glow-through volume and an emissive density. A mode with no weight is never marched; the glow
// and the density take fewer steps than the surface, all from the Detail budget (uSteps).
//
// Controls (all read by the driver, smoothed, summed once and clamped into the genome's bounds):
// Morph is how far each new world mutates from its seed; Warp twists the world along travel;
// Turbulence strengthens the folds; Detail caps the IFS steps here and the march budget; Intensity
// is the emission. The six knobs: k0 PULSE how hard the bass and the kick breathe the folds and
// the light; k1 TEXTURE the fold count and turn; k2 FLOW the speed of every glide; k3 TONE the
// palette's turn; k4 BUILD the emission; k5 SPACE the corridor's width.
//
// Brightness: the genome carries a gain envelope (WG_GAIN) the driver rate-limits, so a retarget
// glides the frame's level instead of stepping it; safety::apply clamps every frame after this.
//
// Provenance: written from scratch for Geode from textbook maths. No code was taken from any
// published shader.

/** Interleaved gradient noise: a per-pixel start offset that hides the volume marches' banding. */
float worldJitter(vec2 frag) { return fract(52.9829189 * fract(dot(frag, vec2(0.06711056, 0.00583715)))); }

void main() {
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);

    float jitter = worldJitter(gl_FragCoord.xy);
    float wS = WG_M_SURFACE;
    float wG = WG_M_GLOW;
    float wD = WG_M_DENSITY;
    vec3 col = vec3(0.0);
    if (wS > 0.01) col += wS * worldSurface(ro, rd, uSteps);
    if (wG > 0.01) col += wG * worldGlow(ro, rd, min(uSteps * 0.5, 56.0), jitter);
    if (wD > 0.01) col += wD * worldDensity(ro, rd, min(uSteps * 0.35, 40.0), jitter);
    // A mode under 1% was skipped; the rest are rescaled so the skip never dims the frame.
    float kept = (wS > 0.01 ? wS : 0.0) + (wG > 0.01 ? wG : 0.0) + (wD > 0.01 ? wD : 0.0);
    col /= max(kept, 0.01);

    col *= WG_GAIN;
    // A soft shoulder, so the glow's piled-up light rolls off instead of clipping.
    col = col / (1.0 + 0.35 * max(col.r, max(col.g, col.b)));
    fragColor = vec4(grade(col), 1.0);
}
