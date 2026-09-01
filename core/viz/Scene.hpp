#pragma once
#include <GLES3/gl3.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "viz/Params.hpp"
#include "viz/TouchField.hpp"

namespace geode::viz {

enum class SceneFamily { Shader, Milkdrop, Fluid };

// Port of Scene.kt plus the optional hooks the renderer wires by capability rather than by class.
class Scene {
public:
    virtual ~Scene() = default;
    virtual const std::string& id() const = 0;
    virtual SceneFamily family() const = 0;
    virtual void init() = 0;
    virtual void setParams(const SceneParams& params) = 0;
    virtual void resize(int width, int height) = 0;
    virtual void update(const GeodeFeatureFrame& features, float dt) = 0;
    virtual void draw(float timeSeconds) = 0;
    virtual void release() = 0;

    virtual void acceptPcm(const float* samples, int count) { (void) samples; (void) count; }
    virtual void setFlow(GLuint texture, float strength) { (void) texture; (void) strength; }
    virtual void setPaletteLut(GLuint texture) { (void) texture; }
    virtual void setTouchField(const TouchField* field) { (void) field; }
    virtual void setFragmentSource(const std::string& source) { (void) source; }
    // > 0 keeps the previous frame in the scene target (Curl Flow, Beam); 0 clears it.
    virtual float trailRetention(const SceneParams& params) const { (void) params; return 0.0f; }
    virtual GLuint velocityTexture() const { return 0; }
};

struct SceneHost {
    std::function<void(const std::string&)> onShaderError;
    std::function<void(const std::string&)> onUserSourceCompiled;
};

}  // namespace geode::viz
