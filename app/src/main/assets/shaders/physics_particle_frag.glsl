#version 300 es
// One physics-layer particle (physics_particle_vert): a soft disc, drawn with a screen blend (GL_ONE,
// GL_ONE_MINUS_SRC_COLOR), PhysicsPlan's front(), so however many pile on one pixel the frame stops at white.
precision highp float;
in vec3 vColour;
in float vFade;
uniform float uMix;  // the particle layer's mix, 0..1
out vec4 fragColor;
void main() {
    vec2 q = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(q, q);
    if (r2 > 1.0 || vFade <= 0.0) { discard; }
    float body = 1.0 - r2;
    body *= body;
    fragColor = vec4(clamp(vColour * (body * vFade * clamp(uMix, 0.0, 1.0)), 0.0, 1.0), 1.0);
}
