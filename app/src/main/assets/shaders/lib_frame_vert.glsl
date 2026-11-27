//#include lib_quad
//#include lib_frame

#ifndef SCENE_UNIFORMS_DECLARED
uniform float uTime;
#endif
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

#ifdef FRAME_SHAPE
flat out vec4 vShapeA;
flat out vec4 vShapeB;
flat out vec4 vShapeC;
flat out vec4 vShapeD;

void frameMain(in FrameIn f, in FrameShapeIn s) {
    frameMain(f);
    FrameShapeOut o;
    frameDerive(s, o);
    vShapeA = o.a;
    vShapeB = o.b;
    vShapeC = o.c;
    vShapeD = o.d;
}
#endif
