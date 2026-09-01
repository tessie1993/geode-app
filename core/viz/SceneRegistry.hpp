#pragma once
#include <memory>
#include <string>
#include <vector>

#include "viz/ProgramBinaryCache.hpp"
#include "viz/Scene.hpp"
#include "viz/ShaderSource.hpp"

namespace geode::viz {

// The native scene table: an id is known when a native scene class can build it.
class SceneRegistry {
public:
    SceneRegistry(const ShaderSource& assets, ProgramBinaryCache* cache, SceneHost host)
        : assets_(assets), cache_(cache), host_(std::move(host)) {}

    bool knows(const std::string& id) const;
    std::vector<std::string> availableIds() const;
    // Builds an uninitialised scene; nullptr for an id native code cannot draw yet.
    std::unique_ptr<Scene> create(const std::string& id, const std::string& quadVert) const;

private:
    const ShaderSource& assets_;
    ProgramBinaryCache* cache_;
    SceneHost host_;
};

}  // namespace geode::viz
