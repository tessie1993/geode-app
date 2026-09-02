#pragma once
#include <memory>
#include <string>
#include <vector>

#include "viz/GlProfile.hpp"
#include "viz/ProgramBinaryCache.hpp"
#include "viz/Scene.hpp"
#include "viz/ShaderSource.hpp"
#include "viz/scenes/ProgramLoader.hpp"

namespace geode::viz {

// Port of SceneRegistry.createScene: an id is known when a native scene class can build it.
class SceneRegistry {
public:
    SceneRegistry(const ShaderSource& assets, ProgramBinaryCache* cache, const GlProfile* profile, SceneHost host)
        : loader_{assets, cache}, profile_(profile), host_(std::move(host)) {}

    bool knows(const std::string& id) const;
    // Every id in the order the Kotlin registry listed them.
    std::vector<std::string> availableIds() const;
    // Builds an uninitialised scene; nullptr for an id native code cannot draw.
    std::unique_ptr<Scene> create(const std::string& id, const std::string& quadVert) const;

private:
    ProgramLoader loader_;
    const GlProfile* profile_;
    SceneHost host_;
};

}  // namespace geode::viz
