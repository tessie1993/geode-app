#version 300 es
// Liquid-ink injection for the WATER style: the same batched Gaussian kernel
// ripple_splat_frag uses on the height channel, but writing COLOUR into the
// ink field instead. RGB accumulates the splat's palette colour, A carries
// coverage so the display pass knows how much of a texel is liquid.
//
// Every emitter splat therefore deposits its own colour into a film that then
// flows with the surface, which is what turns the pool from a tinted
// background into the visual itself gone liquid.
precision highp float;
precision highp sampler2D;
in vec2 vUv;
in vec2 vSim;
uniform sampler2D uTarget;
uniform vec4 uDrops[8];       // xy = sim position, z = radius, w = amplitude
uniform vec4 uDropColor[8];   // rgb = palette colour, a = coverage gain
uniform int uDropCount;
uniform float uCeiling;       // saturation rail for rgb (HDR headroom)
out vec4 fragColor;

void main() {
    vec4 ink = texture(uTarget, vUv);
    for (int i = 0; i < 8; i++) {
        if (i >= uDropCount) break;
        vec4 d = uDrops[i];
        float r = max(d.z, 1e-4);
        vec2 diff = vSim - d.xy;
        // Lockstep with RippleMath.dropProfile: amp * exp(-dist^2 / r^2).
        // abs() on the amplitude: a catch-point drain dips the surface DOWN
        // (negative height) but still stains the water the colour it carries,
        // and a negative deposit would punch a hole in the film instead.
        float g = abs(d.w) * exp(-dot(diff, diff) / (r * r));
        ink.rgb += uDropColor[i].rgb * g;
        ink.a += uDropColor[i].a * g;
    }
    ink.rgb = min(ink.rgb, vec3(uCeiling));
    ink.a = clamp(ink.a, 0.0, 1.0);
    fragColor = ink;
}
