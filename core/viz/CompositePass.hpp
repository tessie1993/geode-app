#pragma once
#include <GLES3/gl3.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "viz/Births.hpp"
#include "viz/Params.hpp"
#include "viz/Texture.hpp"
#include "viz/Transition.hpp"

namespace geode::viz {

// The frame's one opaque fullscreen pass over composite_frag.glsl.
//
// W00: underlay texture pass. A full-frame RGBA8 layer latched from any thread (Renderer::
// setUnderlayRgba) and uploaded here on the GL thread at the start of the next frame
// (Renderer::applyUnderlayUpload), as the RGBA8 bytes Renderer::setUnderlayRgba converted Android's
// ARGB ints to once, when the image was set (#999). The underlay sits UNDER/INTO the scene: draw() blends it into
// the finished composite colour (screen/multiply/add, by uUnderlayAmount) inside
// composite_frag.glsl, right before the dither - so it reads identically on the live view, the
// wallpaper and the frame-exact offscreen export, all of which share this same pass. It is read
// every frame via underlayTexOrZero()/underlayBlendMode()/underlayAmount() into
// CompositePass::Inputs, mirroring how Renderer::composite() already wires uFlow.
// It costs nothing beyond the always-valid zeroTex_ until something is actually uploaded; nothing
// clears itself on its own after that - null clears it explicitly, matching every other Renderer
// setter.
class CompositePass {
public:
    static constexpr float kDitherAmount = 1.0f / 255.0f;

    struct Inputs {
        GLuint texA = 0;
        GLuint texB = 0;
        // Customize › Layers: the layer's own target, blended over the transition's result (zeroTex when none).
        GLuint texL = 0;
        GLuint flowTex = 0;
        float flowStrength = 0.0f;
        float progress = 1.0f;
        float layerMix = 0.5f;
        int blendOrdinal = 0;
        bool hasLayer = false;
        bool hasOutgoing = false;
        TransitionStyle transitionStyle = TransitionStyle::Fade;
        std::string transitionId = "fade";
        float ratio = 1.0f;
        float timeSeconds = 0.0f;
        float postRotationAngle = 0.0f;
        // Life's births (viz/Births) for a scene that has no shader to draw them (gate x): all 0 at Life 0.
        std::array<float, births::kFloats> postBirths{};
        GLuint quadVao = 0;
        SceneParams fx;
        std::array<float, 4> gateA{};
        std::array<float, 4> gateB{};
        std::array<float, 4> gateL{};
        // CameraRig's flat projection and, per texture, whether to apply it.
        std::array<float, 4> camFlat{0.0f, 0.0f, 1.0f, 0.0f};
        std::array<float, 2> camTilt{};
        std::array<float, 2> camGate{};
        float camGateL = 0.0f;
        // W00: underlay, read each frame from CompositePass's own latched texture (see the class
        // comment above) via underlayTexOrZero()/underlayBlendMode()/underlayAmount().
        GLuint underlayTex = 0;
        int underlayBlend = 0;
        float underlayAmount = 0.0f;
    };

    CompositePass(const ShaderSource& assets, ProgramBinaryCache* cache) : transitions_(assets, cache), assets_(assets) {}
    ~CompositePass() { release(); }

    bool create(const std::string& fadeVert, std::string* error);
    void release();
    void releaseStaleTextures();
    GLuint zeroTex() const { return zeroTex_; }
    void warmTransition(const std::string& id) { transitions_.warm(id); }
    TransitionCatalog& catalog() { return transitions_.catalog(); }
    void draw(const Inputs& inputs);

    // W00: underlay upload, called on the GL thread only (Renderer::applyUnderlayUpload). `rgba` is
    // width * height RGBA8 pixels, uploaded straight from the renderer's retained buffer, no copy.
    // width/height <= 0 or a short buffer is treated as a clear.
    void uploadUnderlay(const std::vector<uint8_t>& rgba, int width, int height, int blend, float amount);
    void clearUnderlay();
    GLuint underlayTexOrZero() const { return underlayTex_.ok() ? underlayTex_.id() : zeroTex_; }
    int underlayBlendMode() const { return underlayBlendMode_; }
    float underlayAmount() const { return underlayAmount_; }

private:
    GLuint createZeroTexture();
    GLuint createNoiseTexture();
    void bindTextures(UniformCache& p, const Inputs& inputs);
    void uploadFrameUniforms(UniformCache& p, const Inputs& inputs, const TransitionCatalog::Def* definition);
    void uploadGradeUniforms(UniformCache& p, const Inputs& inputs);

    TransitionPrograms transitions_;
    const ShaderSource& assets_;
    GLuint noiseTex_ = 0;
    GLuint zeroTex_ = 0;
    // W00 state, see the class comment above.
    Texture underlayTex_;
    int underlayBlendMode_ = 0;
    float underlayAmount_ = 0.0f;
};

}  // namespace geode::viz
