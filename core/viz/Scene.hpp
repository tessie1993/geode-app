#pragma once
#include <GLES3/gl3.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "viz/CameraRig.hpp"
#include "viz/NoteResponse.hpp"
#include "viz/Params.hpp"
#include "viz/ShapeGraph.hpp"
#include "viz/TouchField.hpp"

namespace geode::viz {

enum class SceneFamily { Shader, Milkdrop, Fluid };

// A scene, plus the optional hooks the renderer wires by capability rather than by class.
class Scene {
public:
    virtual ~Scene() = default;
    virtual const std::string& id() const = 0;
    virtual SceneFamily family() const = 0;
    virtual void init() = 0;
    // After init and the renderer's setup: builds whatever the first draw() would otherwise build,
    // so a staged build (ScenePrep) sees every program the scene needs before it is shown.
    virtual void prepare() {}
    // After prepare(): true when the scene has nothing to draw because its program failed to build.
    // The renderer then drops it and keeps the scene it had (ScenePrep::fail).
    virtual bool buildFailed() const { return false; }
    virtual void setParams(const SceneParams& params) = 0;
    virtual void resize(int width, int height) = 0;
    // sound: the renderer's sound frame with this scene's band trims applied (gainAdjusted).
    virtual void update(const GeodeSoundFrame& sound, float dt) = 0;
    virtual void draw(float timeSeconds) = 0;
    virtual void release() = 0;

    virtual void acceptPcm(const float* samples, int count) { (void) samples; (void) count; }
    // The same block with its side signal ((L - R) / 2, zero for a mono source), for a scene that draws
    // left against right. Every other scene takes the mono mix.
    virtual void acceptPcmStereo(const float* mono, const float* side, int count) {
        (void) side;
        acceptPcm(mono, count);
    }
    virtual void setPaletteLut(GLuint texture) { (void) texture; }
    virtual void setTouchField(const TouchField* field) { (void) field; }
    // The renderer's camera rig pose, stable for the scene's lifetime; stepped once per frame before draw.
    virtual void setCamera(const CameraPose* pose) { (void) pose; }
    // True when the scene draws the rig's 3D pose itself, so the composite must not also apply its flat projection.
    virtual bool drawsCamera() const { return false; }
    // A held scene (Renderer::holds) that draws the rig itself, when the rig moved since the frame it holds:
    // draw what it holds again from the new pose without stepping it, so the eye never freezes with the field.
    virtual void redrawHeld() {}
    virtual void setFragmentSource(const std::string& source) { (void) source; }
    // > 0 keeps the previous frame in the scene target (Curl Flow); 0 clears it.
    virtual float trailRetention(const SceneParams& params) const { (void) params; return 0.0f; }
    virtual GLuint velocityTexture() const { return 0; }
    // The fluid scene runs its own flow field.
    virtual bool isFluid() const { return false; }
    virtual void setInjectionShaders(const std::string& forceSrc, const std::string& dyeSrc) { (void) forceSrc; (void) dyeSrc; }
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
    // The rate the display is being asked for (0 = free-running), for the fluid quality ladder.
    std::function<float()> pacedFps;
    std::function<void(const std::string& path)> onMilkPresetLoaded;
    // Settings › Performance's march-step multiplier (0..1); unset means full budget.
    std::function<float()> marchScale;
    // Note & key reactions (viz/NoteResponse): this frame's style knobs (uKnob). Unset (or the layer off)
    // leaves every knob at 0.5, the midpoint each style was tuned around.
    std::function<const NoteUniforms*()> notes;
    // The play's seed: the auto-director's (viz/Director), or the one an export pinned, so a scene
    // that draws its own choices from it (Morph World's worlds) replays the film the screen showed.
    // Unset reads as 0.
    std::function<uint64_t()> playSeed;
    // Whether this renderer draws an export (geode_viz_set_offscreen): its frames are on the render clock,
    // so nothing in a scene may wait on the wall clock. Unset reads as live.
    std::function<bool()> offscreen;
    // A style is being built or blended in (ThermalGovernor::settling): slow frames are not this scene's.
    std::function<bool()> settling;
    // The look's palette stack as a kRampSize × 1 texture (viz/Palette), 0 while the look keeps none and
    // its old colour params draw.
    std::function<GLuint()> paletteRamp;
    // The shape graph (viz/ShapeGraph) as this frame uploads it: the renderer ticks one graph once a frame after
    // its params resolve, so every scene it draws (the style, the one leaving, the layer) melts in the same shapes,
    // and live, an export, the wallpaper and the dream each draw their own renderer's. Unset reads as an empty graph.
    std::function<const shapes::Packed*()> shapeGraph;
};

}  // namespace geode::viz
