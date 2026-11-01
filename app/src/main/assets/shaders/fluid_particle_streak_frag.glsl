#version 300 es
// The streak path's fragment stage: fluid_particle_frag's shading on a quad
// instead of a point sprite. The corner coordinate replaces gl_PointCoord, and
// the billboard's stretch goes to ptShade, which divides the light back out so
// a fast tracer is longer, not brighter - one particle's light spread along the
// path it covered, which is what a motion blur is.
//#include lib_particle_common
//#include lib_particle_shade
precision highp float;
in highp float vSpeed;
in highp float vFade;
in highp float vEmitter;
in highp float vSeed;
in highp vec2 vCorner;
in highp float vStretch;
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
    vec4 c =
        ptShade(
            vCorner,
            hue,
            0.25 + sp * 0.75,
            vSeed,
            vFade,
            vStretch,
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
    fragColor = vec4(c.rgb, 1.0);
}
