#include "viz/fluid/RippleSim.hpp"

#include <algorithm>
#include <cmath>

#include "util/Log.hpp"
#include "viz/fluid/RippleMath.hpp"

namespace geode::viz::fluid {

namespace {
constexpr const char* kTag = "RippleSim";
constexpr const char* kProgAssets[] = {"ripple_splat_frag.glsl", "ripple_update_frag.glsl", "water_ink_splat_frag.glsl",
                                       "water_ink_advect_frag.glsl"};
}  // namespace

void RippleSim::create() {
    release();
    formats_ = probeFormats();
    available_ = formats_.ok;
    if (!available_) return;
    quad_.create();
    std::string error;
    const std::string baseVert = loader_.source("fluid_base_vert.glsl", &error);
    const int wanted = inkEnabled ? kProgCount : 2;
    for (int i = 0; i < wanted; ++i) {
        const GLuint program = loader_.buildSource(baseVert, loader_.source(kProgAssets[i], &error), &error);
        if (program == 0) {
            GEODE_LOGW(kTag, "ripple shader rejected by driver: %s", error.c_str());
            onShaderError("Water unavailable on this GPU: " + error);
            release();
            return;
        }
        programs_[static_cast<size_t>(i)] = UniformCache(program);
        programsBuilt_ = i + 1;
    }
}

bool RippleSim::resize(int w, int h) {
    if (!available_) return false;
    if (w == width_ && h == height_ && grid_) return false;
    width_ = w;
    height_ = h;
    aspect_ = static_cast<float>(w) / static_cast<float>(std::max(h, 1));
    allocGrid();
    return true;
}

bool RippleSim::applyResolution(int newSimRes) {
    if (!available_) return false;
    if (newSimRes == simRes_ && grid_) return false;
    simRes_ = newSimRes;
    if (width_ > 1) allocGrid();
    return true;
}

void RippleSim::allocGrid() {
    GLint prevFbo = 0;
    GLint prevVp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevVp);
    if (grid_) grid_->release();
    if (ink_) ink_->release();
    ink_.reset();
    const auto [gw, gh] = resolution(simRes_, width_, height_);
    grid_.emplace(gw, gh, formats_.rg, true);
    grid_->create();
    if (!grid_->ok()) {
        GEODE_LOGW(kTag, "ripple grid allocation failed (%dx%d) - water disabled", gw, gh);
        onShaderError("Water grid could not be allocated on this GPU");
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
        release();
        return;
    }
    if (inkEnabled && programsBuilt_ > kInkSplat) {
        ink_.emplace(gw, gh, formats_.rgba, true);
        ink_->create();
        if (!ink_->ok()) {
            GEODE_LOGW(kTag, "ink grid allocation failed (%dx%d) - liquid layer off", gw, gh);
            ink_->release();
            ink_.reset();
        } else {
            clearInk();
        }
    }
    cellSize_ = 2.0f / static_cast<float>(gh);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
}

