#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

//#include lib_flash_limit

// Flash limiter, pass 4 of 4 (core/viz/FlashLimitPass): the finished frame to the real target,
// each pixel moved by its tile's gain so the tile shows the limited luminance. The gain field is
// blended bilinearly between tile centres (fetched and blended here, not by the sampler, so the
// result is the same on every GPU), which keeps tile edges invisible; across a uniform flash every
// tile carries the same gain and the shown luminance is exact. In linear light,
// out = mix(in * gain, white, lift): a gain to dim (never clips), a blend toward white to raise.
// When no tile near the pixel is limited, which is nearly always, the pixel is copied untouched.
uniform sampler2D uFrame;
uniform sampler2D uGain;
uniform ivec2 uTileCount;

out vec4 fragColor;

vec2 gainAt(ivec2 tile) {
    vec4 e = texelFetch(uGain, clamp(tile, ivec2(0), uTileCount - 1), 0);
    return vec2(flUnpack16(e.rg), flUnpack16(e.ba));
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec4 colour = texelFetch(uFrame, pixel, 0);
    vec2 frameSize = vec2(textureSize(uFrame, 0));
    vec2 t = gl_FragCoord.xy / frameSize * vec2(uTileCount) - 0.5;
    ivec2 i0 = ivec2(floor(t));
    vec2 f = t - floor(t);
    vec2 g = mix(mix(gainAt(i0), gainAt(i0 + ivec2(1, 0)), f.x), mix(gainAt(i0 + ivec2(0, 1)), gainAt(i0 + ivec2(1, 1)), f.x), f.y);
    if (g.x >= 0.99999 && g.y <= 0.00001) {
        fragColor = vec4(colour.rgb, 1.0);
        return;
    }
    vec3 lin = flSrgbToLinear(colour.rgb);
    lin = mix(lin * g.x, vec3(1.0), g.y);
    fragColor = vec4(flLinearToSrgb(lin), 1.0);
}
