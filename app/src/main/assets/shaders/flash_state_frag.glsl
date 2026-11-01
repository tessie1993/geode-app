#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

//#include lib_flash_limit

// Flash limiter, pass 3 of 4 (core/viz/FlashLimitPass): one fragment per tile steps that tile's
// state (flash_limit::step, host-tested in core/tests/flash_limit_test.cpp) from last frame's
// textures into this frame's (ping-pong), and writes the gain the final pass applies.
//   state0: shown (rg), anchor (ba)          16-bit each
//   state1: budget / capacity (rg), primed (b)
//   gain:   gain (rg), lift (ba)             16-bit each
uniform sampler2D uState0;
uniform sampler2D uState1;
uniform sampler2D uTiles;
uniform float uDt;

layout(location = 0) out vec4 outState0;
layout(location = 1) out vec4 outState1;
layout(location = 2) out vec4 outGain;

void main() {
    ivec2 tile = ivec2(gl_FragCoord.xy);
    vec4 s0 = texelFetch(uState0, tile, 0);
    vec4 s1 = texelFetch(uState1, tile, 0);
    float raw = flUnpack16(texelFetch(uTiles, tile, 0).rg);
    bool primed = s1.b > 0.5;
    vec3 next = flStep(flUnpack16(s0.rg), flUnpack16(s0.ba), flUnpack16(s1.rg) * uFlCapacity, primed, raw, uDt);
    outState0 = vec4(flPack16(next.x), flPack16(next.y));
    outState1 = vec4(flPack16Down(next.z / uFlCapacity), 1.0, 1.0);
    // The gain is worked out from the quantised shown value, as the host reference's gainFor is.
    vec2 g = flGain(flUnpack16(flPack16(next.x)), raw);
    outGain = vec4(flPack16(g.x), flPack16(g.y));
}
