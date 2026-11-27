#version 300 es
//#include lib_frame

uniform float uTime;
flat out vec4 vTurn;
flat out vec4 vTurnB;

void main() {
    quadVertex();
    vTurn = frameTurn(uTime, FRAME_RATE(40));
    vTurnB = frameAngle(-uTime * FRAME_RATE(30));
}
