//#include lib_frame

flat in vec4 vFrameA;
flat in vec4 vFrameB;
flat in vec4 vFrameC;

/** The owner's published FrameOut (lib_frame_vert): a read of the three varyings, nothing derived here. */
#define gFrame FrameOut(vFrameA, vFrameB, vFrameC)

void frameTurnXZ(in vec4 m, inout vec3 p) {
    p.xz = mat2(m) * p.xz;
}

void frameTurnYZ(in vec4 m, inout vec3 p) {
    p.yz = mat2(m) * p.yz;
}

void frameTurnXY(in vec4 m, inout vec3 p) {
    p.xy = mat2(m) * p.xy;
}
