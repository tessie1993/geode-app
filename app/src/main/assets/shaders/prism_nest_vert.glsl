#version 300 es
//#include lib_frame_vert

void main() {
    quadVertex();
    frameMain(FrameIn(uTime, FrameTurn(FRAME_RATE(50), 0.0), FrameTurn(-FRAME_RATE(65), 0.6), FrameTurn(FRAME_RATE(100), 1.1)));
}
