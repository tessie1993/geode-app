// The per-frame state, owned by the vertex stage. A style tells the owner what it needs as a FrameIn (the
// clock and its turns) once per draw, and the owner publishes one FrameOut that every fragment module reads
// (lib_frame_frag). Nothing between the two is computed or handed back piecemeal: modules are void functions
// that take a descriptor in and write their result through out or inout.

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
    frameTurnOf(f.time, f.b, o.b);
    frameTurnOf(f.time, f.c, o.c);
}
