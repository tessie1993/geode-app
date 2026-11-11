#pragma once

#include <cstdint>
#include <string>

// A .milk preset whose warp or composite shader will not translate or compile still plays: projectM swaps
// that shader for its default one and keeps the rest of the preset (MilkDrop's rule, plugin.cpp's fallback
// shaders), and projectm_get_dropped_shaders says which it swapped. The stage says so, through the same
// note a failed load uses ("<path>: <what happened>").
namespace geode::viz::milk {

// projectm_get_dropped_shaders' bits (tools/projectm-v4.1.7-shader-fallback.patch).
inline constexpr uint32_t kDroppedWarp = 1u;
inline constexpr uint32_t kDroppedComposite = 2u;

// Empty when the preset kept its own shaders.
inline std::string droppedShaderNote(const std::string& path, uint32_t dropped) {
    const bool warp = (dropped & kDroppedWarp) != 0;
    const bool composite = (dropped & kDroppedComposite) != 0;
    if (!warp && !composite) return {};
    if (warp && composite) return path + ": its warp and composite shaders did not compile, so the default ones play instead";
    return path + ": its " + (warp ? "warp" : "composite") + " shader did not compile, so the default one plays instead";
}

}  // namespace geode::viz::milk
