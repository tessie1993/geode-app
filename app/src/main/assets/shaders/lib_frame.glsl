// What changes once a frame, worked out once a frame. A style's own <id>_vert.glsl includes this, computes its
// per-frame values in main() from uniforms and uTime alone, and hands them to the fragment as `flat` varyings:
// the vertex stage runs three times a draw, the fragment stage once a pixel (and, in a march, once a step).
// Only values that hold for one frame belong here; a value that holds for a style's whole life is a GLSL const.

out vec2 vUv;

void quadVertex() {
    vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}

/** k whole cycles per uTime wrap (7100 s, ShaderScene): the rate every family's *_RATE(k) spells. */
#define FRAME_RATE(k) (6.2831853 * float(k) / 7100.0)

/** mat2(frameTurn(t, rate)) turns by t * rate; .x is its cosine and .z its sine. */
vec4 frameTurn(float t, float rate) {
    float a = t * rate;
    float c = cos(a);
    float s = sin(a);
    return vec4(c, -s, s, c);
}
