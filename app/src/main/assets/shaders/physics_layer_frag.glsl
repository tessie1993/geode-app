#version 300 es
// The physics layers' back half (core/viz/physics): the fluid under or over the style's finished picture. The
// maths is PhysicsPlan's layer(), which physics_layers_test holds: at uFluid 0 the style comes out as it went in;
// behind (uOrder 0) the ink fills the style's darks and leaves its lit parts alone; over (uOrder 1) it is
// screened on, so it only ever lightens. The particles are drawn over this afterwards (physics_particle_frag).
precision highp float;
precision highp sampler2D;
in vec2 vUv;
uniform sampler2D uStyle;  // the finished picture
uniform sampler2D uDye;    // the fluid's ink (a zero texture with the fluid off)
uniform float uFluid;      // the fluid's mix, 0..1
uniform float uOrder;      // 0 behind the style .. 1 over it
out vec4 fragColor;

const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

void main() {
    vec4 style = texture(uStyle, vUv);
    vec3 s = style.rgb;
    vec3 ink = clamp(texture(uDye, vUv).rgb, 0.0, 1.0);
    float under = 1.0 - smoothstep(0.0, 0.45, dot(s, LUMA));
    vec3 behind = min(s + ink * under, vec3(1.0));
    vec3 over = s + ink * (1.0 - s);
    vec3 layered = mix(behind, over, clamp(uOrder, 0.0, 1.0));
    fragColor = vec4(clamp(mix(s, layered, clamp(uFluid, 0.0, 1.0)), 0.0, 1.0), style.a);
}
