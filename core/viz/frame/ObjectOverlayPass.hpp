#pragma once
#include <GLES3/gl3.h>

#include <string>

#include "scene/object/ObjectOverlay.hpp"
#include "viz/CameraRig.hpp"
#include "viz/Program.hpp"
#include "viz/ShaderSource.hpp"
#include "viz/frame/Pass.hpp"

namespace geode::scene {
class Scene;
}

namespace geode::viz::frame {

// The scene objects' overlay (#515, spec #511): the live SDF shapes of the drawn style's
// geode::scene::Scene, raymarched in one full-screen pass (scene_objects_frag on lib_objects) and blended
// over the world after the world is drawn.
//
// WHERE: Renderer::drawSceneTarget, straight after the active scene's draw, into the scene target, so
// the trail, Liquid mode, the composite's grade and gate, the flash limiter's probe and every surface
// (live, wallpaper, dream, Cast and the export's OffscreenSceneRenderer: the same Renderer path) see the
// shapes exactly as they see the world. Only a style whose Scene has live shapes draws it (wants()); a
// crossfade's outgoing style and the layer slot keep their objects stepping but undrawn.
//
// THE ACCEPTED LIMIT (#302 § Rendering): the shapes never go behind world geometry and never into its
// fog: the pass has no depth from the world, it only blends over it. The world's fog colours a far shape
// a little, never hides it.
//
// CPU and GL split as frame::Pass asks: prepare() packs the instance block on the CPU through the pure
// object::packOverlay (scene_object_test checks the count, transforms and cap it produces), draw() only
// uploads it (one uniform buffer, std140, binding kBinding) and draws one triangle.
class ObjectOverlayPass final : public Pass {
public:
    // The uniform buffer binding point the pass binds its block to. No other pass uses uniform buffers.
    static constexpr GLuint kBinding = 1;
    // The texture unit it binds the palette LUT on (the styles' own choice of 2, for the same LUT).
    static constexpr GLuint kLutUnit = 2;

    ObjectOverlayPass() = default;
    ~ObjectOverlayPass() override { release(); }
    ObjectOverlayPass(const ObjectOverlayPass&) = delete;
    ObjectOverlayPass& operator=(const ObjectOverlayPass&) = delete;

    // Beyond the Pass interface, as EdgeGlowPass: building the program needs the renderer's assets,
    // cache and shared vertex shader. Called on a fresh context, after release().
    bool create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& fadeVert, std::string* error);
    // This frame's scene (null: nothing to draw), the camera it was stepped with and the palette LUT.
    // Handed over by the renderer on the GL thread before wants().
    void setFrame(const ::geode::scene::Scene* scene, const CameraPose* camera, GLuint paletteLut);

    bool wants(const FrameCtx& ctx) const override;
    void prepare(const FrameCtx& ctx) override;
    void draw(const PassInputs& inputs, const PassTarget& target) override;
    void release() override;

    const ::geode::scene::object::OverlayFrame& frame() const { return frame_; }

private:
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint ubo_ = 0;
    UniformCache uniforms_;
    const ::geode::scene::Scene* scene_ = nullptr;
    const CameraPose* camera_ = nullptr;
    GLuint paletteLut_ = 0;
    ::geode::scene::object::OverlayFrame frame_{};
    bool prepared_ = false;
};

}  // namespace geode::viz::frame
