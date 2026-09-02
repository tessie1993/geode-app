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
    // The fluid scene runs its own flow field; the water scene owns the ripples and the touch smears.
    virtual bool isFluid() const { return false; }
    virtual void setInjectionShaders(const std::string& forceSrc, const std::string& dyeSrc) { (void) forceSrc; (void) dyeSrc; }
    virtual bool isWater() const { return false; }
    virtual void queueTouchStroke(float nx, float ny, float ndx, float ndy, float dt, float strength) {
        (void) nx; (void) ny; (void) ndx; (void) ndy; (void) dt; (void) strength;
    }
};

struct SceneHost {
    // "" clears the last error, as a successful compile does.
    std::function<void(const std::string&)> onShaderError;
    std::function<void(const std::string& sceneId, const std::string& source)> onUserSourceCompiled;
    // The rate the display is being asked for (0 = free-running), for the fluid quality ladder.
    std::function<float()> pacedFps;
};

}  // namespace geode::viz
