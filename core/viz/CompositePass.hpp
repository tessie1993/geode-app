#pragma once
#include <GLES3/gl3.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "viz/Params.hpp"
#include "viz/Texture.hpp"
#include "viz/Transition.hpp"

namespace geode::viz {

// The frame's one opaque fullscreen pass over composite_frag.glsl.
//
// W00: underlay texture pass. A full-frame RGBA8 layer latched from any thread (Renderer::
// setUnderlayRgba) and uploaded here on the GL thread at the start of the next frame
// (Renderer::applyUnderlayUpload), converting from Android's ARGB int layout to GL's RGBA8 byte
// layout exactly once, on upload. The underlay sits UNDER/INTO the scene: draw() blends it into
// the finished composite colour (screen/multiply/add, by uUnderlayAmount) inside
// composite_frag.glsl, right before the dither - so it reads identically on the live view, the
// wallpaper and the frame-exact offscreen export, all of which share this same pass. It is read
// every frame via underlayTexOrZero()/underlayBlendMode()/underlayAmount() into
// CompositePass::Inputs, mirroring how Renderer::composite() already wires uFlow/uRipple.
// It costs nothing beyond the always-valid zeroTex_ until something is actually uploaded; nothing
// clears itself on its own after that - null clears it explicitly, matching every other Renderer
// setter.
class CompositePass {
public:
    static constexpr int kStyleLayer = 6;
    static constexpr float kDitherAmount = 1.0f / 255.0f;

    struct Inputs {
        GLuint texA = 0;
        GLuint texB = 0;
        GLuint flowTex = 0;
        float flowStrength = 0.0f;
        GLuint rippleTex = 0;
        float rippleTexelW = 0.0f;
        float rippleTexelH = 0.0f;
        float rippleStrength = 0.0f;
        float rippleSpecular = 0.0f;
        float progress = 1.0f;
        float layerMix = 0.5f;
        int blendOrdinal = 0;
        bool hasLayer = false;
        bool hasOutgoing = false;
        TransitionStyle transitionStyle = TransitionStyle::Fade;
        std::string transitionId = "fade";
        float ratio = 1.0f;
        float timeSeconds = 0.0f;
        float strobeHz = 0.0f;
        float postRotationAngle = 0.0f;
        float postCyclePhase = 0.0f;
        GLuint quadVao = 0;
        SceneParams fx;
        std::array<float, 4> gateA{};
        std::array<float, 4> gateB{};
        // CameraRig's flat projection and, per texture, whether to apply it.
        std::array<float, 4> camFlat{0.0f, 0.0f, 1.0f, 0.0f};
        std::array<float, 2> camTilt{};
        std::array<float, 2> camGate{};
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

    // W00: underlay upload, called on the GL thread only (Renderer::applyUnderlayUpload).
    // `argbPixels` is Android's Bitmap.getPixels layout (0xAARRGGBB per int); converted to GL's
    // RGBA8 byte order once here. width/height <= 0 or a short buffer is treated as a clear.
    void uploadUnderlay(const std::vector<uint32_t>& argbPixels, int width, int height, int blend, float amount);
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
