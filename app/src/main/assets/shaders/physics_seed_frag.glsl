#version 300 es
// The physics layers' fluid takes its ink and its stir from the style's finished picture (core/viz/physics,
// FluidSim::applyField), once a frame before the solver steps. Built on fluid_base_vert.glsl, so vL/vR/vT/vB
// are the neighbouring cells of the field being seeded.
//
// uMode 0, velocity: the picture's contours stir the fluid. The luminance gradient turned a quarter runs along
// each contour, which is divergence-free, so the pressure solve keeps all of it: a lit shape sets the fluid
// circling round its edge. The warm-cool gradient (red minus blue) adds a second stir, so warm and cool shapes
// turn their own ways. uSwirl is the push for a whole step of luminance across two cells (PhysicsPlan swirlGain).
//
// uMode 1, dye: the ink moves toward the picture's colour where the picture is lit (uWeight, PhysicsPlan
// dyeWeight, frame-rate independent); where it is dark the ink the fluid carried there stays, so the style's
// colours flow out into its darks.
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP; half-float fields need highp.
precision highp sampler2D;
in vec2 vUv;
in vec2 vL;
in vec2 vR;
in vec2 vT;
in vec2 vB;
uniform sampler2D uTarget;  // the field's state
uniform sampler2D uStyle;   // the style's finished picture
uniform int uMode;
uniform float uSwirl;
uniform float uDt;
uniform float uWeight;
out vec4 fragColor;

const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

// Luminance and warmth of the picture at a point.
vec2 lumaWarm(vec2 uv) {
    vec3 c = texture(uStyle, clamp(uv, 0.0, 1.0)).rgb;
    return vec2(dot(c, LUMA), c.r - c.b);
}

void main() {
    vec4 base = texture(uTarget, vUv);
    if (uMode == 0) {
        vec2 l = lumaWarm(vL);
        vec2 r = lumaWarm(vR);
        vec2 t = lumaWarm(vT);
        vec2 b = lumaWarm(vB);
        vec2 gradLuma = vec2(r.x - l.x, t.x - b.x);
        vec2 gradWarm = vec2(r.y - l.y, t.y - b.y);
        vec2 g = gradLuma + 0.6 * gradWarm;
        vec2 v = base.xy + vec2(-g.y, g.x) * (uSwirl * uDt);
        // Self-heal, as the advection does: a NaN or Inf must not latch into the field.
        if (any(isnan(v)) || any(isinf(v))) v = vec2(0.0);
        fragColor = vec4(v, 0.0, 1.0);
    } else {
        vec3 s = texture(uStyle, vUv).rgb;
        float lit = smoothstep(0.04, 0.5, dot(s, LUMA));
        vec3 ink = mix(base.rgb, s, clamp(uWeight * lit, 0.0, 1.0));
        if (any(isnan(ink)) || any(isinf(ink))) ink = vec3(0.0);
        fragColor = vec4(ink, 1.0);
    }
}
