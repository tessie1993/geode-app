#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Morph World's baked assets: what tools/morph_world_assets/bake.py writes from CC0 BlenderKit
// models and materials (docs/morph-world-assets.md has each one's name, author and licence).
//
//   shaders/world_shapes.bin    kShapeCount slabs of a kShapeRes^3 R8 signed distance grid over the
//                               cube [-1, 1]^3, x fastest then y then z; the byte maps the distance
//                               in cube units over [kShapeLo, kShapeHi]. lib_world samples it as one
//                               32 x 32 x 512 GL_TEXTURE_3D (LINEAR, CLAMP_TO_EDGE), the slab picked
//                               by z; shapeDistance() below reads it the same way on the CPU.
//   shaders/world_textures.bin  kTexCount layers of kTexRes^2 R8 surface detail (a material's base
//                               colour as luminance), a GL_TEXTURE_2D_ARRAY with MIRRORED_REPEAT.
//
// The shapes are process-wide and set once (setShapeVolumes): the scene that reads the asset sets
// them, so WorldGenome's open-space probe and corridor port see the same shapes the GPU draws.
// Until then the mesh primitive is a sphere, on both sides (WG_ASSETS = 0 in lib_world).
namespace geode::viz::world {

constexpr int kShapeCount = 16;
constexpr int kShapeRes = 32;
constexpr float kShapeLo = -0.25f;
constexpr float kShapeHi = 0.75f;
constexpr std::size_t kShapeBytes = static_cast<std::size_t>(kShapeCount) * kShapeRes * kShapeRes * kShapeRes;

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
