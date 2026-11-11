#include "viz/liquid/LiquidPass.hpp"

#include <algorithm>

#include "util/Log.hpp"
#include "viz/Framebuffer.hpp"
#include "viz/liquid/LiquidMath.hpp"

namespace geode::viz::liquid {

namespace {
constexpr const char* kTag = "GeodeLiquid";

GLuint makeTexture(int w, int h, bool halfFloat) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    if (halfFloat) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}
}  // namespace

bool LiquidPass::create(const ShaderSource& assets, ProgramBinaryCache* cache, const std::string& vert, bool halfFloat,
                        std::string* error) {
    release();
    const auto frag = assets.load("liquid_frag.glsl", error);
    if (!frag) return false;
    program_ = program::build(vert, *frag, cache, error);
    if (program_ == 0) return false;
    uniforms_ = UniformCache(program_);
    halfFloat_ = halfFloat;
    return true;
}

void LiquidPass::release() {
    releaseSide(sides_[0]);
    releaseSide(sides_[1]);
    if (program_ != 0) glDeleteProgram(program_);
    program_ = 0;
    width_ = height_ = 0;
    fresh_ = true;
}

bool LiquidPass::makeSide(Side& s) {
    s.liquid = makeTexture(width_, height_, halfFloat_);
    s.display = makeTexture(width_, height_, false);
    glGenFramebuffers(1, &s.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s.liquid, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, s.display, 0);
    const GLenum buffers[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, buffers);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (complete) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!complete) {
        GEODE_LOGW(kTag, "liquid target incomplete (%dx%d, half %d)", width_, height_, halfFloat_ ? 1 : 0);
        releaseSide(s);
    }
    return complete;
}

void LiquidPass::releaseSide(Side& s) {
    if (s.fbo != 0) glDeleteFramebuffers(1, &s.fbo);
    if (s.liquid != 0) glDeleteTextures(1, &s.liquid);
    if (s.display != 0) glDeleteTextures(1, &s.display);
    s = {};
}

bool LiquidPass::ensure(int width, int height) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (program_ == 0) return false;
    if (width == width_ && height == height_ && sides_[0].fbo != 0 && sides_[1].fbo != 0) return true;
    releaseSide(sides_[0]);
    releaseSide(sides_[1]);
    width_ = width;
    height_ = height;
    bool ok = makeSide(sides_[0]) && makeSide(sides_[1]);
    if (!ok && halfFloat_) {
        // A driver that probed half float renderable but will not attach it beside RGBA8.
        halfFloat_ = false;
        releaseSide(sides_[0]);
        releaseSide(sides_[1]);
        ok = makeSide(sides_[0]) && makeSide(sides_[1]);
    }
    if (!ok) {
        releaseSide(sides_[0]);
        releaseSide(sides_[1]);
        width_ = height_ = 0;
    }
    read_ = 0;
    fresh_ = true;
    return ok;
}

GLuint LiquidPass::run(const Frame& f) {
    if (program_ == 0 || sides_[0].fbo == 0 || sides_[1].fbo == 0) return 0;
    const int write = 1 - read_;
    Side& dst = sides_[write];
    glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
    // Every pixel is written below: a tile GPU must not load the old contents back first.
    Framebuffer::discardColorAttachments(GL_FRAMEBUFFER, 2);
    glViewport(0, 0, width_, height_);
    glDisable(GL_BLEND);
    glUseProgram(program_);
    auto bind = [this](const char* name, GLuint tex, int unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1i(uniforms_.loc(name), unit);
    };
    bind("uPrev", sides_[read_].liquid, 0);
    bind("uStyle", f.styleTex, 1);
    bind("uBorn", f.bornTex != 0 ? f.bornTex : f.styleTex, 2);
    bind("uVel", f.velocityTex, 3);
    glUniform2f(uniforms_.loc("uVelScale"), f.velScaleX, f.velScaleY);
    glUniform2f(uniforms_.loc("uTexel"), 1.0f / static_cast<float>(width_), 1.0f / static_cast<float>(height_));
    glUniform2f(uniforms_.loc("uStyleTexel"), 1.0f / static_cast<float>(std::max(f.styleWidth, 1)),
                1.0f / static_cast<float>(std::max(f.styleHeight, 1)));
    glUniform1f(uniforms_.loc("uDt"), f.dt);
    glUniform1f(uniforms_.loc("uAspect"), static_cast<float>(width_) / static_cast<float>(height_));
    glUniform2f(uniforms_.loc("uVp"), f.dive.vpX, f.dive.vpY);
    glUniform1f(uniforms_.loc("uScale"), f.dive.scale);
    glUniform1f(uniforms_.loc("uTwist"), f.dive.twist);
    // The first frame after a reset pours the whole style in, so the liquid starts as the style.
    glUniform1f(uniforms_.loc("uInject"), fresh_ ? 1.0f : f.inject);
    glUniform1f(uniforms_.loc("uEdge"), perFrameWeight(f.edge, f.dt));
    glUniform1f(uniforms_.loc("uSharpen"), f.sharpen);
    glUniform1f(uniforms_.loc("uBornR"), f.bornRadius);
    glUniform1f(uniforms_.loc("uBornGain"), fresh_ ? 0.0f : f.bornGain);
    glUniform1i(uniforms_.loc("uBornMode"), f.bornMode);
    glUniform1i(uniforms_.loc("uAge"), f.age);
    glUniform1i(uniforms_.loc("uSurface"), f.surface);
    glUniform1i(uniforms_.loc("uFeedback"), f.feedback ? 1 : 0);
    glUniform1f(uniforms_.loc("uShimmer"), f.shimmer * frames60(f.dt));
    glUniform1f(uniforms_.loc("uWarp"), f.warp * frames60(f.dt));
    glUniform1f(uniforms_.loc("uLight"), f.light);
    glUniform1f(uniforms_.loc("uTime"), f.time);
    glBindVertexArray(f.quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    for (int unit = 3; unit >= 0; unit--) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    read_ = write;
    fresh_ = false;
    return dst.display;
}

}  // namespace geode::viz::liquid
