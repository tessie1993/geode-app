#version 300 es
// GL_POINTS render for the fluid styles' lifecycle particle layer: a static
// VBO of texel coords, state fetched in the vertex stage (vertex texture
// fetch, core ES3). Age drives a fade-in / fade-out envelope so births and
// recycles are soft, and the emitter index rides along for per-spawn-point
// coloring.
//
// This layer stays GL_POINTS where the CPU particle styles moved to instanced
// billboards, deliberately: it draws up to 49k sprites, and a quad big enough
// to hold the aura would multiply its fill cost by an order of magnitude for
// tracers only a few pixels across. It shares the LOOK instead - the sprite
// square IS the lib_particle_shade quad, so the SDF body lands at PT_SHAPE_R
// of the point radius and the rest is aura, exactly as on the billboard path.
//#include lib_particle_common
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). Half-float state values far exceed that; on GPUs
// honoring sampler precision (Mali) every read clamps and quantizes.
precision highp sampler2D;
layout(location = 0) in vec2 aTexel;
uniform highp sampler2D uState;
uniform highp sampler2D uMeta;
uniform highp float uAspect;
uniform highp float uPointScale;
out highp float vSpeed;
out highp float vFade;
out highp float vEmitter;
out highp float vSeed;
void main() {
    vec4 s = texture(uState, aTexel);
    vec4 m = texture(uMeta, aTexel);
    vSpeed = length(s.zw);
    float age = m.x;
    float ttl = max(m.y, 1e-3);
    // Soft envelope: 0.35 s fade-in, last 20% of life fades out.
    vFade = smoothstep(0.0, 0.35, age) * (1.0 - smoothstep(ttl * 0.8, ttl, age));
    vEmitter = m.z;
    vSeed = m.w;
    gl_Position = vec4(s.x / uAspect, s.y, 0.0, 1.0);
    // Cap raised from the old 8 px so Particle size can actually resolve a
    // shape here; spending it is the user's call and the default scale still
    // lands around 5 px.
    float widthPx = clamp(uPointScale * (0.8 + vSpeed * 1.6), 1.0, 12.0);
    vec2 held = ptRadiusFade(widthPx * 0.5);
    gl_PointSize = held.x * 2.0;
    // Sub-pixel dimming folds into the lifecycle envelope.
    vFade *= held.y;
}
