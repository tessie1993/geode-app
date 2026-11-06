#include "scene/env/Environment.hpp"

#include <string>

#include "scene/env/MorphWorldEnv.hpp"
#include "viz/Catalogue.hpp"

namespace geode::scene::env {

// The registry: one row per ported family, its world made for the style variant asked. A port adds
// its family here (and its style ids to the catalogue's kSixDof once its world passes the 26-ray test).

std::unique_ptr<Environment> make(std::string_view styleId) {
    if (styleId == viz::catalogue::kMorphWorld) return std::make_unique<MorphWorldEnv>(std::string(styleId));
    return nullptr;
}

bool has(std::string_view styleId) { return styleId == viz::catalogue::kMorphWorld; }

}  // namespace geode::scene::env
