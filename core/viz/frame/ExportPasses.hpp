#pragma once
#include <GLES3/gl3.h>

#include <cstdint>
#include <string>
#include <vector>

#include "viz/Program.hpp"
#include "viz/Quad.hpp"
#include "viz/ShaderSource.hpp"
#include "viz/Texture.hpp"
#include "viz/frame/Pass.hpp"

// The export post-passes (#442): the non-Pro watermark and a loop render's seam. They were Kotlin GL
// (export/WatermarkOverlay.kt, LoopRender's SeamStash); now each is a frame::Pass on Surface::Export,
// driven through geode_export_* in core/api/geode_api.h, and Kotlin keeps only what has to be Android: the
// wordmark's Bitmap raster, MediaCodec and EGL. (A Studio render's GL Transition and its capture pass went
// with the Studio project render, ADR 0007.)
//
// Thread: the export thread with EncoderSurface's context current (VideoExporter, LoopRender). Neither
// pass owns the export clock: they draw the frame they are handed, when they are handed it.
//
// GL state: each pass sets what it needs and puts back what the caller relies on (the program, blending,
// the vertex array, texture unit 0), because the encoder's next scene frame draws on after it.
//
// The maths (placement, weights) is ExportPassMath.hpp, tested on the host.
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

}  // namespace geode::viz::frame
