#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

//#include lib_flash_limit

// Flash limiter, pass 1 of 4 (core/viz/FlashLimitPass): the drawn frame to a grid of cells (8 per
// tile each way), each the exact mean, over the pixels it covers, of three linear measures:
// relative luminance, saturated-red value, and luminance with the red guarded away. Every pixel is
// read once with texelFetch and decoded before averaging: a bilinear tap would average
// display-encoded values, and the mean of encoded black and white is not half the light.
uniform sampler2D uFrame;
uniform ivec2 uCellCount;

layout(location = 0) out vec4 outLumRed;
layout(location = 1) out vec4 outGuarded;

void main() {
    ivec2 size = textureSize(uFrame, 0);
    ivec2 cell = ivec2(gl_FragCoord.xy);
    ivec2 p0 = ivec2(flCellStart(cell.x, size.x, uCellCount.x), flCellStart(cell.y, size.y, uCellCount.y));
    ivec2 p1 = clamp(((cell + 1) * size) / uCellCount, p0 + 1, size);
    vec3 sum = vec3(0.0);
    for (int y = p0.y; y < p1.y; y++) {
        vec3 row = vec3(0.0);
        for (int x = p0.x; x < p1.x; x++) row += flMeasure(flSrgbToLinear(texelFetch(uFrame, ivec2(x, y), 0).rgb));
        sum += row;
    }
    vec3 mean = sum / float((p1.x - p0.x) * (p1.y - p0.y));
    outLumRed = vec4(flPack16(mean.x), flPack16(mean.y));
    outGuarded = vec4(flPack16(mean.z), 0.0, 1.0);
}
