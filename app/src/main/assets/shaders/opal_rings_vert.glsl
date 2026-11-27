#version 300 es
#define FRAME_SLOTS 1
//#include lib_frame_vert

void main() {
    quadVertex();
    frameMain(FrameIn(uTime, FrameTurn(FRAME_RATE(80), 0.0), FRAME_NONE, FRAME_NONE));
}
