#include "viz/fluid/FluidLook.hpp"

#include <algorithm>

#include "util/Log.hpp"
#include "viz/BlueNoise.hpp"

namespace geode::viz::fluid {

namespace {
constexpr const char* kTag = "FluidSim";
}

void Look::create(const Formats& formats) {
    release();
    formats_ = formats;
    std::string error;
    const std::string vert = loader_.source("fluid_base_vert.glsl", &error);
    const auto build = [&](const char* frag) { return loader_.buildSource(vert, loader_.source(frag, &error), &error); };
    const GLuint prefilter = build("fluid_bloom_prefilter_frag.glsl");
    const GLuint bloomBlur = build("fluid_bloom_blur_frag.glsl");
    const GLuint bloomFinal = build("fluid_bloom_final_frag.glsl");
    const GLuint sunraysMask = build("fluid_sunrays_mask_frag.glsl");
    const GLuint sunrays = build("fluid_sunrays_frag.glsl");
    const GLuint blur = build("fluid_blur_frag.glsl");
    const std::string displaySrc = loader_.source("fluid_display_frag.glsl", &error);
    std::array<GLuint, kDisplayVariants> display{};
    bool ok = prefilter && bloomBlur && bloomFinal && sunraysMask && sunrays && blur && !displaySrc.empty();
    for (int flags = 0; flags < kDisplayVariants && ok; ++flags) {
        display[static_cast<size_t>(flags)] = loader_.buildSource(vert, withKeywords(displaySrc, flags), &error);
        ok = display[static_cast<size_t>(flags)] != 0;
    }
    if (!ok) {
        GEODE_LOGW(kTag, "look shader rejected by driver: %s", error.c_str());
        for (GLuint p : {prefilter, bloomBlur, bloomFinal, sunraysMask, sunrays, blur}) {
            if (p != 0) glDeleteProgram(p);
        }
        for (GLuint p : display) {
            if (p != 0) glDeleteProgram(p);
        }
        return;
    }
    prefilter_ = UniformCache(prefilter);
    bloomBlur_ = UniformCache(bloomBlur);
    bloomFinal_ = UniformCache(bloomFinal);
    sunraysMask_ = UniformCache(sunraysMask);
    sunrays_ = UniformCache(sunrays);
    blur_ = UniformCache(blur);
    for (size_t i = 0; i < display.size(); ++i) display_[i] = UniformCache(display[i]);
    ditherTex_ = blue_noise::createTexture(loader_.assets);
    quad_.create();
    available_ = true;
}

void Look::resize(int w, int h) {
    if (!available_) return;
    if (w == targetW_ && h == targetH_ && bloomResult_) return;
    targetW_ = w;
    targetH_ = h;
    releaseTargets();
    const auto [bw, bh] = resolution(kBloomBaseRes, w, h);
    bloomResult_.emplace(bw, bh, formats_.rgba, true);
    bloomResult_->create();
    int mw = bw;
    int mh = bh;
    for (int i = 0; i < kBloomMaxLevels; ++i) {
        mw >>= 1;
        mh >>= 1;
        if (mw < 2 || mh < 2) break;
        bloomMips_.emplace_back(mw, mh, formats_.rgba, true);
        bloomMips_.back().create();
    }
    const auto [sw, sh] = resolution(kSunraysRes, w, h);
    sunraysMaskTarget_.emplace(sw, sh, formats_.rgba, true);
    sunraysMaskTarget_->create();
    sunraysTarget_.emplace(sw, sh, formats_.r, true);
    sunraysTarget_->create();
    sunraysTemp_.emplace(sw, sh, formats_.r, true);
    sunraysTemp_->create();
    const bool allOk = bloomResult_->ok() && std::all_of(bloomMips_.begin(), bloomMips_.end(), [](const Fbo& f) { return f.ok(); }) &&
                       sunraysMaskTarget_->ok() && sunraysTarget_->ok() && sunraysTemp_->ok();
    if (!allOk) {
        GEODE_LOGW(kTag, "look target allocation failed - bloom/sunrays disabled");
        releaseTargets();
    }
}

void Look::process(GLuint dyeTex, bool bloomOn, bool sunraysOn) {
    if (!available_) return;
    glDisable(GL_BLEND);
    quad_.bind();
    if (bloomOn) applyBloom(dyeTex);
    if (sunraysOn) applySunrays(dyeTex);
    quad_.unbind();
}

void Look::applyBloom(GLuint dyeTex) {
    if (!bloomResult_ || bloomMips_.size() < 2) return;
    const float knee = bloomThreshold * bloomKnee + 1e-4f;
    const Fbo* dst = &bloomMips_[0];
    use(prefilter_, 1.0f / static_cast<float>(dst->width()), 1.0f / static_cast<float>(dst->height()));
    bindTex(prefilter_, "uTexture", dyeTex, 0);
    glUniform3f(prefilter_.loc("uCurve"), bloomThreshold - knee, knee * 2.0f, 0.25f / knee);
    glUniform1f(prefilter_.loc("uThreshold"), bloomThreshold);
    blitDiscarding(*dst);
    const Fbo* last = dst;
    for (size_t i = 1; i < bloomMips_.size(); ++i) {
        dst = &bloomMips_[i];
        use(bloomBlur_, 1.0f / static_cast<float>(last->width()), 1.0f / static_cast<float>(last->height()));
        bindTex(bloomBlur_, "uTexture", last->tex(), 0);
        blitDiscarding(*dst);
        last = dst;
    }
    glBlendFunc(GL_ONE, GL_ONE);
    glEnable(GL_BLEND);
    // Additive up-chain: the destination still holds this frame's down-chain value, so plain blits only.
    for (int i = static_cast<int>(bloomMips_.size()) - 2; i >= 0; --i) {
        dst = &bloomMips_[static_cast<size_t>(i)];
        use(bloomBlur_, 1.0f / static_cast<float>(last->width()), 1.0f / static_cast<float>(last->height()));
        bindTex(bloomBlur_, "uTexture", last->tex(), 0);
        blit(*dst);
        last = dst;
    }
    glDisable(GL_BLEND);
    use(bloomFinal_, 1.0f / static_cast<float>(last->width()), 1.0f / static_cast<float>(last->height()));
    bindTex(bloomFinal_, "uTexture", last->tex(), 0);
    glUniform1f(bloomFinal_.loc("uIntensity"), bloomIntensity);
    blitDiscarding(*bloomResult_);
}

void Look::applySunrays(GLuint dyeTex) {
    if (!sunraysMaskTarget_ || !sunraysTarget_ || !sunraysTemp_) return;
    const Fbo& mask = *sunraysMaskTarget_;
    const Fbo& rays = *sunraysTarget_;
    const Fbo& temp = *sunraysTemp_;
    const float invW = 1.0f / static_cast<float>(rays.width());
    const float invH = 1.0f / static_cast<float>(rays.height());
    use(sunraysMask_, 1.0f / static_cast<float>(mask.width()), 1.0f / static_cast<float>(mask.height()));
    bindTex(sunraysMask_, "uTexture", dyeTex, 0);
    blitDiscarding(mask);
    use(sunrays_, invW, invH);
    bindTex(sunrays_, "uTexture", mask.tex(), 0);
    glUniform1f(sunrays_.loc("uWeight"), sunraysWeight);
    blitDiscarding(rays);
    use(blur_, invW, invH);
    bindTex(blur_, "uTexture", rays.tex(), 0);
    glUniform2f(blur_.loc("uDirection"), 1.33333f * invW, 0.0f);
    blitDiscarding(temp);
    bindTex(blur_, "uTexture", temp.tex(), 0);
    glUniform2f(blur_.loc("uDirection"), 0.0f, 1.33333f * invH);
    blitDiscarding(rays);
}

void Look::drawDisplay(GLuint dyeTex, bool shadingOn, bool bloomOn, bool sunraysOn, int viewportW, int viewportH) {
    if (!available_) return;
    const bool bloomReady = bloomOn && bloomMips_.size() >= 2 && bloomResult_.has_value();
    const bool raysReady = sunraysOn && sunraysTarget_.has_value();
    const int flags = (shadingOn ? 1 : 0) | (bloomReady ? 2 : 0) | (raysReady ? 4 : 0);
    UniformCache& program = display_[static_cast<size_t>(flags)];
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    const float invW = 1.0f / static_cast<float>(viewportW);
    const float invH = 1.0f / static_cast<float>(viewportH);
    use(program, invW, invH);
    bindTex(program, "uDye", dyeTex, 0);
    if (bloomReady) bindTex(program, "uBloom", bloomResult_->tex(), 1);
    if (raysReady) bindTex(program, "uSunrays", sunraysTarget_->tex(), 2);
    bindTex(program, "uDither", ditherTex_, 3);
    glUniform2f(program.loc("uDitherScale"), static_cast<float>(viewportW) / blue_noise::kSize, static_cast<float>(viewportH) / blue_noise::kSize);
    glUniform2f(program.loc("uTexelSize"), invW, invH);
    quad_.draw();
    glDisable(GL_BLEND);
}

void Look::release() {
    releaseTargets();
    for (UniformCache* cache : {&prefilter_, &bloomBlur_, &bloomFinal_, &sunraysMask_, &sunrays_, &blur_}) {
        if (cache->program() != 0) glDeleteProgram(cache->program());
        *cache = UniformCache(0);
    }
    for (UniformCache& cache : display_) {
        if (cache.program() != 0) glDeleteProgram(cache.program());
        cache = UniformCache(0);
    }
    if (ditherTex_ != 0) glDeleteTextures(1, &ditherTex_);
    ditherTex_ = 0;
    quad_.release();
    targetW_ = 1;
    targetH_ = 1;
    available_ = false;
}

void Look::releaseTargets() {
    for (Fbo& mip : bloomMips_) mip.release();
    bloomMips_.clear();
    for (auto* target : {&bloomResult_, &sunraysMaskTarget_, &sunraysTarget_, &sunraysTemp_}) {
        if (*target) (*target)->release();
        target->reset();
    }
}

std::string Look::withKeywords(const std::string& src, int flags) {
    std::string defines;
    if (flags & 1) defines += "#define SHADING\n";
    if (flags & 2) defines += "#define BLOOM\n";
    if (flags & 4) defines += "#define SUNRAYS\n";
    if (defines.empty()) return src;
    const size_t vIdx = src.find("#version");
    const size_t nl = src.find('\n', vIdx == std::string::npos ? 0 : vIdx);
    if (nl == std::string::npos) return src + "\n" + defines;
    return src.substr(0, nl + 1) + defines + src.substr(nl + 1);
}

void Look::use(UniformCache& program, float invW, float invH) {
    glUseProgram(program.program());
    glUniform2f(program.loc("uInvRes"), invW, invH);
}

void Look::blitDiscarding(const Fbo& target) {
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo());
    target.discardContents();
    glViewport(0, 0, target.width(), target.height());
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void Look::blit(const Fbo& target) {
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo());
    glViewport(0, 0, target.width(), target.height());
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void Look::bindTex(UniformCache& program, const char* name, GLuint tex, int unit) {
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(program.loc(name), unit);
}

}  // namespace geode::viz::fluid
