#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;

//#include lib_flash_limit

// Flash limiter, pass 2 of 4 (core/viz/FlashLimitPass): cells to tiles, each tile the mean of its
// 8 x 8 cells weighted by their pixel counts, so it is the mean over exactly the tile's pixels.
uniform sampler2D uCellLumRed;
uniform sampler2D uCellGuarded;
uniform ivec2 uFrameSize;
uniform ivec2 uCellCount;
const int kCellsPerTile = 8;

layout(location = 0) out vec4 outLumRed;
layout(location = 1) out vec4 outGuarded;

void main() {
    ivec2 base = ivec2(gl_FragCoord.xy) * kCellsPerTile;
    vec3 sum = vec3(0.0);
    float area = 0.0;
    for (int y = 0; y < kCellsPerTile; y++) {
        int c = base.y + y;
        float h = float(max(min(((c + 1) * uFrameSize.y) / uCellCount.y, uFrameSize.y) - flCellStart(c, uFrameSize.y, uCellCount.y), 1));
        for (int x = 0; x < kCellsPerTile; x++) {
            int d = base.x + x;
            float w = float(max(min(((d + 1) * uFrameSize.x) / uCellCount.x, uFrameSize.x) - flCellStart(d, uFrameSize.x, uCellCount.x), 1));
            vec4 a = texelFetch(uCellLumRed, ivec2(d, c), 0);
            vec4 g = texelFetch(uCellGuarded, ivec2(d, c), 0);
            sum += w * h * vec3(flUnpack16(a.rg), flUnpack16(a.ba), flUnpack16(g.rg));
            area += w * h;
        }
    }
    vec3 mean = sum / area;
    outLumRed = vec4(flPack16(mean.x), flPack16(mean.y));
    outGuarded = vec4(flPack16(mean.z), 0.0, 1.0);
}
