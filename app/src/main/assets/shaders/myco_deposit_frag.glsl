#version 300 es
precision highp float;

// MYCELIUM - the deposit pass, fragment side. Additive blending accumulates
// every agent's drop; r is species A's pheromone, g is species B's. The
// deposit stays LINEAR (never log-packed) so two crossing agents deposit
// exactly twice one agent's amount.

uniform float uDeposit;

flat in float vSpecies;
out vec4 fragColor;

void main() {
    float a = vSpecies < 0.5 ? uDeposit : 0.0;
    float b = vSpecies < 0.5 ? 0.0 : uDeposit;
    fragColor = vec4(a, b, 0.0, 0.0);
}
