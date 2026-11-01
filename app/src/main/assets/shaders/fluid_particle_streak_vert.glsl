#version 300 es
// The fluid tracers as motion-blurred streaks: one instanced quad per particle,
// oriented along its velocity and stretched by its speed (ptBillboard, the same
// trick the CPU particle styles use). The upper quality tiers draw this; Low
// and Min keep fluid_particle_vert's GL_POINTS, whose square sprite cannot
// stretch.
//
// What this buys: at 60 fps a tracer moving 2 screen heights a second jumps
// ~70 px between frames, and a round dot drawn at each stop reads as strobing
// grain. Smeared over the distance it actually travelled during the frame, the
// same population reads as continuous flow lines - the look of a long-exposure
// photograph of the fluid - and fast filaments become visible as filaments.
//
// The cost is fill, so the stretch is capped per tier (Tier::maxStretch) and in
// pixels: a slow particle's quad is exactly the point sprite's square, so the
// whole price is paid only where there is motion to show.
//
// State is fetched per vertex (four times per particle). The alternative, a
// vertex buffer written by transform feedback, would trade those fetches for
// a second copy of the state; on the tilers this targets the fetches are cheaper.
//#include lib_particle_common
precision highp float;
precision highp sampler2D;
layout(location = 0) in vec2 aTexel;   // per instance: the particle's state texel
layout(location = 1) in vec2 aCorner;  // per vertex: the quad corner, -1..1
uniform highp sampler2D uState;
uniform highp sampler2D uMeta;
uniform highp float uAspect;
uniform highp float uPointScale;
uniform vec2 uViewport;           // px
uniform float uStretchSeconds;    // how much of a second's travel folds into the streak
uniform float uMaxStretch;        // ceiling on length / width
out highp float vSpeed;
out highp float vFade;
out highp float vEmitter;
out highp float vSeed;
out highp vec2 vCorner;
out highp float vStretch;
void main() {
    vec4 s = texture(uState, aTexel);
    vec4 m = texture(uMeta, aTexel);
    vSpeed = length(s.zw);
    float age = m.x;
    float ttl = max(m.y, 1e-3);
    vFade = smoothstep(0.0, 0.35, age) * (1.0 - smoothstep(ttl * 0.8, ttl, age));
    vEmitter = m.z;
    vSeed = m.w;
    // The same width the point path draws, so a slow tracer is unchanged.
    float widthPx = clamp(uPointScale * (0.8 + vSpeed * 1.6), 1.0, 12.0);
    vec2 held = ptRadiusFade(widthPx * 0.5);
    vFade *= held.y;
    // Sim space spans the viewport height over 2 units on both axes.
    vec2 velPx = s.zw * (0.5 * uViewport.y);
    float stretch;
    // ptBillboard counts stretch in quad half-widths, so the travel is divided by the full width:
    // the streak is then the sprite plus the distance covered in uStretchSeconds, up to the cap.
    vec2 offsetPx = ptBillboard(aCorner, velPx, held.x, uStretchSeconds / (2.0 * held.x), uMaxStretch, stretch);
    // The streak trails BEHIND the particle: shift its centre back half its extra length.
    float speedPx = length(velPx);
    vec2 dir = speedPx > 1.0 ? velPx / speedPx : vec2(1.0, 0.0);
    offsetPx -= dir * held.x * (stretch - 1.0);
    vStretch = stretch;
    vCorner = aCorner;
    vec2 ndc = vec2(s.x / uAspect, s.y) + offsetPx * 2.0 / uViewport;
    // A dead particle collapses its quad off-screen rather than shading a transparent one.
    gl_Position = vFade > 0.0 ? vec4(ndc, 0.0, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);
}
