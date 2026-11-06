#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Morph World's baked assets: what tools/morph_world_assets/bake.py writes from CC0 BlenderKit
// models and materials (docs/morph-world-assets.md has each one's name, author and licence).
//
//   shaders/world_shapes.bin    kShapeCount slabs of a kShapeRes^3 R8 signed distance grid over the
//                               cube [-1, 1]^3, x fastest then y then z; the byte maps the distance
//                               in cube units over [kShapeLo, kShapeHi]. shapeDistance() below reads
//                               the file as it is. The GPU never sees the slabs stacked (32 x 32 x 768
//                               is deeper than the 256 GLES 3.0 promises for GL_MAX_3D_TEXTURE_SIZE):
//                               ShaderScene repacks them at upload (packAtlas) into a tile atlas of
//                               kAtlasCols x kAtlasRows tiles of kShapeRes^3 side by side in x and y,
//                               192 x 128 x 32 (LINEAR, CLAMP_TO_EDGE). lib_world's wShape() finds a
//                               model's tile with tileOrigin()'s maths and clamps each sample to that
//                               tile's texel centres, so linear filtering never reads a neighbour.
//   shaders/world_textures.bin  kTexCount layers of kTexRes^2 R8 surface detail (a material's base
//                               colour as luminance), a GL_TEXTURE_2D_ARRAY with MIRRORED_REPEAT.
//
// The shapes are process-wide and set once (setShapeVolumes): the scene that reads the asset sets
// them, so WorldGenome's open-space probe and corridor port see the same shapes the GPU draws.
// Until then the mesh primitive is a sphere, on both sides (WG_ASSETS = 0 in lib_world).
namespace geode::viz::world {

constexpr int kShapeCount = 24;
constexpr int kShapeRes = 32;
constexpr float kShapeLo = -0.25f;
constexpr float kShapeHi = 0.75f;
constexpr std::size_t kShapeBytes = static_cast<std::size_t>(kShapeCount) * kShapeRes * kShapeRes * kShapeRes;

// The GPU's tile atlas: shape i sits at tile (i % kAtlasCols, i / kAtlasCols), every tile a whole
// kShapeRes^3 volume, depth kShapeRes. lib_world's WG_ATLAS_COLS and WG_ATLAS_ROWS are these.
constexpr int kAtlasCols = 6;
constexpr int kAtlasRows = 4;
constexpr int kAtlasWidth = kAtlasCols * kShapeRes;
constexpr int kAtlasHeight = kAtlasRows * kShapeRes;
constexpr int kAtlasDepth = kShapeRes;
constexpr std::size_t kAtlasBytes = static_cast<std::size_t>(kAtlasWidth) * kAtlasHeight * kAtlasDepth;
static_assert(kAtlasCols * kAtlasRows >= kShapeCount, "every shape has a tile");
// GLES 3.0's guaranteed GL_MAX_3D_TEXTURE_SIZE; the atlas fits it in every dimension.
constexpr int kGles3Max3dSize = 256;
static_assert(kAtlasWidth <= kGles3Max3dSize && kAtlasHeight <= kGles3Max3dSize && kAtlasDepth <= kGles3Max3dSize,
              "the atlas fits any GLES 3.0 device");

struct TileOrigin {
    int x = 0;
    int y = 0;
};

// The texel where shape `index`'s tile starts (z is always 0). Out-of-range indices clamp.
TileOrigin tileOrigin(int index);

// The largest of the atlas's three dimensions: what GL_MAX_3D_TEXTURE_SIZE must reach to hold it.
constexpr int atlasMaxDimension() {
    return kAtlasWidth > kAtlasHeight ? (kAtlasWidth > kAtlasDepth ? kAtlasWidth : kAtlasDepth)
                                      : (kAtlasHeight > kAtlasDepth ? kAtlasHeight : kAtlasDepth);
}

// world_shapes.bin's slabs (kShapeBytes) as the atlas the GPU samples (kAtlasBytes, x fastest, then
// y, then z; unused tiles zero). Empty if `slabs` is the wrong size.
std::vector<uint8_t> packAtlas(const std::vector<uint8_t>& slabs);

// lib_world's wShape() on the CPU: shape `index` at cube point (x, y, z), sampled from `atlas` the
// way the GPU does (the normalized coordinate clamped to the tile's texel centres, then GL_LINEAR).
// It must equal shapeDistance() on the same shapes; world_genome_test checks that it does.
float atlasDistance(const std::vector<uint8_t>& atlas, int index, float x, float y, float z);

constexpr int kTexCount = 8;
constexpr int kTexRes = 128;
constexpr std::size_t kTexBytes = static_cast<std::size_t>(kTexCount) * kTexRes * kTexRes;

// Takes the bytes of world_shapes.bin. The first call with kShapeBytes bytes wins and holds for the
// process; any later call, or one with the wrong size, is ignored. Returns whether shapes are set.
bool setShapeVolumes(const std::vector<uint8_t>& bytes);
bool hasShapes();

// The signed distance of baked shape `index` at cube point (x, y, z), in cube units: trilinear
// inside the cube (GL_LINEAR's sampling), and outside it never more than the true distance (the
// distance to the cube plus nothing, or the boundary sample less the way out, whichever is more).
// Without shapes, a sphere of radius 0.6.
float shapeDistance(int index, float x, float y, float z);

}  // namespace geode::viz::world
