#version 300 es
precision highp float;
precision highp sampler2D;

// The field a simulation is presented on in 3D (core/viz/present/FieldPresent, lib_field3d), written
// once per frame from the simulation's state at the state's own size.
//
// x runs along the state's LONG side, so a surface lays the long side along its long way round
// whichever way the screen is turned (uSwap: the state is taller than wide). The drift the solver has
// not yet shifted into the state (uFrac, state uv) is taken off here, so the field moves smoothly at
// any speed. Out:
//   r  the height, 0..1: the state's channels weighted by uHeightMask, smoothstepped over uHeightRange
//   g  the tint, a palette position the kind's present reads
//   b  the age: old matter takes the present's second material
//   a  young matter: alive and not yet old, which the present lets glow
// FieldPresent mipmaps the result so the march reads it at its footprint. One state read per texel.

in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uState;
uniform float uSwap;
uniform vec2 uFrac;
uniform vec4 uHeightMask;
uniform vec2 uHeightRange;
uniform vec4 uTintMask;
uniform vec4 uAgeMask;

void main() {
    vec2 at = mix(vUv, vUv.yx, step(0.5, uSwap)) - uFrac;
    vec4 s = texture(uState, at);
    float h = smoothstep(uHeightRange.x, uHeightRange.y, dot(s, uHeightMask));
    float tint = dot(s, uTintMask);
    float age = clamp(dot(s, uAgeMask), 0.0, 1.0);
    float young = smoothstep(0.1, 0.5, h) * (1.0 - smoothstep(0.05, 0.45, age));
    vec4 o = vec4(h, tint, age, young);
    if (any(isnan(o)) || any(isinf(o))) o = vec4(0.0);
    fragColor = o;
}
