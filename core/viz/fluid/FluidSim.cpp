#include "viz/fluid/FluidSim.hpp"

#include <algorithm>

#include "util/Log.hpp"

namespace geode::viz::fluid {

namespace {
constexpr const char* kTag = "FluidSim";
}

const char* FluidSim::progAsset(int prog) {
    switch (prog) {
        case kSplatBatch: return "fluid_splat_batch_frag.glsl";
        case kAdvect: return "fluid_advect_frag.glsl";
        case kMacCormack: return "fluid_maccormack_frag.glsl";
        case kCurl: return "fluid_curl_frag.glsl";
        case kVorticity: return "fluid_vorticity_frag.glsl";
        case kDivergence: return "fluid_divergence_frag.glsl";
        case kMgSmooth: return "fluid_mg_smooth_frag.glsl";
        case kMgRestrict: return "fluid_mg_restrict_frag.glsl";
        case kMgProlong: return "fluid_mg_prolong_frag.glsl";
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
        std::string frag = loader_.source(progAsset(i), &error);
        // The display shader's #if chain needs LOOK defined (GLSL ES rejects an undefined name in
        // #if). Look prepends it for its variants; this plain fallback is LOOK 0, no keywords.
        if (i == kDisplay) {
            const size_t nl = frag.find('\n');
            frag.insert(nl == std::string::npos ? frag.size() : nl + 1, "#define LOOK 0\n");
        }
        const GLuint program = loader_.buildSource(baseVertSrc_, frag, &error);
        if (program == 0) {
            GEODE_LOGW(kTag, "base shader rejected by driver: %s", error.c_str());
            onShaderError("Fluid unavailable on this GPU: " + error);
            release();
            return;
        }
        programs_[static_cast<size_t>(i)] = UniformCache(program);
    }
    programsBuilt_ = true;
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

void FluidSim::setTemperature(bool on) {
    if (velocityOnly_) on = false;
    if (on == temperatureOn_) return;
    temperatureOn_ = on;
    if (available_ && width_ > 1 && velocity_) allocGrids(true);
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
    std::optional<DoubleFbo> oldTemperature = std::move(temperature_);
    velocity_.reset();
    dye_.reset();
    temperature_.reset();
    if (pressure_) pressure_->release();
    pressure_.reset();
    if (divergence_) divergence_->release();
    if (curl_) curl_->release();
    // Scratch targets are sized to their field and made on first use (ensureScratch).
    for (std::optional<Fbo>* scratch : {&velocityScratch_, &dyeScratch_}) {
        if (*scratch) (*scratch)->release();
        scratch->reset();
    }
    for (DoubleFbo& level : mgP_) level.release();
    for (Fbo& level : mgRhs_) level.release();
    mgP_.clear();
    mgRhs_.clear();

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
    if (temperatureOn_) {
        // Linear: the display samples it at screen resolution. R16F is filterable in core ES 3.0.
        temperature_.emplace(sw, sh, formats_.r, true);
        temperature_->create();
        if (preserve && oldTemperature && temperature_->ok() && oldTemperature->ok()) copyInto(oldTemperature->read(), temperature_->read());
    }
    if (oldTemperature) oldTemperature->release();
    pressure_.emplace(sw, sh, formats_.r, false);
    pressure_->create();
    divergence_.emplace(sw, sh, formats_.r, false);
    divergence_->create();
    curl_.emplace(sw, sh, formats_.r, false);
    curl_->create();
    // The multigrid's coarse levels, a correction and a right-hand side each. All small, so one
    // failing means the driver is refusing FBOs outright, and that takes the sim down with the rest.
    const auto levels = math::multigridLevels(sw, sh);
    mgP_.reserve(levels.size());
    mgRhs_.reserve(levels.size());
    bool levelsOk = true;
    for (size_t l = 1; l < levels.size(); ++l) {
        mgP_.emplace_back(levels[l].first, levels[l].second, formats_.r, false);
        mgP_.back().create();
        mgRhs_.emplace_back(levels[l].first, levels[l].second, formats_.r, false);
        mgRhs_.back().create();
        levelsOk = levelsOk && mgP_.back().ok() && mgRhs_.back().ok();
    }
    const bool allOk = velocity_->ok() && pressure_->ok() && divergence_->ok() && curl_->ok() && (velocityOnly_ || (dye_ && dye_->ok())) &&
                       (!temperatureOn_ || (temperature_ && temperature_->ok())) && levelsOk;
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

void FluidSim::queueSplat(const Splat& s) {
    if (available_) pending_.push_back(s);
}

void FluidSim::step(float dtRaw) {
    // The whole frame is simulated, in equal steps the solver is stable under (#383): the splats queued
    // this frame go into the first step, which clears them.
    const math::Substeps plan = math::substeps(dtRaw);
    for (int i = 0; i < plan.count; ++i) stepOnce(plan.step);
}

void FluidSim::stepOnce(float dtStep) {
    if (!available_) return;
    if (!velocity_ || !pressure_ || !divergence_ || !curl_) {
        pending_.clear();
        return;
    }
    DoubleFbo& vel = *velocity_;
    DoubleFbo& press = *pressure_;
    const Fbo& div = *divergence_;
    const Fbo& crl = *curl_;
    const float dt = dtStep;
    glDisable(GL_BLEND);
    quad_.bind();
    const float velInvW = 1.0f / static_cast<float>(vel.width());
    const float velInvH = 1.0f / static_cast<float>(vel.height());

    const float vd = 1.0f + velocityDissipation * dt;
    advect(vel, macCormack ? &velocityScratch_ : nullptr, false, dt, vd, vd, vd);
    const bool hot = temperature_.has_value();
    if (hot) {
        // Semi-Lagrangian always: heat is smooth by nature and only ever seen blurred by the looks.
        const float cd = math::coolingDivisor(cooling, dt);
        advect(*temperature_, nullptr, false, dt, cd, cd, cd);
    }

    runBatchedSplats(vel, 0);
    if (hot && heat > 0.0f) runBatchedSplats(*temperature_, 2);

    useProgram(kCurl, crl.width(), crl.height());
    bindTex("uVelocity", vel.read().tex(), 0, kCurl);
    set1f(kCurl, "uHalfRdx", halfRdx_);
    blitDiscarding(crl);
    useProgram(kVorticity, vel.width(), vel.height());
    bindTex("uVelocity", vel.read().tex(), 0, kVorticity);
    bindTex("uCurl", crl.tex(), 1, kVorticity);
    // Texture 0 is incomplete and samples as zero: a style without heat gets no lift.
    bindTex("uTemperature", hot ? temperature_->read().tex() : 0, 2, kVorticity);
    set1f(kVorticity, "uBuoyancy", hot ? buoyancy : 0.0f);
    set1f(kVorticity, "uMaxHeat", math::kMaxTemperature);
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
    solvePressure();
    useProgram(kGradient, vel.width(), vel.height());
    bindTex("uPressure", press.read().tex(), 0, kGradient);
    bindTex("uVelocity", vel.read().tex(), 1, kGradient);
    set1f(kGradient, "uHalfRdx", halfRdx_);
    set2f(kGradient, "uInvRes", velInvW, velInvH);
    blitDiscarding(vel.write());
    vel.swap();

    if (!velocityOnly_ && dye_) {
        DoubleFbo& dyeB = *dye_;
        runBatchedSplats(dyeB, 1);
        const float a = std::clamp(chromaticAging, 0.0f, 1.0f);
        advect(dyeB, macCormack ? &dyeScratch_ : nullptr, true, dt, 1.0f + densityDissipation * (1.0f - 0.20f * a) * dt,
               1.0f + densityDissipation * (1.0f + 0.35f * a) * dt, 1.0f + densityDissipation * (1.0f - 0.05f * a) * dt);
    }
    pending_.clear();
    quad_.unbind();
}

bool FluidSim::ensureScratch(std::optional<Fbo>& scratch, const DoubleFbo& like, TexFormat fmt, bool linear) {
    if (scratch && scratch->width() == like.width() && scratch->height() == like.height()) return scratch->ok();
    if (scratch) scratch->release();
    scratch.emplace(like.width(), like.height(), fmt, linear);
    scratch->create();
    if (!scratch->ok()) {
        // Out of memory for the third buffer: this field drops to first-order advection and the
        // rest of the sim is unaffected. Logged once, because the refused target is kept.
        GEODE_LOGW(kTag, "MacCormack scratch (%dx%d) refused - semi-Lagrangian for this field", like.width(), like.height());
    }
    return scratch->ok();
}

void FluidSim::advect(DoubleFbo& field, std::optional<Fbo>* scratch, bool linearVelocity, float dt, float d0, float d1, float d2) {
    const float velInvW = 1.0f / static_cast<float>(velocity_->width());
    const float velInvH = 1.0f / static_cast<float>(velocity_->height());
    const float srcInvW = 1.0f / static_cast<float>(field.width());
    const float srcInvH = 1.0f / static_cast<float>(field.height());
    // The scratch must be the field's twin: the dye is RGBA and linear, the velocity RG and nearest.
    const bool isDye = dye_ && &field == &*dye_;
    const TexFormat fmt = isDye ? formats_.rgba : formats_.rg;
    const bool second = scratch != nullptr && ensureScratch(*scratch, field, fmt, isDye);
    const GLuint velTex = velocity_->read().tex();

    // Predictor: plain semi-Lagrangian. It carries no decay when a corrector follows, so both see
    // the same field; the corrector applies it once.
    useProgram(kAdvect, field.width(), field.height());
    bindTex("uVelocity", velTex, 0, kAdvect);
    // A field of the velocity's own size reads it at matching texel centres; the dye reads it
    // between them, through the linear sampler.
    if (linearVelocity) glBindSampler(0, linearSampler_);
    bindTex("uSource", field.read().tex(), 1, kAdvect);
    set2f(kAdvect, "uSrcInvRes", srcInvW, srcInvH);
    set2f(kAdvect, "uVelInvRes", velInvW, velInvH);
    set1f(kAdvect, "uDt", dt);
    set1f(kAdvect, "uRdx", rdx_);
    if (second) {
        set3f(kAdvect, "uDecay", 1.0f, 1.0f, 1.0f);
    } else {
        set3f(kAdvect, "uDecay", d0, d1, d2);
    }
    blitDiscarding(field.write());
    if (second) {
        useProgram(kMacCormack, field.width(), field.height());
        bindTex("uVelocity", velTex, 0, kMacCormack);
        bindTex("uSource", field.read().tex(), 1, kMacCormack);
        bindTex("uForward", field.write().tex(), 2, kMacCormack);
        set2f(kMacCormack, "uSrcInvRes", srcInvW, srcInvH);
        set2f(kMacCormack, "uVelInvRes", velInvW, velInvH);
        set1f(kMacCormack, "uDt", dt);
        set1f(kMacCormack, "uRdx", rdx_);
        set3f(kMacCormack, "uDecay", d0, d1, d2);
        blitDiscarding(**scratch);
        field.adoptRead(**scratch);
    } else {
        field.swap();
    }
    if (linearVelocity) glBindSampler(0, 0);
}

float FluidSim::levelH2(int level) const {
    const float h = cellSize_ * static_cast<float>(1 << level);
    return h * h;
}

void FluidSim::clearTarget(const Fbo& target) {
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo());
    glViewport(0, 0, target.width(), target.height());
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void FluidSim::smoothPass(int level) {
    DoubleFbo& p = levelP(level);
    useProgram(kMgSmooth, p.width(), p.height());
    bindTex("uP", p.read().tex(), 0, kMgSmooth);
    bindTex("uRhs", levelRhs(level), 1, kMgSmooth);
    set1f(kMgSmooth, "uH2", levelH2(level));
    set1f(kMgSmooth, "uOmega", math::kSmootherOmega);
    blitDiscarding(p.write());
    p.swap();
}

void FluidSim::vCycle(int level, const math::SolvePlan& plan) {
    DoubleFbo& p = levelP(level);
    // A coarse level solves for a correction to the level above, starting from zero.
    if (level > 0) clearTarget(p.read());
    if (level == plan.levels - 1) {
        for (int i = 0; i < plan.coarseIterations; ++i) smoothPass(level);
        return;
    }
    for (int i = 0; i < plan.smooth; ++i) smoothPass(level);
    const Fbo& coarseRhs = mgRhs_[static_cast<size_t>(level)];
    useProgram(kMgRestrict, coarseRhs.width(), coarseRhs.height());
    bindTex("uP", p.read().tex(), 0, kMgRestrict);
    bindTex("uRhs", levelRhs(level), 1, kMgRestrict);
    set1f(kMgRestrict, "uH2", levelH2(level));
    blitDiscarding(coarseRhs);
    vCycle(level + 1, plan);
    DoubleFbo& coarse = levelP(level + 1);
    useProgram(kMgProlong, p.width(), p.height());
    bindTex("uP", p.read().tex(), 0, kMgProlong);
    bindTex("uE", coarse.read().tex(), 1, kMgProlong);
    blitDiscarding(p.write());
    p.swap();
    for (int i = 0; i < plan.smooth; ++i) smoothPass(level);
}

void FluidSim::solvePressure() {
    math::SolvePlan plan = math::planPressureSolve(pressureIterations, pressure_->width(), pressure_->height());
    // The plan's levels come from the same multigridLevels the allocation used; this only guards
    // the two from ever disagreeing.
    plan.levels = std::min(plan.levels, static_cast<int>(mgP_.size()) + 1);
    if (plan.levels >= 2) {
        for (int c = 0; c < plan.cycles; ++c) vCycle(0, plan);
    } else {
        plan.extraPasses = std::max(pressureIterations, 1);
    }
    for (int i = 0; i < plan.extraPasses; ++i) smoothPass(0);
}

void FluidSim::runBatchedSplats(DoubleFbo& target, int mode) {
    UniformCache& cache = programs_[kSplatBatch];
    std::array<float, math::kSplatBatch * 4> seg{};
    std::array<float, math::kSplatBatch * 4> val{};
    for (size_t first = 0; first < pending_.size(); first += math::kSplatBatch) {
        const size_t n = std::min(pending_.size() - first, static_cast<size_t>(math::kSplatBatch));
        for (size_t i = 0; i < n; ++i) {
            const Splat& s = pending_[first + i];
            seg[i * 4 + 0] = s.prevX;
            seg[i * 4 + 1] = s.prevY;
            seg[i * 4 + 2] = s.curX;
            seg[i * 4 + 3] = s.curY;
            val[i * 4 + 0] = s.radius;
            val[i * 4 + 1] = mode == 0 ? s.velX : s.r;
            val[i * 4 + 2] = mode == 0 ? s.velY : s.g;
            val[i * 4 + 3] = mode == 0 ? 0.0f : s.b;
        }
        glUseProgram(cache.program());
        glUniform2f(cache.loc("uInvRes"), 1.0f / static_cast<float>(target.width()), 1.0f / static_cast<float>(target.height()));
        glUniform1f(cache.loc("uAspect"), aspect_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, target.read().tex());
        glUniform1i(cache.loc("uTarget"), 0);
        glUniform4fv(cache.loc("uSeg"), static_cast<GLsizei>(n), seg.data());
        glUniform4fv(cache.loc("uVal"), static_cast<GLsizei>(n), val.data());
        glUniform1i(cache.loc("uCount"), static_cast<GLint>(n));
        glUniform1i(cache.loc("uMode"), mode);
        glUniform1f(cache.loc("uCeiling"), dyeCeiling);
        glUniform1f(cache.loc("uHeat"), heat);
        glUniform1f(cache.loc("uMaxHeat"), math::kMaxTemperature);
        blitDiscarding(target.write());
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
    for (DoubleFbo& level : mgP_) level.release();
    for (Fbo& level : mgRhs_) level.release();
    mgP_.clear();
    mgRhs_.clear();
    for (std::optional<Fbo>* scratch : {&velocityScratch_, &dyeScratch_}) {
        if (*scratch) (*scratch)->release();
        scratch->reset();
    }
    for (std::optional<DoubleFbo>* grid : {&velocity_, &dye_, &pressure_, &temperature_}) {
        if (*grid) (*grid)->release();
        grid->reset();
    }
    for (std::optional<Fbo>* fbo : {&divergence_, &curl_}) {
        if (*fbo) (*fbo)->release();
        fbo->reset();
    }
    if (programsBuilt_) {
        for (auto& cache : programs_) glDeleteProgram(cache.program());
        programsBuilt_ = false;
    }
    programs_.fill(UniformCache(0));
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

void FluidSim::bindTex(const char* name, GLuint tex, int unit, int prog) {
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(loc(prog, name), unit);
}

}  // namespace geode::viz::fluid
