// The per-frame state, owned by the vertex stage. A style tells the owner what it needs as a FrameIn (the
// clock and its turns) once per draw, and the owner publishes one FrameOut that every fragment module reads
// (lib_frame_frag). Nothing between the two is computed or handed back piecemeal: modules are void functions
// that take a descriptor in and write their result through out or inout.

#ifndef FRAME_SLOTS
#define FRAME_SLOTS 3
#endif

/** A turn's angle is start + time * rate; rate is FRAME_RATE(k), k whole cycles per uTime wrap (7100 s, ShaderScene). */
struct FrameTurn {
    float rate;
    float start;
};

struct FrameIn {
    float time;
    FrameTurn a;
    FrameTurn b;
    FrameTurn c;
};

/** Each turn as the column-major mat2 that turns by its angle: mat2(a), with .x its cosine and .z its sine. */
struct FrameOut {
    vec4 a;
    vec4 b;
    vec4 c;
};

#define FRAME_RATE(k) (6.2831853 * float(k) / 7100.0)
#define FRAME_NONE FrameTurn(0.0, 0.0)

void frameTurnOf(in float time, in FrameTurn turn, out vec4 m) {
    float angle = turn.start + time * turn.rate;
    float c = cos(angle);
    float s = sin(angle);
    m = vec4(c, -s, s, c);
}

void frameDerive(in FrameIn f, out FrameOut o) {
    frameTurnOf(f.time, f.a, o.a);
    o.b = vec4(0.0);
    o.c = vec4(0.0);
#if FRAME_SLOTS > 1
    frameTurnOf(f.time, f.b, o.b);
#endif
#if FRAME_SLOTS > 2
    frameTurnOf(f.time, f.c, o.c);
#endif
}

/** Superformula exponent sets (n1, n2, n3), and the carried per-frame scalars of the style that owns them. */
struct FrameShapeIn {
    vec3 a;
    vec3 b;
    vec3 c;
    vec4 d;
};

/** Each set with the curve's maximum in .w; d as it came. */
struct FrameShapeOut {
    vec4 a;
    vec4 b;
    vec4 c;
    vec4 d;
};

/** The raw superformula at u with a = b = 1: (|cos u|^n2 + |sin u|^n3)^(-1/n1). */
float frameSfRaw(in float u, in vec3 n) {
    float s = pow(max(abs(cos(u)), 1e-4), n.y) + pow(max(abs(sin(u)), 1e-4), n.z);
    return pow(max(s, 1e-5), -1.0 / n.x);
}

/** The curve's maximum over a quarter period, which covers every u. Independent of the lobe count. */
void frameSfOf(in vec3 n, out vec4 o) {
    float mx = 1e-4;
    for (int k = 0; k <= 8; k++) mx = max(mx, frameSfRaw(float(k) * (3.14159265 / 16.0), n));
    o = vec4(n, mx);
}

void frameDerive(in FrameShapeIn s, out FrameShapeOut o) {
    frameSfOf(s.a, o.a);
    frameSfOf(s.b, o.b);
    frameSfOf(s.c, o.c);
    o.d = s.d;
}
