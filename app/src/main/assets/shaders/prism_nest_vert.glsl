#version 300 es
//#include lib_frame

uniform float uTime;
flat out vec4 vTurn;
flat out vec4 vTurnB;
flat out vec4 vTurnC;

void main() {
    quadVertex();
    vTurn = frameTurn(uTime, FRAME_RATE(50));
    vTurnB = frameAngle(0.6 - uTime * FRAME_RATE(65));
    vTurnC = frameAngle(1.1 + uTime * FRAME_RATE(100));
}
