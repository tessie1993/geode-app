#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

//#include lib_flash_limit

// Flash limiter, pass 4 of 4 (core/viz/FlashLimitPass): the drawn frame to the real target. Each
// pixel takes its tile's transform (found through the same integer cell split the measuring passes
// used, so a tile's transform covers exactly the pixels its means came from): optionally the red
// guard, then out = mix(in * gain, white, lift) in linear light. The transform is constant over a
// tile, which is what makes the tile's shown luminance and red exactly the values the control pass
// counted; while a region is held its tiles can therefore show as blocks, which only happens while
// the drawn picture is strobing. Wherever the tile is not limited (nearly always, everywhere), the
// pixel is copied untouched.
uniform sampler2D uFrame;
uniform sampler2D uCtrl0;
uniform sampler2D uCtrl1;
uniform ivec2 uGrid;
uniform ivec2 uCellCount;

out vec4 fragColor;

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(uFrame, 0);
    vec4 colour = texelFetch(uFrame, pixel, 0);
    ivec2 cell = ivec2(flCellOf(pixel.x, size.x, uCellCount.x), flCellOf(pixel.y, size.y, uCellCount.y));
    ivec2 tile = min(cell / (uCellCount / uGrid), uGrid - 1);
    int t = tile.y * uGrid.x + tile.x;
    ivec2 texel = ivec2(t % 16, t / 16);
    vec4 c0 = texelFetch(uCtrl0, texel, 0);
    vec4 c1 = texelFetch(uCtrl1, texel, 0);
    bool guarded = c0.a > 0.5;
    float gain = flUnpack16(c1.rg);
    float lift = flUnpack16(c1.ba);
    if (!guarded && gain == 1.0 && lift == 0.0) {
        fragColor = vec4(colour.rgb, 1.0);
        return;
    }
    vec3 lin = flSrgbToLinear(colour.rgb);
    if (guarded) lin = flGuard(lin);
    lin = mix(lin * gain, vec3(1.0), lift);
    fragColor = vec4(flLinearToSrgb(lin), 1.0);
}
