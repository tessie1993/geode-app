#version 300 es
//#include lib_frame

uniform float uTime;
flat out vec4 vTurn;

void main() {
    quadVertex();
    vTurn = frameTurn(uTime, FRAME_RATE(80));
}