void RippleSim::clearInk() {
    if (!ink_) return;
    for (Fbo* side : {&ink_->read(), &ink_->write()}) {
        glBindFramebuffer(GL_FRAMEBUFFER, side->fbo());
        glViewport(0, 0, side->width(), side->height());
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

void RippleSim::queueDrop(float x, float y, float radius, float amplitude, float r, float g, float b) {
    if (!available_) return;
    std::lock_guard<std::mutex> lock(pendingLock_);
    if (pending_.size() >= static_cast<size_t>(kMaxPending)) return;
    pending_.push_back({x, y, radius, amplitude, r, g, b});
}

void RippleSim::queueStroke(float x, float y, float dx, float dy, float dt, float radius, float strength, float r, float g, float b) {
    for (const auto& d : ripple::strokeDrops(x, y, dx, dy, dt, radius, strength)) queueDrop(d.x, d.y, d.radius, d.amplitude, r, g, b);
}

void RippleSim::step(float dtRaw) {
    if (!available_) return;
    if (!grid_) {
        std::lock_guard<std::mutex> lock(pendingLock_);
        pending_.clear();
        return;
    }
    DoubleFbo& g = *grid_;
    drained_.clear();
    {
        std::lock_guard<std::mutex> lock(pendingLock_);
        drained_.swap(pending_);
    }
    const float dt = std::clamp(dtRaw, 0.0f, 1.0f / 30.0f);
    glDisable(GL_BLEND);
    quad_.bind();

    size_t i = 0;
    while (i < drained_.size()) {
        const int n = static_cast<int>(std::min(static_cast<size_t>(kDropsPerPass), drained_.size() - i));
        for (int j = 0; j < n; ++j) {
            const Drop& d = drained_[i + static_cast<size_t>(j)];
            const size_t o = static_cast<size_t>(j) * 4;
            dropVec_[o] = d.x;
            dropVec_[o + 1] = d.y;
            dropVec_[o + 2] = d.radius;
            dropVec_[o + 3] = d.amplitude;
            dropColorVec_[o] = d.r;
            dropColorVec_[o + 1] = d.g;
            dropColorVec_[o + 2] = d.b;
            dropColorVec_[o + 3] = std::max({d.r, d.g, d.b});
        }
        useProgram(kSplat, g.width(), g.height());
        bindTex("uTarget", g.read().tex(), 0, kSplat);
        glUniform4fv(loc(kSplat, "uDrops"), kDropsPerPass, dropVec_.data());
        glUniform1i(loc(kSplat, "uDropCount"), n);
        blitDiscarding(g.write());
        g.swap();
        if (ink_) {
            DoubleFbo& inkFbo = *ink_;
            useProgram(kInkSplat, inkFbo.width(), inkFbo.height());
            bindTex("uTarget", inkFbo.read().tex(), 0, kInkSplat);
            glUniform4fv(loc(kInkSplat, "uDrops"), kDropsPerPass, dropVec_.data());
            glUniform4fv(loc(kInkSplat, "uDropColor"), kDropsPerPass, dropColorVec_.data());
            glUniform1i(loc(kInkSplat, "uDropCount"), n);
            set1f(kInkSplat, "uCeiling", kInkCeiling);
            blitDiscarding(inkFbo.write());
            inkFbo.swap();
        }
        i += static_cast<size_t>(n);
    }
    drained_.clear();

    if (dt > 0.0f) {
        const float c = std::max(waveSpeed, 1e-4f);
        const float cfl = ripple::cflClampedDt(c, dt, cellSize_);
        const int substeps = std::clamp(static_cast<int>(std::ceil(dt / cfl)), 1, kMaxSubsteps);
        const float subDt = ripple::cflClampedDt(c, dt / static_cast<float>(substeps), cellSize_);
        const float clampedDamping = std::clamp(damping, 0.9f, 0.999f);
        const float subDamping = std::pow(clampedDamping, subDt * 60.0f);
        const float subHeightDecay = ripple::heightDecayPerSubstep(clampedDamping, subDt);
        const float k = c * c * subDt / (cellSize_ * cellSize_);
        useProgram(kUpdate, g.width(), g.height());
        set1f(kUpdate, "uK", k);
        set1f(kUpdate, "uDt", subDt);
        set1f(kUpdate, "uDamping", subDamping);
        set1f(kUpdate, "uHeightDecay", subHeightDecay);
        for (int s = 0; s < substeps; ++s) {
            bindTex("uHeight", g.read().tex(), 0, kUpdate);
            blitDiscarding(g.write());
            g.swap();
        }
    }

    if (ink_ && dt > 0.0f) {
        DoubleFbo& inkFbo = *ink_;
        useProgram(kInkAdvect, inkFbo.width(), inkFbo.height());
        bindTex("uInk", inkFbo.read().tex(), 0, kInkAdvect);
        bindTex("uHeight", g.read().tex(), 1, kInkAdvect);
        set1f(kInkAdvect, "uDt", dt);
        set1f(kInkAdvect, "uFlow", std::clamp(inkFlow, 0.0f, 8.0f));
        set1f(kInkAdvect, "uKeep", ripple::inkDissipation(inkDissipation, dt));
        blitDiscarding(inkFbo.write());
        inkFbo.swap();
        glActiveTexture(GL_TEXTURE0);
    }
    quad_.unbind();
}

void RippleSim::release() {
    if (grid_) grid_->release();
    grid_.reset();
    if (ink_) ink_->release();
    ink_.reset();
    for (int i = 0; i < programsBuilt_; ++i) glDeleteProgram(programs_[static_cast<size_t>(i)].program());
    programs_.fill(UniformCache(0));
    programsBuilt_ = 0;
    quad_.release();
    std::lock_guard<std::mutex> lock(pendingLock_);
    pending_.clear();
    available_ = false;
}

void RippleSim::useProgram(int prog, int gridW, int gridH) {
    UniformCache& p = programs_[static_cast<size_t>(prog)];
    glUseProgram(p.program());
    glUniform2f(p.loc("uInvRes"), 1.0f / static_cast<float>(gridW), 1.0f / static_cast<float>(gridH));
    glUniform1f(p.loc("uAspect"), aspect_);
}

void RippleSim::blitDiscarding(const Fbo& target) {
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo());
    target.discardContents();
    glViewport(0, 0, target.width(), target.height());
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void RippleSim::bindTex(const char* name, GLuint tex, int unit, int prog) {
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(loc(prog, name), unit);
}

}  // namespace geode::viz::fluid
