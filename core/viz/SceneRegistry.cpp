#include "viz/SceneRegistry.hpp"

#include <array>

#include "viz/scenes/ShaderScene.hpp"

namespace geode::viz {

namespace {

// Port of SceneCapabilities.SHADER_SCENES: id -> shaders/<id>_frag.glsl, in the catalog's order.
constexpr std::array<const char*, 30> kShaderIds = {
    "julia",     "tunnel",  "mandel",    "kaleido",   "plasma",    "bars",       "ring",      "scope",
    "liss",      "warp",    "grid",      "voronoi",   "metaballs", "ripples",    "starfield", "waves",
    "hexgrid",   "spiral",  "aurora",    "solar",     "winter",    "lava",       "vanishing", "morphogen",
    "nebula",    "noneuclid", "kifs",    "orb_lattice", "rod_tunnel", "neon_tiles",
};

bool isShaderId(const std::string& id) {
    for (const char* known : kShaderIds) {
        if (id == known) return true;
    }
    return false;
}

}  // namespace

bool SceneRegistry::knows(const std::string& id) const {
    return isShaderId(id);
}

std::vector<std::string> SceneRegistry::availableIds() const {
    return std::vector<std::string>(kShaderIds.begin(), kShaderIds.end());
}

std::unique_ptr<Scene> SceneRegistry::create(const std::string& id, const std::string& quadVert) const {
    if (!isShaderId(id)) return nullptr;
    std::string error;
    const auto fragment = assets_.load(id + "_frag.glsl", &error);
    if (!fragment) {
        host_.onShaderError(error);
        return nullptr;
    }
    return std::make_unique<ShaderScene>(id, quadVert, *fragment, cache_, host_);
}

}  // namespace geode::viz
