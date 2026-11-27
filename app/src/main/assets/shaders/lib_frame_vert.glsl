//#include lib_quad
//#include lib_frame

uniform float uTime;
flat out vec4 vFrameA;
#if FRAME_SLOTS > 1
flat out vec4 vFrameB;
#endif
#if FRAME_SLOTS > 2
flat out vec4 vFrameC;
#endif

void framePublish(in FrameOut o) {
    vFrameA = o.a;
#if FRAME_SLOTS > 1
    vFrameB = o.b;
#endif
#if FRAME_SLOTS > 2
    vFrameC = o.c;
#endif
}

void frameMain(in FrameIn f) {
    FrameOut o;
    frameDerive(f, o);
    framePublish(o);
}