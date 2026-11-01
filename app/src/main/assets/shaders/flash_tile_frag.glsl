#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

//#include lib_flash_limit

// Flash limiter, pass 2 of 4 (core/viz/FlashLimitPass): the cell grid to the tile grid, each tile
// the mean of its 8 x 8 cells (cells differ in size by at most one pixel each way, so their plain
// mean is the tile's luminance to well under a 16-bit step on any real surface).
uniform sampler2D uCellsTex;
// Cells per tile each way.
const int kCellsPerTile = 8;

out vec4 fragColor;

void main() {
    ivec2 base = ivec2(gl_FragCoord.xy) * kCellsPerTile;
    float sum = 0.0;
    for (int y = 0; y < kCellsPerTile; y++) {
        for (int x = 0; x < kCellsPerTile; x++) sum += flUnpack16(texelFetch(uCellsTex, base + ivec2(x, y), 0).rg);
    }
    fragColor = vec4(flPack16(sum / float(kCellsPerTile * kCellsPerTile)), 0.0, 1.0);
}
