#version 300 es
//#include lib_frame_vert

void main() {
    quadVertex();
    frameMain(FrameIn(uTime, FrameTurn(FRAME_RATE(40), 0.0), FrameTurn(FRAME_RATE(90), 0.0), FRAME_NONE));
}
