#pragma once
#include <GLES3/gl3.h>

#include <cstdint>
#include <string>
#include <vector>

#include "viz/Program.hpp"
#include "viz/Quad.hpp"
#include "viz/ShaderSource.hpp"
#include "viz/Texture.hpp"
#include "viz/Transition.hpp"
#include "viz/frame/Pass.hpp"

// The export post-passes (#442): the non-Pro watermark, a loop render's seam, and a Studio render's GL
// Transition with the capture that feeds it. They were Kotlin GL (export/WatermarkOverlay.kt, LoopRender's
// SeamStash, export/GlTransitionEffect.kt); now each is a frame::Pass on Surface::Export, driven through
// geode_export_* in core/api/geode_api.h, and Kotlin keeps only what has to be Android: the wordmark's
// Bitmap raster, MediaCodec and EGL, and Media3's GlShaderProgram shell.
//
// Thread: whichever thread has the export's GL context current. For the watermark and the seam that is the
// export thread with EncoderSurface's context (VideoExporter, LoopRender); for the transition and the
// capture it is Media3 Transformer's GL thread, inside its own framebuffer and viewport. None of them owns
// the export clock: they draw the frame they are handed, when they are handed it.
//
// GL state: each pass sets what it needs and puts back what a caller relies on (the program, blending, the
// vertex array, texture unit 0), because both callers draw on after it: the encoder's next scene frame and
// Media3's own programs, which use client-side vertex arrays and so need GL_ARRAY_BUFFER and the vertex
// array object unbound.
//
// The maths (placement, weights, progress) is ExportPassMath.hpp, tested on the host.
namespace geode::viz::frame {

// The free tier's wordmark tiled diagonally across the whole frame, over the finished scene in the
// encoder's default framebuffer. One tile is uploaded once (Kotlin rasterises it: text needs Android's
// Paint); per frame it is one full-frame draw whose texture coordinates repeat that tile.
class WatermarkPass final : public Pass {
public:
    ~WatermarkPass() override { release(); }

    // `rgba` is the tile, tileWidth × tileHeight premultiplied RGBA8, top row first (Bitmap order).
    bool create(const ShaderSource& assets, const void* rgba, int tileWidth, int tileHeight, std::string* error);

    bool wants(const FrameCtx& ctx) const override;
    void prepare(const FrameCtx& ctx) override;
    void draw(const PassInputs& inputs, const PassTarget& target) override;
    void release() override;

private:
    GLuint program_ = 0;
    UniformCache uniforms_;
    Texture tile_;
    FullscreenTriangle triangle_;
    int tileWidth_ = 0;
    int tileHeight_ = 0;
    float tilesU_ = 0.0f;
    float tilesV_ = 0.0f;
};

// A loop render's seam: the loop's opening frames copied straight off the encoder surface into textures,
// then laid back over its closing frames at a rising alpha, so the last frame dissolves into the first. No
// pixel crosses to the CPU. One full-resolution texture per seam frame, so the stash takes what the driver
// gives it (allocate) and the render shortens the seam to that.
class SeamPass final : public Pass {
public:
    ~SeamPass() override { release(); }

    bool create(const ShaderSource& assets, int width, int height, std::string* error);
    // Up to `requested` frames of stash; returns how many fitted (0 when none did).
    int allocate(int requested);
    int frames() const { return static_cast<int>(textures_.size()); }

    // What the next draw does: copy the target into stash frame `index`, or lay stash frame `index` over
    // the target at closing frame `tail` of `seamFrames` (the weight is exportmath::seam::weight).
    void stageCapture(int index);
    void stageBlend(int index, int tail, int seamFrames);

    bool wants(const FrameCtx& ctx) const override;
    void prepare(const FrameCtx& ctx) override;
    void draw(const PassInputs& inputs, const PassTarget& target) override;
    void release() override;

private:
    enum class Op { None, Capture, Blend };

    bool allocateOne(GLuint id) const;

    GLuint program_ = 0;
    UniformCache uniforms_;
    FullscreenTriangle triangle_;
    std::vector<GLuint> textures_;
    int width_ = 0;
    int height_ = 0;
    Op op_ = Op::None;
    int index_ = 0;
    int tail_ = 0;
    int seamFrames_ = 0;
    float alpha_ = 0.0f;
};

// The frame a Studio clip ended on, kept for the GL Transition that opens the next clip. Each frame is
// drawn through unchanged and copied into one texture; at the clip's end the last one is read back
// (readBack), because the next clip's program may live in another context.
class CapturePass final : public Pass {
public:
    ~CapturePass() override { release(); }

    bool create(const ShaderSource& assets, std::string* error);
    // The clip's frame size; reallocates the copy only when it changes.
    void configure(int width, int height);
    // The clip's frame for the next draw (PassInputs::sceneTex carries it there).
    bool wants(const FrameCtx& ctx) const override;
    void prepare(const FrameCtx& ctx) override;
    void draw(const PassInputs& inputs, const PassTarget& target) override;
    void release() override;

    int width() const { return latest_.width(); }
    int height() const { return latest_.height(); }
    // The last frame copied, width × height RGBA8 bottom row first; false when nothing was or it does not fit.
    bool readBack(void* out, size_t capacity) const;

private:
    GLuint program_ = 0;
    UniformCache uniforms_;
    FullscreenTriangle triangle_;
    Texture latest_;
    bool captured_ = false;
};

// A GL Transition opening a Studio clip: from the previous clip's captured frame to the clip's own picture,
// over a duration from the clip's first frame. Two inputs, so the outgoing frame comes in through its own
// setter (setFrom) and the incoming one as PassInputs::sceneTex; without an outgoing frame, an unknown
// transition id, or past the end, it draws the clip unchanged.
class TransitionPass final : public Pass {
public:
    ~TransitionPass() override { release(); }

    // `id` is a gl_transitions.json name (TransitionCatalog); a built-in or unknown id builds only the copy.
    bool create(const ShaderSource& assets, const std::string& id, int64_t durationUs, std::string* error);
    void configure(int width, int height);
    // The outgoing frame, RGBA8 bottom row first; taken once, as the Kotlin effect took it.
    void setFrom(const void* rgba, int width, int height);
    // The presentation time of the next draw; the first one seen is the transition's origin.
    void setPresentationUs(int64_t ptsUs);

    bool wants(const FrameCtx& ctx) const override;
    void prepare(const FrameCtx& ctx) override;
    void draw(const PassInputs& inputs, const PassTarget& target) override;
    void release() override;

private:
    GLuint copyProgram_ = 0;
    GLuint blendProgram_ = 0;
    UniformCache copyUniforms_;
    UniformCache blendUniforms_;
    FullscreenTriangle triangle_;
    Texture from_;
    std::string id_;
    int64_t durationUs_ = 0;
    int64_t originUs_ = -1;
    int64_t ptsUs_ = 0;
    float ratio_ = 1.0f;
    float progress_ = 1.0f;
    // The transition's definition; its parameters are uploaded once, after the blend program links.
    TransitionCatalog::Def def_;
};

}  // namespace geode::viz::frame
