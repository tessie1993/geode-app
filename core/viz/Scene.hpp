#pragma once
#include <GLES3/gl3.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "viz/CameraRig.hpp"
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
    // sound: the renderer's sound frame with this scene's band trims applied (gainAdjusted).
    virtual void update(const GeodeSoundFrame& sound, float dt) = 0;
    virtual void draw(float timeSeconds) = 0;
    virtual void release() = 0;

    virtual void acceptPcm(const float* samples, int count) { (void) samples; (void) count; }
    virtual void setFlow(GLuint texture, float strength) { (void) texture; (void) strength; }
    virtual void setPaletteLut(GLuint texture) { (void) texture; }
    virtual void setTouchField(const TouchField* field) { (void) field; }
    // The renderer's camera rig pose, stable for the scene's lifetime; stepped once per frame before draw.
    virtual void setCamera(const CameraPose* pose) { (void) pose; }
    // True when the scene draws the rig's 3D pose itself, so the composite must not also apply its flat projection.
    virtual bool drawsCamera() const { return false; }
    virtual void setFragmentSource(const std::string& source) { (void) source; }
    // > 0 keeps the previous frame in the scene target (Curl Flow); 0 clears it.
    virtual float trailRetention(const SceneParams& params) const { (void) params; return 0.0f; }
    virtual GLuint velocityTexture() const { return 0; }
    // The fluid scene runs its own flow field; the water scene owns the ripples and the touch smears.
    virtual bool isFluid() const { return false; }
    virtual void setInjectionShaders(const std::string& forceSrc, const std::string& dyeSrc) { (void) forceSrc; (void) dyeSrc; }
    virtual bool isWater() const { return false; }
    virtual void queueTouchStroke(float nx, float ny, float ndx, float ndy, float dt, float strength) {
        (void) nx; (void) ny; (void) ndx; (void) ndy; (void) dt; (void) strength;
    }
    // The surface size in pixels, which MilkDrop renders at rather than the supersampled target.
    virtual void setWindowSize(int width, int height) { (void) width; (void) height; }
    virtual void queueMilkPreset(const std::string& path) { (void) path; }
    // The last preset that compiled anywhere in this renderer: what a rebuilt MilkDrop scene falls
    // back to when the preset it was asked for fails, rather than projectM's idle preset.
    virtual void setMilkFallbackPreset(const std::string& path) { (void) path; }
    virtual void reloadMilkPreset() {}
    virtual void setMilkTextureDir(const std::string& dir) { (void) dir; }
};

struct SceneHost {
    // "" clears the last error, as a successful compile does.
    std::function<void(const std::string&)> onShaderError;
    std::function<void(const std::string& sceneId, const std::string& source)> onUserSourceCompiled;
    // The rate the display is being asked for (0 = free-running), for the fluid quality ladder.
    std::function<float()> pacedFps;
    std::function<void(const std::string& path)> onMilkPresetLoaded;
    // Settings › Performance's march-step multiplier (0..1); unset means full budget.
    std::function<float()> marchScale;
};

}  // namespace geode::viz
