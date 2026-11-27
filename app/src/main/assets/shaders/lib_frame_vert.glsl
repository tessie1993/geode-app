//#include lib_quad
//#include lib_frame

uniform float uTime;
flat out vec4 vFrameA;
flat out vec4 vFrameB;
flat out vec4 vFrameC;

void framePublish(in FrameOut o) {
    vFrameA = o.a;
    vFrameB = o.b;
    vFrameC = o.c;
}

void frameMain(in FrameIn f) {
    FrameOut o;
    frameDerive(f, o);
    framePublish(o);
}
