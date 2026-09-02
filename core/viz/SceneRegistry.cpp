#include "viz/SceneRegistry.hpp"

#include <array>

#include "viz/scenes/AcidScene.hpp"
#include "viz/scenes/BeamScene.hpp"
#include "viz/scenes/CurlFlowScene.hpp"
#include "viz/scenes/CymaticsScene.hpp"
#include "viz/scenes/FluidScene.hpp"
#include "viz/scenes/LifeScene.hpp"
#include "viz/scenes/MilkdropScene.hpp"
#include "viz/scenes/MycoScene.hpp"
#include "viz/scenes/ShaderScene.hpp"
#include "viz/scenes/SilkScene.hpp"
#include "viz/scenes/StyleCatalog.hpp"
#include "viz/scenes/WaterScene.hpp"

namespace geode::viz {

namespace {

// Port of SceneCapabilities.SHADER_SCENES: id -> shaders/<id>_frag.glsl, in the catalog's order.
constexpr std::array<const char*, 31> kShaderIds = {
    "julia",     "tunnel",  "mandel",    "kaleido",   "plasma",    "bars",       "ring",      "scope",
    "liss",      "warp",    "grid",      "voronoi",   "metaballs", "ripples",    "starfield", "waves",
    "hexgrid",   "spiral",  "aurora",    "solar",     "winter",    "lava",       "vanishing", "morphogen",
    "nebula",    "noneuclid", "kifs",    "orb_lattice", "rod_tunnel", "neon_tiles",
    "fractal_tunnel",
};

constexpr const char* kMilkdrop = "milkdrop";
constexpr const char* kFluid = "fluid";
constexpr const char* kCurlFlow = "curlflow";
constexpr const char* kWater = "water";
constexpr const char* kBeam = "beam";

bool isShaderId(const std::string& id) {
    for (const char* known : kShaderIds) {
        if (id == known) return true;
    }
    return false;
}

void append(std::vector<std::string>& out, std::vector<std::string> ids) {
    out.insert(out.end(), ids.begin(), ids.end());
}

}  // namespace

bool SceneRegistry::knows(const std::string& id) const {
    return isShaderId(id) || styles::cymatics(id) || styles::silk(id) || styles::life(id) || styles::acid(id) || styles::myco(id) ||
           id == kMilkdrop || id == kFluid || id == kCurlFlow || id == kWater || id == kBeam;
}

std::vector<std::string> SceneRegistry::availableIds() const {
    std::vector<std::string> out;
    append(out, styles::silkIds());
    append(out, styles::lifeIds());
    append(out, styles::mycoIds());
    append(out, styles::acidIds());
    out.insert(out.end(), kShaderIds.begin(), kShaderIds.end());
    out.emplace_back(kMilkdrop);
    out.emplace_back(kFluid);
    out.emplace_back(kCurlFlow);
    out.emplace_back(kWater);
    append(out, styles::cymaticsIds());
    out.emplace_back(kBeam);
    return out;
}

std::unique_ptr<Scene> SceneRegistry::create(const std::string& id, const std::string& quadVert) const {
    if (isShaderId(id)) {
        std::string error;
        const auto fragment = loader_.assets.load(id + "_frag.glsl", &error);
        if (!fragment) {
            host_.onShaderError(error);
            return nullptr;
        }
        return std::make_unique<ShaderScene>(id, quadVert, *fragment, loader_.cache, host_);
    }
    if (const auto* style = styles::cymatics(id)) return std::make_unique<CymaticsScene>(*style, loader_, host_);
    if (const auto* style = styles::silk(id)) return std::make_unique<SilkScene>(*style, loader_, profile_, host_);
    if (const auto* style = styles::life(id)) return std::make_unique<LifeScene>(*style, loader_, host_);
    if (const auto* style = styles::acid(id)) return std::make_unique<AcidScene>(*style, loader_, host_);
    if (const auto* style = styles::myco(id)) return std::make_unique<MycoScene>(*style, loader_, host_);
    if (id == kMilkdrop) return std::make_unique<MilkdropScene>(loader_, host_);
    if (id == kFluid) return std::make_unique<FluidScene>(loader_, host_);
    if (id == kCurlFlow) return std::make_unique<CurlFlowScene>(loader_, host_);
    if (id == kWater) return std::make_unique<WaterScene>(loader_, host_);
    if (id == kBeam) return std::make_unique<BeamScene>(loader_, host_);
    return nullptr;
}

}  // namespace geode::viz
