#version 300 es
// Speed-colored sprites for the fluid styles' particle layer, drawn additively
// over the dye. Hue is (base + per-emitter offset + speed shift) so streams
// from different spawn points are distinguishable, and the lifecycle fade
// envelope multiplies in (soft births/recycles instead of popping).
//
// The shading itself is lib_particle_shade.glsl - the same code the CPU
// particle styles run - so Particle shape, the SDF silhouettes and the
// tone-mapped glow behave identically on Fluid and Curl Flow. Grading is NOT
// applied here: the composite pass owns saturation, contrast and gamma for the
// whole fluid family, so those go in neutral and only brightness rides through.
//#include lib_particle_common
//#include lib_particle_shade
precision highp float;
in highp float vSpeed;
in highp float vFade;
in highp float vEmitter;
in highp float vSeed;
uniform highp float uHueBase;
uniform highp float uHueSpan;
uniform highp float uBrightness;
uniform highp float uShape;
uniform highp float uGlow;
uniform highp float uTime;
out vec4 fragColor;
void main() {
    if (vFade <= 0.0) { discard; }
    float sp = clamp(vSpeed * 1.8, 0.0, 1.0);
    float hue = fract(uHueBase + vEmitter * 0.09 * uHueSpan + sp * uHueSpan + vSeed * 0.03);
    // gl_PointCoord spans the sprite square, which IS the shade quad.
    vec4 c =
        ptShade(
            gl_PointCoord * 2.0 - 1.0,
            hue,
            0.25 + sp * 0.75,
            vSeed,
            vFade,
            1.0,
            uShape,
            1.0,
            uBrightness,
            1.0,
            1.0,
            uGlow,
            uTime,
            gl_FragCoord.xy
        );
    if (dot(c.rgb, c.rgb) <= 0.0) { discard; }
    // Additive blend (GL_ONE, GL_ONE): only the premultiplied colour lands.
    fragColor = vec4(c.rgb, 1.0);
}
