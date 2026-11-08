#pragma once

#include <array>
#include <cstddef>

#include "scene/object/ObjectSystem.hpp"

// What the object overlay pass (core/viz/frame/ObjectOverlayPass, scene_objects_frag) uploads, packed on
// the CPU by one pure function so a host test checks exactly what the GPU will read (#515): how many
// shapes, where they are, and the cap.
//
// THE CONTRACT with scene_objects_frag.glsl (keep the two in step):
//   - uniform block SceneObjects (std140) { vec4 uObjects[kMaxShapes * 5]; }: shape i is uObjects[5i ..
//     5i + 4] = ShapeGpu's posScale, orient, size, material, life (ObjectSystem.hpp). 64 × 80 bytes =
//     5 KB, inside GLES 3.0's 16 KB minimum MAX_UNIFORM_BLOCK_SIZE; a plain uniform array of 320 vec4s
//     would not fit the 224-vector minimum of MAX_FRAGMENT_UNIFORM_VECTORS, hence the block.
//   - uniform int uObjectCount: how many of them are live, 0..kMaxShapes; the shader's loop is bounded
//     by the constant kMaxShapes and breaks on it.
//   - uObjLook (vec4 hue, emission, gain, fog) and uObjView (vec4 roll, lens, 0, 0): the environment's
//     material and camera feel (OverlayLook), so a shape is lit and coloured like the world round it
//     and its rays turn and focus as the world's do.
//   - the palette uniforms lib_palette reads (OverlayPalette), the same values the style's own program
//     was given this frame, and the renderer's palette LUT.

namespace geode::scene::object {

// The environment's material and view for objects, read off its uniforms each frame (Environment::
// objectLook). The defaults are a neutral world: palette offset 0, plain light, no fog, no roll or lens.
struct OverlayLook {
    float hue = 0.0f;       // the world's palette position (Morph World: its offset plus the chord's turn)
    float emission = 1.0f;  // the rim and glow light
    float gain = 1.0f;      // the world's brightness envelope (rate-limited there), applied to objects too
    float fog = 0.0f;       // the world's fog density; objects fade toward the fog colour by it, never hide
    float roll = 0.0f;      // radians the view banks about its forward axis (the world's own roll)
    float lens = 1.0f;      // the world's lens factor on uCamFocal
};

// The style's palette this frame, as its ShaderScene uploaded it (lib_palette's inputs).
struct OverlayPalette {
    float base = 0.0f;
    float range = 1.0f;
    float base2 = 0.0f;
    float range2 = 1.0f;
    float mix = 0.0f;
    float colorShift = 0.0f;  // the colour shift plus the style's colour-cycle phase
    float hueRange = 1.0f;
    float lutMix = 0.0f;      // 0: procedural, 1: the LUT row
    float lutRow = 0.1f;      // the LUT row's texture coordinate
};

struct OverlayFrame {
    static constexpr std::size_t kMaxShapes = ObjectPool::kMaxShapesAndModels;
    static constexpr std::size_t kVec4PerShape = sizeof(ShapeGpu) / (4 * sizeof(float));

    std::array<ShapeGpu, kMaxShapes> shapes{};
    int count = 0;
    OverlayLook look{};
    OverlayPalette palette{};

    const float* data() const { return &shapes[0].posScale[0]; }
    // The bytes of the block the draw uploads: only the live shapes.
    std::size_t bytes() const { return static_cast<std::size_t>(count) * sizeof(ShapeGpu); }
};
static_assert(OverlayFrame::kVec4PerShape == 5, "scene_objects_frag reads five vec4s per shape");
static_assert(sizeof(OverlayFrame::shapes) <= 16384, "the block fits GLES 3.0's minimum MAX_UNIFORM_BLOCK_SIZE");

// Packs the drawable shapes (ObjectSystem::packShapes: born, visible, slot order) into `out`, at most
// min(cap, kMaxShapes) of them, with the look and the palette. Returns the count. Pure.
int packOverlay(const ObjectSystem& objects, const OverlayLook& look, const OverlayPalette& palette, std::size_t cap,
                OverlayFrame& out);

}  // namespace geode::scene::object
