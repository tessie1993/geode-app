#include "viz/fluid/FluidSim.hpp"

#include <algorithm>

#include "util/Log.hpp"

namespace geode::viz::fluid {

namespace {
constexpr const char* kTag = "FluidSim";
}

const char* FluidSim::progAsset(int prog) {
    switch (prog) {
        case kSplat: return "fluid_splat_frag.glsl";
        case kAdvect: return "fluid_advect_frag.glsl";
        case kCurl: return "fluid_curl_frag.glsl";
        case kVorticity: return "fluid_vorticity_frag.glsl";
        case kDivergence: return "fluid_divergence_frag.glsl";
        case kPressure: return "fluid_pressure_frag.glsl";
        case kGradient: return "fluid_gradient_frag.glsl";
        case kClear: return "fluid_clear_frag.glsl";
        case kCopy: return "fluid_copy_frag.glsl";
        default: return "fluid_display_frag.glsl";
    }
}

void FluidSim::create() {
    release();
    formats_ = probeFormats();
    available_ = formats_.ok;
    if (!available_) return;
    quad_.create();
    glGenSamplers(1, &linearSampler_);
    glSamplerParameteri(linearSampler_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(linearSampler_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(linearSampler_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(linearSampler_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    std::string error;
    baseVertSrc_ = loader_.source("fluid_base_vert.glsl", &error);
    for (int i = 0; i < kProgCount; ++i) {
        const GLuint program = loader_.buildSource(baseVertSrc_, loader_.source(progAsset(i), &error), &error);
        if (program == 0) {
            GEODE_LOGW(kTag, "base shader rejected by driver: %s", error.c_str());
            onShaderError("Fluid unavailable on this GPU: " + error);
            release();
            return;
        }
        programs_[static_cast<size_t>(i)] = UniformCache(program);
    }
    programsBuilt_ = true;
    std::lock_guard<std::mutex> lock(injectionLock_);
    injectionDirty_ = !pendingForceSrc_.empty() || !pendingDyeSrc_.empty();
}

bool FluidSim::resize(int w, int h) {
    if (!available_) return false;
    if (w == width_ && h == height_ && velocity_) return false;
    width_ = w;
    height_ = h;
    aspect_ = static_cast<float>(w) / static_cast<float>(std::max(h, 1));
    allocGrids(true);
    return true;
}

bool FluidSim::applyResolution(int newSimRes, int newDyeRes) {
    if (!available_) return false;
    if (newSimRes == simRes && newDyeRes == dyeRes && velocity_) return false;
    simRes = newSimRes;
    dyeRes = newDyeRes;
    if (width_ > 1) allocGrids(true);
    return true;
}

void FluidSim::allocGrids(bool preserve) {
    GLint prevFbo = 0;
    GLint prevVp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevVp);
    const auto [sw, sh] = resolution(simRes, width_, height_);
    const auto [dw, dh] = resolution(dyeRes, width_, height_);
    std::optional<DoubleFbo> oldVelocity = std::move(velocity_);
    std::optional<DoubleFbo> oldDye = std::move(dye_);
    if (pressure_) pressure_->release();
    pressure_.reset();
    if (divergence_) divergence_->release();
    if (curl_) curl_->release();
    velocity_.emplace(sw, sh, formats_.rg, false);
    velocity_->create();
    if (preserve && oldVelocity && velocity_->ok() && oldVelocity->ok()) copyInto(oldVelocity->read(), velocity_->read());
    if (oldVelocity) oldVelocity->release();
    if (!velocityOnly_) {
        dye_.emplace(dw, dh, formats_.rgba, true);
        dye_->create();
        if (preserve && oldDye && dye_->ok() && oldDye->ok()) copyInto(oldDye->read(), dye_->read());
    }
    if (oldDye) oldDye->release();
    pressure_.emplace(sw, sh, formats_.r, false);
    pressure_->create();
    divergence_.emplace(sw, sh, formats_.r, false);
    divergence_->create();
    curl_.emplace(sw, sh, formats_.r, false);
    curl_->create();
    const bool allOk = velocity_->ok() && pressure_->ok() && divergence_->ok() && curl_->ok() && (velocityOnly_ || (dye_ && dye_->ok()));
    if (!allOk) {
        GEODE_LOGW(kTag, "grid allocation failed (%dx%d / %dx%d) - fluid disabled", sw, sh, dw, dh);
        onShaderError("Fluid grids could not be allocated on this GPU");
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
        release();
        return;
    }
    cellSize_ = 2.0f / static_cast<float>(sh);
    rdx_ = 1.0f / cellSize_;
    halfRdx_ = 0.5f / cellSize_;
    alpha_ = -cellSize_ * cellSize_;
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
}

void FluidSim::copyInto(const Fbo& src, const Fbo& dst) {
    glDisable(GL_BLEND);
    quad_.bind();
    useProgram(kCopy, dst.width(), dst.height());
    bindTex("uTexture", src.tex(), 0, kCopy);
    blitDiscarding(dst);
    quad_.unbind();
}

void FluidSim::setInjectionShaders(const std::string& forceSrc, const std::string& dyeSrc) {
    std::lock_guard<std::mutex> lock(injectionLock_);
    pendingForceSrc_ = forceSrc;
    pendingDyeSrc_ = dyeSrc;
    injectionDirty_ = true;
}

void FluidSim::compileInjectionIfNeeded() {
    std::string force;
    std::string dyeSrc;
    {
        std::lock_guard<std::mutex> lock(injectionLock_);
        if (!injectionDirty_) return;
        injectionDirty_ = false;
        force = pendingForceSrc_;
        dyeSrc = pendingDyeSrc_;
    }
    std::string firstError;
    customForce_ = compileCustom(force, std::move(customForce_), &firstError);
    customDye_ = compileCustom(dyeSrc, std::move(customDye_), &firstError);
    onShaderError(firstError);
}

std::optional<UniformCache> FluidSim::compileCustom(const std::string& src, std::optional<UniformCache> current, std::string* firstError) {
    if (src.empty()) {
        deleteCustom(current);
        return std::nullopt;
    }
    std::string error;
    const GLuint p = loader_.buildSource(baseVertSrc_, src, &error);
    if (p == 0) {
        if (firstError->empty()) *firstError = error;
        return current;
    }
    deleteCustom(current);
    return UniformCache(p);
}

void FluidSim::deleteCustom(std::optional<UniformCache>& custom) {
    if (custom) glDeleteProgram(custom->program());
    custom.reset();
}

void FluidSim::queueSplat(const Splat& s) {
    if (available_) pending_.push_back(s);
}

void FluidSim::step(float dtRaw) {
    if (!available_) return;
    if (!velocity_ || !pressure_ || !divergence_ || !curl_) {
        pending_.clear();
        return;
    }
    DoubleFbo& vel = *velocity_;
    DoubleFbo& press = *pressure_;
    const Fbo& div = *divergence_;
    const Fbo& crl = *curl_;
    const float dt = std::clamp(dtRaw, 0.0f, 1.0f / 30.0f);
    compileInjectionIfNeeded();
    glDisable(GL_BLEND);
    quad_.bind();
    const float velInvW = 1.0f / static_cast<float>(vel.width());
    const float velInvH = 1.0f / static_cast<float>(vel.height());

    useProgram(kAdvect, vel.width(), vel.height());
    bindTex("uVelocity", vel.read().tex(), 0, kAdvect);
    bindTex("uSource", vel.read().tex(), 0, kAdvect);
    set2f(kAdvect, "uSrcInvRes", velInvW, velInvH);
    set2f(kAdvect, "uVelInvRes", velInvW, velInvH);
    set1f(kAdvect, "uDt", dt);
    set1f(kAdvect, "uRdx", rdx_);
    const float vd = 1.0f + velocityDissipation * dt;
    set3f(kAdvect, "uDecay", vd, vd, vd);
    blitDiscarding(vel.write());
    vel.swap();

    runInjection(vel, 0, customForce_, dt);

    useProgram(kCurl, crl.width(), crl.height());
    bindTex("uVelocity", vel.read().tex(), 0, kCurl);
    set1f(kCurl, "uHalfRdx", halfRdx_);
    blitDiscarding(crl);
    useProgram(kVorticity, vel.width(), vel.height());
    bindTex("uVelocity", vel.read().tex(), 0, kVorticity);
    bindTex("uCurl", crl.tex(), 1, kVorticity);
    set1f(kVorticity, "uCurlStrength", curlStrength);
    set1f(kVorticity, "uDx", cellSize_);
    set1f(kVorticity, "uDt", dt);
    blitDiscarding(vel.write());
    vel.swap();

    useProgram(kDivergence, div.width(), div.height());
    bindTex("uVelocity", vel.read().tex(), 0, kDivergence);
    set1f(kDivergence, "uHalfRdx", halfRdx_);
    set2f(kDivergence, "uInvRes", 1.0f / static_cast<float>(div.width()), 1.0f / static_cast<float>(div.height()));
    blitDiscarding(div);
    useProgram(kClear, press.width(), press.height());
    bindTex("uTexture", press.read().tex(), 0, kClear);
    set1f(kClear, "uValue", pressureDamp);
    blitDiscarding(press.write());
    press.swap();
    useProgram(kPressure, press.width(), press.height());
    bindTex("uDivergence", div.tex(), 1, kPressure);
    set1f(kPressure, "uAlpha", alpha_);
    set2f(kPressure, "uInvRes", 1.0f / static_cast<float>(press.width()), 1.0f / static_cast<float>(press.height()));
    for (int i = 0; i < pressureIterations; ++i) {
        bindTex("uPressure", press.read().tex(), 0, kPressure);
        blitDiscarding(press.write());
        press.swap();
    }
    useProgram(kGradient, vel.width(), vel.height());
    bindTex("uPressure", press.read().tex(), 0, kGradient);
    bindTex("uVelocity", vel.read().tex(), 1, kGradient);
    set1f(kGradient, "uHalfRdx", halfRdx_);
    set2f(kGradient, "uInvRes", velInvW, velInvH);
    blitDiscarding(vel.write());
    vel.swap();

    if (!velocityOnly_ && dye_) {
        DoubleFbo& dyeB = *dye_;
        runInjection(dyeB, 1, customDye_, dt);
        useProgram(kAdvect, dyeB.width(), dyeB.height());
        bindTex("uVelocity", vel.read().tex(), 0, kAdvect);
        glBindSampler(0, linearSampler_);
        bindTex("uSource", dyeB.read().tex(), 1, kAdvect);
        set2f(kAdvect, "uSrcInvRes", 1.0f / static_cast<float>(dyeB.width()), 1.0f / static_cast<float>(dyeB.height()));
        set2f(kAdvect, "uVelInvRes", velInvW, velInvH);
        set1f(kAdvect, "uDt", dt);
        set1f(kAdvect, "uRdx", rdx_);
        const float a = std::clamp(chromaticAging, 0.0f, 1.0f);
        set3f(kAdvect, "uDecay", 1.0f + densityDissipation * (1.0f - 0.20f * a) * dt, 1.0f + densityDissipation * (1.0f + 0.35f * a) * dt,
              1.0f + densityDissipation * (1.0f - 0.05f * a) * dt);
        blitDiscarding(dyeB.write());
        dyeB.swap();
        glBindSampler(0, 0);
    }
    pending_.clear();
    quad_.unbind();
}

void FluidSim::runInjection(DoubleFbo& target, int mode, std::optional<UniformCache>& custom, float dt) {
    for (const Splat& s : pending_) {
        UniformCache& cache = custom ? *custom : programs_[kSplat];
        glUseProgram(cache.program());
        glUniform2f(cache.loc("uInvRes"), 1.0f / static_cast<float>(target.width()), 1.0f / static_cast<float>(target.height()));
        glUniform1f(cache.loc("uAspect"), aspect_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, target.read().tex());
        glUniform1i(cache.loc("uTarget"), 0);
        glUniform2f(cache.loc("uPrev"), s.prevX, s.prevY);
        glUniform2f(cache.loc("uCur"), s.curX, s.curY);
        glUniform1f(cache.loc("uRadius"), s.radius);
        if (mode == 0) {
            glUniform3f(cache.loc("uValue"), s.velX, s.velY, 0.0f);
        } else {
            glUniform3f(cache.loc("uValue"), s.r, s.g, s.b);
        }
        glUniform1i(cache.loc("uMode"), mode);
        glUniform1f(cache.loc("uCeiling"), dyeCeiling);
        if (custom) {
            glUniform1f(cache.loc("uDt"), dt);
            glUniform1f(cache.loc("uDx"), cellSize_);
            glUniform1f(cache.loc("uTime"), timeSeconds);
            glUniform1f(cache.loc("uBass"), audioBass);
            glUniform1f(cache.loc("uMid"), audioMid);
            glUniform1f(cache.loc("uTreble"), audioTreble);
            glUniform1f(cache.loc("uEnergy"), audioEnergy);
            glUniform1f(cache.loc("uBeat"), audioBeat);
        }
        blit(target.write());
        target.swap();
    }
}

void FluidSim::drawDisplay() {
    if (!available_ || !dye_) return;
    quad_.bind();
    UniformCache& prog = programs_[kDisplay];
    glUseProgram(prog.program());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, dye_->read().tex());
    glUniform1i(prog.loc("uDye"), 0);
    const float invW = 1.0f / static_cast<float>(dye_->width());
    const float invH = 1.0f / static_cast<float>(dye_->height());
    glUniform2f(prog.loc("uInvRes"), invW, invH);
    glUniform2f(prog.loc("uTexelSize"), invW, invH);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    quad_.unbind();
}

void FluidSim::release() {
    for (auto* grid : {&velocity_, &dye_, &pressure_}) {
        if (*grid) (*grid)->release();
        grid->reset();
    }
    for (auto* fbo : {&divergence_, &curl_}) {
        if (*fbo) (*fbo)->release();
        fbo->reset();
    }
    if (programsBuilt_) {
        for (auto& cache : programs_) glDeleteProgram(cache.program());
        programsBuilt_ = false;
    }
    programs_.fill(UniformCache(0));
    deleteCustom(customForce_);
    deleteCustom(customDye_);
    quad_.release();
    if (linearSampler_ != 0) glDeleteSamplers(1, &linearSampler_);
    linearSampler_ = 0;
    pending_.clear();
    available_ = false;
}

void FluidSim::useProgram(int prog, int gridW, int gridH) {
    UniformCache& p = programs_[static_cast<size_t>(prog)];
    glUseProgram(p.program());
    glUniform2f(p.loc("uInvRes"), 1.0f / static_cast<float>(gridW), 1.0f / static_cast<float>(gridH));
    glUniform1f(p.loc("uAspect"), aspect_);
}

void FluidSim::blitDiscarding(const Fbo& target) {
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo());
    target.discardContents();
    glViewport(0, 0, target.width(), target.height());
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void FluidSim::blit(const Fbo& target) {
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo());
    glViewport(0, 0, target.width(), target.height());
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void FluidSim::bindTex(const char* name, GLuint tex, int unit, int prog) {
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(loc(prog, name), unit);
}

}  // namespace geode::viz::fluid
