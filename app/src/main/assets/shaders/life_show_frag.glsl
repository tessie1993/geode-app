#version 300 es
#define VIEW_OWNS_SHAPE
#define VIEW_BENDS_IN_3D
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_neon
//#include lib_field3d

// LIFE - the present. The state is a field of living matter (Lenia's A or
// Gray-Scott's v), an age trace and the melody ink each cell was born with;
// core/viz/present/FieldPresent writes it into uField (r the matter as a
// height, g the ink, b the age, a young matter) and this draws it in 3D on
// the camera rig through lib_field3d: seven styles fly over it as a living
// terrain, Gyre circles it as a torus, Pulsar and Mitosis as a sphere
// (LifeScene's look table). The matter is the relief, and finer copies of
// the field itself are its fractal detail, so every ridge is made of the
// organism. Young matter wears the style's first material and old matter
// (the soma, by the age trace) its second.
//
// Colour: the palette at the style's position, moved by the melody ink, so
// colonies carry the colour of the line that grew them, by the age and by the
// height; new births glow at the palette's far side (BUILD). TONE moves the
// position, Intensity and PULSE the emission (lib_field3d). Nothing here hears
// a hit: the lows lift the relief and the mids turn it, through lib_field3d.
//
// Cost: lib_field3d's march (up to 110 steps of up to three field reads) and
// five distances at the hit.

/** The matter's colour where the eye lands: the palette by its ink, its age and its height. */
vec3 fieldColour(vec4 f) {
    float t = gFieldTone + uFieldLook[4].y * (0.38 * f.g + 0.1 * f.b + 0.08 * f.r);
    return pal(t) * mix(0.35, 1.0, f.r);
}

/** New births glow at the palette's far side from the matter round them. */
vec3 fieldGlow(vec4 f) {
    return pal(gFieldTone + 0.45 + uFieldLook[4].y * 0.38 * f.g) * 0.6;
}

//#include lib_field3d_march

void main() {
    vec2 uv = view();
    fieldSetup();
    vec3 ro;
    vec3 rd;
    fieldEye(uv, ro, rd);
    fragColor = fieldFinish(fieldTrace(ro, rd), 1.0);
}
