#version 300 es
//#include lib_frame

uniform float uTime;
flat out vec4 vTurn;
flat out vec4 vJulia;

void main() {
    quadVertex();
    vTurn = frameTurn(uTime, FRAME_RATE(40));
    vJulia = frameTurn(uTime, FRAME_RATE(90));
}
