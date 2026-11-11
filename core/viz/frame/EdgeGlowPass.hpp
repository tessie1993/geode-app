#pragma once
#include <GLES3/gl3.h>

#include <string>

#include "viz/Program.hpp"
#include "viz/ShaderSource.hpp"
#include "viz/frame/EdgeGlowMath.hpp"
#include "viz/frame/Pass.hpp"

namespace geode::viz::frame {

// The wallpaper's edge look as a frame Pass (#439; the look and its maths are in EdgeGlowMath.hpp). It was
// Kotlin GL in wallpaper/EdgeGlow.kt, a second program drawn after the visual that had to save and restore
// the renderer's GL state and read the PCM ring itself for its level. Native, it runs inside the frame like
// any other pass, hears the frame's own sound engine, and the shell only hands over its options.
//
// Drawn by Renderer::render after the composite, into the target framebuffer. Only while the shell has turned it on
// (geode_viz_set_edge_glow; the wallpaper is the only caller) and never offscreen: an export is never a
// wallpaper. Cost: one full-screen triangle of arithmetic, no texture reads.
class EdgeGlowPass final : public Pass {
public:
    EdgeGlowPass() = default;
    ~EdgeGlowPass() override { release(); }
    EdgeGlowPass(const EdgeGlowPass&) = delete;
    EdgeGlowPass& operator=(const EdgeGlowPass&) = delete;

    // Beyond the Pass interface: building the program needs the renderer's assets, cache and shared vertex
    // shader, which a FrameCtx does not carry. Called on a fresh context, after release().
    bool create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& fadeVert, std::string* error);
    // The latest options from the shell, handed over by the renderer on the GL thread each frame. Turning
    // the glow off and on again starts the envelope from rest.
    void setOptions(const edge_glow::Options& options);

    bool wants(const FrameCtx& ctx) const override;
    void prepare(const FrameCtx& ctx) override;
    void draw(const PassInputs& inputs, const PassTarget& target) override;
    void release() override;

    float level() const { return envelope_.level; }

private:
    GLuint program_ = 0;
    GLuint vao_ = 0;
    UniformCache uniforms_;
    edge_glow::Options options_;
    edge_glow::Envelope envelope_;
    edge_glow::Shape shape_;
    bool prepared_ = false;
};

}  // namespace geode::viz::frame
