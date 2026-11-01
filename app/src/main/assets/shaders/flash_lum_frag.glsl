#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

//#include lib_flash_limit

// Flash limiter, pass 1 of 4 (core/viz/FlashLimitPass): the finished frame to a grid of cells
// (eight per tile each way), each the exact mean relative luminance of the pixels it covers.
// Every pixel is read once with texelFetch and decoded before it is averaged: a bilinear tap would
// average display-encoded values, and the mean of encoded black and white is not half the light.
uniform sampler2D uFrame;
// Cells across and down.
uniform ivec2 uCells;

out vec4 fragColor;

void main() {
    ivec2 size = textureSize(uFrame, 0);
    ivec2 cell = ivec2(gl_FragCoord.xy);
    ivec2 p0 = min((cell * size) / uCells, size - 1);
    ivec2 p1 = clamp(((cell + 1) * size) / uCells, p0 + 1, size);
    float sum = 0.0;
    for (int y = p0.y; y < p1.y; y++) {
        float row = 0.0;
        for (int x = p0.x; x < p1.x; x++) row += flLuminance(texelFetch(uFrame, ivec2(x, y), 0).rgb);
        sum += row;
    }
    float count = float((p1.x - p0.x) * (p1.y - p0.y));
    fragColor = vec4(flPack16(sum / count), 0.0, 1.0);
}
