#version 300 es
// Ported from WebGL-Fluid-Simulation - MIT License, (c) 2017 Pavel Dobryakov
// (structure); sim-space varying per Geode FLUID_SIM v2 spec.
precision highp float;
layout(location = 0) in vec2 aPosition;
uniform vec2 uInvRes;   // 1/texels of the grid being finite-differenced
uniform float uAspect;  // width/height of the domain
out vec2 vUv;
out vec2 vSim;          // x in [-A,A], y in [-1,1]
out vec2 vL;
out vec2 vR;
out vec2 vT;
out vec2 vB;
void main() {
    vUv = aPosition * 0.5 + 0.5;
    vSim = vec2(aPosition.x * uAspect, aPosition.y);
    vL = vUv - vec2(uInvRes.x, 0.0);
    vR = vUv + vec2(uInvRes.x, 0.0);
    vT = vUv + vec2(0.0, uInvRes.y);
    vB = vUv - vec2(0.0, uInvRes.y);
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
