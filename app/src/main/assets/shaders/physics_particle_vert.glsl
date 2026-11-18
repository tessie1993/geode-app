#version 300 es
// The physics layers' front half (core/viz/physics): one point per particle of fluid::Particles' state (the
// layout fluid_particle_update_frag writes: A = pos.xy, vel.xy in sim units; B = age, ttl, emitter, seed), each
// coloured from the picture under it. The picture's hue is lifted to full strength, so a particle over a dim
// stretch keeps the colour the picture has there; over black it takes the fluid's ink when the fluid runs, else
// a pale glint. Births and recycles fade in and out over the particle's life.
precision highp float;
precision highp sampler2D;
layout(location = 0) in vec2 aTexel;
uniform sampler2D uState;
uniform sampler2D uMeta;
uniform sampler2D uStyle;    // the style's finished frame
uniform sampler2D uDye;      // the fluid's ink (a zero texture with the fluid off)
uniform float uAspect;
uniform float uPointScale;   // the sprite's base size, pixels
out vec3 vColour;
out float vFade;
void main() {
    vec4 s = texture(uState, aTexel);
    vec4 m = texture(uMeta, aTexel);
    float age = m.x;
    float ttl = max(m.y, 1e-3);
    vFade = smoothstep(0.0, 0.35, age) * (1.0 - smoothstep(ttl * 0.8, ttl, age));
    vec2 clip = vec2(s.x / uAspect, s.y);
    vec2 uv = clamp(clip * 0.5 + 0.5, 0.0, 1.0);
    vec3 c = max(textureLod(uStyle, uv, 0.0).rgb, clamp(textureLod(uDye, uv, 0.0).rgb, 0.0, 1.0));
    float peak = max(c.r, max(c.g, c.b));
    vec3 hue = c / max(peak, 1e-3);
    // Dark under the particle: from its hue toward a pale glint, so a particle over black still shows.
    vColour = mix(vec3(0.85), hue, smoothstep(0.02, 0.12, peak)) * (0.6 + 0.4 * peak);
    gl_Position = vec4(clip, 0.0, 1.0);
    gl_PointSize = clamp(uPointScale * (0.8 + length(s.zw) * 1.2), 1.0, 10.0);
}
