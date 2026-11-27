#include "viz/liquid/LiquidPass.hpp"

#include <algorithm>

#include "util/Log.hpp"
#include "viz/gl/Framebuffer.hpp"
#include "viz/liquid/LiquidMath.hpp"

namespace geode::viz::liquid {

namespace {
constexpr const char* kTag = "GeodeLiquid";
// liquid_frag.glsl's texture units; run() unbinds 0..kUnitVel after its draw.
constexpr int kUnitPrev = 0;
constexpr int kUnitStyle = 1;
constexpr int kUnitBorn = 2;
constexpr int kUnitVel = 3;

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

// The bound framebuffer's draw buffers: the liquid, and the display beside it when there is one.
void drawBuffersFor(bool display) {
    const GLenum buffers[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(display ? 2 : 1, buffers);
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
    // The samplers' units, once: a uniform keeps its value until the program is linked again.
    glUseProgram(program_);
    glUniform1i(uniforms_.loc("uPrev"), kUnitPrev);
    glUniform1i(uniforms_.loc("uStyle"), kUnitStyle);
    glUniform1i(uniforms_.loc("uBorn"), kUnitBorn);
    glUniform1i(uniforms_.loc("uVel"), kUnitVel);
    halfFloat_ = halfFloat;
    return true;
}

void LiquidPass::release() {
    releaseSides();
    if (program_ != 0) glDeleteProgram(program_);
    program_ = 0;
    width_ = height_ = 0;
    fresh_ = true;
}

bool LiquidPass::makeSide(Side& s) {
    s.liquid = makeTexture(width_, height_, halfFloat_);
    glGenFramebuffers(1, &s.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s.liquid, 0);
    if (display_ != 0) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, display_, 0);
    drawBuffersFor(display_ != 0);
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
    s = {};
}

void LiquidPass::releaseSides() {
    releaseSide(sides_[0]);
    releaseSide(sides_[1]);
    if (display_ != 0) glDeleteTextures(1, &display_);
    display_ = 0;
}

void LiquidPass::releaseTargets() {
    releaseSides();
    width_ = height_ = 0;
    read_ = 0;
    fresh_ = true;
}

bool LiquidPass::setDisplay(bool display) {
    const GLuint old = display_;
    display_ = display ? makeTexture(width_, height_, false) : 0;
    bool complete = true;
    for (Side& s : sides_) {
        glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, display_, 0);
        drawBuffersFor(display);
        complete = complete && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (old != 0) glDeleteTextures(1, &old);
    return complete;
}

bool LiquidPass::ensure(int width, int height, bool display) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (program_ == 0) return false;
    const bool sized = width == width_ && height == height_ && sides_[0].fbo != 0 && sides_[1].fbo != 0;
    if (sized && (display_ != 0) == display) return true;
    // Liquid mode and Trails trade the display at the same size, and the liquid carries on in both sides.
    if (sized && setDisplay(display)) return true;
    releaseSides();
    width_ = width;
    height_ = height;
    if (display) display_ = makeTexture(width_, height_, false);
    bool ok = makeSide(sides_[0]) && makeSide(sides_[1]);
    if (!ok && halfFloat_) {
        // A driver that probed half float renderable but will not attach it beside RGBA8.
        halfFloat_ = false;
        releaseSide(sides_[0]);
        releaseSide(sides_[1]);
        ok = makeSide(sides_[0]) && makeSide(sides_[1]);
    }
    if (!ok) {
        releaseSides();
        width_ = height_ = 0;
    }
    read_ = 0;
    fresh_ = true;
    return ok;
}

GLuint LiquidPass::run(const Frame& f) {
    if (program_ == 0 || sides_[0].fbo == 0 || sides_[1].fbo == 0) return 0;
    // A stopped clock holds the picture: another pass would sharpen and fade it again with nothing moving.
    if (!(f.dt > 0.0f) && !fresh_) return shown(read_);
    const int write = 1 - read_;
    Side& dst = sides_[write];
    glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
    // Every pixel is written below: a tile GPU must not load the old contents back first.
    Framebuffer::discardColorAttachments(GL_FRAMEBUFFER, display_ != 0 ? 2 : 1);
    glViewport(0, 0, width_, height_);
    glDisable(GL_BLEND);
    glUseProgram(program_);
    // Each sampler's unit was set at create().
    auto bind = [](int unit, GLuint tex) {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, tex);
    };
    bind(kUnitPrev, sides_[read_].liquid);
    bind(kUnitStyle, f.styleTex);
    bind(kUnitBorn, f.bornTex != 0 ? f.bornTex : f.styleTex);
    bind(kUnitVel, f.velocityTex);
    glUniform2f(uniforms_.loc("uVelScale"), f.velScaleX, f.velScaleY);
    glUniform2f(uniforms_.loc("uTexel"), 1.0f / static_cast<float>(width_), 1.0f / static_cast<float>(height_));
    glUniform2f(uniforms_.loc("uStyleTexel"), 1.0f / static_cast<float>(std::max(f.styleWidth, 1)),
                1.0f / static_cast<float>(std::max(f.styleHeight, 1)));
    glUniform1f(uniforms_.loc("uDt"), f.dt);
    glUniform1f(uniforms_.loc("uAspect"), static_cast<float>(width_) / static_cast<float>(height_));
    glUniform2f(uniforms_.loc("uVp"), f.vpX, f.vpY);
    const motion::FrameTerms& m = f.terms;
    glUniform1i(uniforms_.loc("uMotionFlags"), m.flags);
    glUniform1f(uniforms_.loc("uZoom"), m.zoom);
    glUniform1f(uniforms_.loc("uZoomExp"), m.zoomExp);
    glUniform1f(uniforms_.loc("uCx"), m.cx);
    glUniform1f(uniforms_.loc("uCy"), m.cy);
    glUniform1f(uniforms_.loc("uDx"), m.dx);
    glUniform1f(uniforms_.loc("uDy"), m.dy);
    glUniform1f(uniforms_.loc("uSx"), m.sx);
    glUniform1f(uniforms_.loc("uSy"), m.sy);
    glUniform2f(uniforms_.loc("uRotCS"), m.rotCos, m.rotSin);
    glUniform1f(uniforms_.loc("uWarp"), m.warp);
    glUniform1f(uniforms_.loc("uWarpInvScale"), m.warpInvScale);
    glUniform4fv(uniforms_.loc("uWarpFreq"), 1, m.warpFreq);
    glUniform4fv(uniforms_.loc("uWarpPhase"), 1, m.warpPhase);
    // The first frame after a reset pours the whole style in, so the liquid starts as the style.
    glUniform1f(uniforms_.loc("uInject"), fresh_ ? 1.0f : f.inject);
    glUniform1f(uniforms_.loc("uEdge"), perFrameWeight(f.edge, f.dt));
    glUniform1f(uniforms_.loc("uSharpen"), f.sharpen);
    glUniform1f(uniforms_.loc("uBornR"), f.bornRadius);
    const float bornGain = fresh_ ? 0.0f : f.bornGain;
    glUniform1f(uniforms_.loc("uBornGain"), bornGain);
    glUniform1i(uniforms_.loc("uPassFlags"), passFlags(f.flowing, bornGain));
    glUniform1i(uniforms_.loc("uBornMode"), f.bornMode);
    glUniform1i(uniforms_.loc("uAge"), f.age);
    glUniform1i(uniforms_.loc("uSurface"), f.surface);
    glUniform1i(uniforms_.loc("uFeedback"), f.feedback ? 1 : 0);
    glUniform1f(uniforms_.loc("uShimmer"), f.shimmer * frames60(f.dt));
    glUniform1f(uniforms_.loc("uLight"), f.light);
    glUniform1f(uniforms_.loc("uTime"), f.time);
    glBindVertexArray(f.quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    for (int unit = kUnitVel; unit >= 0; unit--) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    read_ = write;
    fresh_ = false;
    return shown(write);
}

}  // namespace geode::viz::liquid
