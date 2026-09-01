#version 300 es
precision highp float;

// MYCELIUM - the deposit pass, vertex side. One point per agent: the vertex
// fetches its agent's texel (vertex texture fetch, core ES 3.0 and already
// relied on by the fluid family's particle layer) and lands a 1-texel point
// at the agent's trail position. The fragment side writes the deposit
// additively; species decides the channel.

uniform sampler2D uAgents;
uniform vec2 uAgentRes;

flat out float vSpecies;

void main() {
    int w = int(uAgentRes.x);
    ivec2 ij = ivec2(gl_VertexID % w, gl_VertexID / w);
    vec4 a = texelFetch(uAgents, ij, 0);
    vSpecies = a.w;
    gl_Position = vec4(a.xy * 2.0 - 1.0, 0.0, 1.0);
    gl_PointSize = 1.0;
}
