#include "viz/SceneRegistry.hpp"

#include "viz/Catalogue.hpp"
#include "viz/scenes/AcidScene.hpp"
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

// Which ids exist, which are fragment styles and how they turn is the catalogue's (viz/Catalogue);
// the registry only builds the scene an id names.

bool SceneRegistry::isShaderStyle(const std::string& id) { return catalogue::isShaderStyle(id); }

float SceneRegistry::spinRate(const std::string& id) { return catalogue::spinRate(id); }

bool SceneRegistry::knows(const std::string& id) const { return catalogue::knows(id); }

std::vector<std::string> SceneRegistry::availableIds() const { return catalogue::allIds(); }

std::unique_ptr<Scene> SceneRegistry::create(const std::string& id, const std::string& quadVert) const {
    if (catalogue::isShaderStyle(id)) {
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
    if (const auto* style = styles::fluid(id)) return std::make_unique<FluidScene>(*style, loader_, host_);
    switch (catalogue::kindOf(id)) {
        case catalogue::Kind::Milkdrop: return std::make_unique<MilkdropScene>(loader_, host_);
        case catalogue::Kind::CurlFlow: return std::make_unique<CurlFlowScene>(loader_, host_);
        case catalogue::Kind::Water: return std::make_unique<WaterScene>(loader_, host_);
        default: return nullptr;
    }
}

}  // namespace geode::viz
