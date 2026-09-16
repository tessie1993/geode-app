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

// Port of CompositePass.kt: the frame's one opaque fullscreen pass over composite_frag.glsl.
//
// W00: overlay/underlay texture pass. Full-frame RGBA8 layers latched from any thread (Renderer::
// setOverlayRgba/setUnderlayRgba) and uploaded here on the GL thread at the start of the next
// frame (Renderer::applyOverlayUploads), converting from Android's ARGB int layout to GL's RGBA8
// byte layout exactly once, on upload.
//   - Underlay sits UNDER/INTO the scene: draw() blends it into the finished composite colour
//     (screen/multiply/add, by uUnderlayAmount) inside composite_frag.glsl, right before the
//     dither - so it reads identically on the live view, the wallpaper and the frame-exact
//     offscreen export, all of which share this same pass. Read every frame via
//     underlayTexOrZero()/underlayBlendMode()/underlayAmount() into CompositePass::Inputs,
//     mirroring how Renderer::composite() already wires uFlow/uRipple.
//   - Overlay is drawn LAST, as its own textured-quad draw call (drawOverlay()) straight over the
//     finished output, premultiplied-alpha blended (GL_ONE, GL_ONE_MINUS_SRC_ALPHA) so cover art,
//     text, lyrics and logo layers can sit above every post-FX and transition without being
//     graded by them.
// Both are zero-cost no-ops (no extra draw call, no texture bound beyond the always-valid
// zeroTex_) until something is actually uploaded; nothing clears itself on its own after that -
// null clears it explicitly, matching every other Renderer setter.
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
        float hitImpulse = 0.0f;
        float flash = 0.0f;
        float strobeHz = 0.0f;
        float postRotationAngle = 0.0f;
        float postCyclePhase = 0.0f;
        float postBeatPulse = 0.0f;
        GLuint quadVao = 0;
        SceneParams fx;
        std::array<float, 4> gateA{};
        std::array<float, 4> gateB{};
        // W00: underlay, read each frame from CompositePass's own latched texture (see the class
        // comment above) via underlayTexOrZero()/underlayBlendMode()/underlayAmount().
        GLuint underlayTex = 0;
        int underlayBlend = 0;
        float underlayAmount = 0.0f;
    };

    CompositePass(const ShaderSource& assets, ProgramBinaryCache* cache) : transitions_(assets, cache), assets_(assets), cache_(cache) {}
    ~CompositePass() { release(); }

    bool create(const std::string& fadeVert, std::string* error);
    void release();
    void releaseStaleTextures();
    GLuint zeroTex() const { return zeroTex_; }
    void warmTransition(const std::string& id) { transitions_.warm(id); }
    TransitionCatalog& catalog() { return transitions_.catalog(); }
    void draw(const Inputs& inputs);

    // W00: overlay/underlay uploads, called on the GL thread only (Renderer::applyOverlayUploads).
    // `argbPixels` is Android's Bitmap.getPixels layout (0xAARRGGBB per int); converted to GL's
    // RGBA8 byte order once here. width/height <= 0 or a short buffer is treated as a clear.
    void uploadOverlay(const std::vector<uint32_t>& argbPixels, int width, int height);
    void clearOverlay();
    void uploadUnderlay(const std::vector<uint32_t>& argbPixels, int width, int height, int blend, float amount);
    void clearUnderlay();
    // Draws the latched overlay, premultiplied-alpha blended, over whatever is already bound as
    // the draw framebuffer; a no-op when no overlay is set. Call AFTER draw() in the same frame.
    void drawOverlay(GLuint quadVao);
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
    ProgramBinaryCache* cache_;
    GLuint noiseTex_ = 0;
    GLuint zeroTex_ = 0;
    // W00 state, see the class comment above.
    GLuint overlayProgram_ = 0;
    UniformCache overlayUniforms_;
    Texture overlayTex_;
    Texture underlayTex_;
    int underlayBlendMode_ = 0;
    float underlayAmount_ = 0.0f;
};

}  // namespace geode::viz
