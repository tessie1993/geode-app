//#include lib_frame

flat in vec4 vFrameA;
#if FRAME_SLOTS > 1
flat in vec4 vFrameB;
#endif
#if FRAME_SLOTS > 2
flat in vec4 vFrameC;
#endif

/** The owner's published FrameOut (lib_frame_vert): a read of the varyings the style declared, nothing derived here. */
#if FRAME_SLOTS == 1
#define gFrame FrameOut(vFrameA, vec4(0.0), vec4(0.0))
#elif FRAME_SLOTS == 2
#define gFrame FrameOut(vFrameA, vFrameB, vec4(0.0))
#else
#define gFrame FrameOut(vFrameA, vFrameB, vFrameC)
#endif

void frameTurnXZ(in vec4 m, inout vec3 p) {
    p.xz = mat2(m) * p.xz;
}

void frameTurnYZ(in vec4 m, inout vec3 p) {
    p.yz = mat2(m) * p.yz;
}

void frameTurnXY(in vec4 m, inout vec3 p) {
    p.xy = mat2(m) * p.xy;
}