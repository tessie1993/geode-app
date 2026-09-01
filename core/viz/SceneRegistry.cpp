#include "viz/SceneRegistry.hpp"

namespace geode::viz {

bool SceneRegistry::knows(const std::string& id) const {
    for (const auto& known : availableIds()) {
        if (known == id) return true;
    }
    return false;
}

// No scene class is native yet; 4.5 registers the fragment styles here.
std::vector<std::string> SceneRegistry::availableIds() const {
    return {};
}

std::unique_ptr<Scene> SceneRegistry::create(const std::string& id, const std::string& quadVert) const {
    (void) id;
    (void) quadVert;
    (void) assets_;
    (void) cache_;
    (void) host_;
    return nullptr;
}

}  // namespace geode::viz
