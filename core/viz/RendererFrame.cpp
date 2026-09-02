#include "viz/Renderer.hpp"

#include <algorithm>
#include <cmath>

#include "viz/CompositeGrade.hpp"
#include "viz/LiveSignal.hpp"
#include "viz/Quad.hpp"

namespace geode::viz {

namespace {
// Port of applyBandGains: the per-scene band trims, clamped as the Kotlin path clamps them.
GeodeFeatureFrame gainAdjusted(const GeodeFeatureFrame& f, const SceneParams& p) {
    if (p.bassGain == 1.0f && p.midGain == 1.0f && p.trebGain == 1.0f) return f;
    GeodeFeatureFrame out = f;
    out.bass = std::clamp(f.bass * p.bassGain, 0.0f, 2.0f);
    out.mid = std::clamp(f.mid * p.midGain, 0.0f, 2.0f);
    out.treble = std::clamp(f.treble * p.trebGain, 0.0f, 2.0f);
    return out;
}
}  // namespace

float Renderer::beginFrame(double timeSeconds) {
    resetFrameState();
    frameNowS_ = timeSeconds;
    const double elapsed = lastTimeS_ < 0.0 ? 1.0 / 60.0 : timeSeconds - lastTimeS_;
    const float dt = static_cast<float>(std::clamp(elapsed, 0.001, 0.1));
    lastTimeS_ = timeSeconds;
    thermal_.onFrame(dt);
    timeSeconds_ = std::fmod(timeSeconds_ + dt, kTimeWrapSeconds);
    std::vector<std::pair<std::string, std::string>> pending;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        pending.swap(pendingShaders_);
        frameFeatures_ = features_;
    }
    for (const auto& [id, src] : pending) {
        if (Scene* scene = builtScene(id)) scene->setFragmentSource(src);
    }
    return dt;
}

Scene* Renderer::resolveActiveScene() {
    std::string requestedId;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        requestedId = requestedSceneId_;
    }
    Scene* requested = sceneFor(requestedId);
    sceneJustSwitched_ = false;
    if (requested && requested != activeScene_) {
        lastTimeS_ = frameNowS_;
        const bool cuts = TransitionCatalog::builtIn(transitionId_) == TransitionStyle::Cut;
        if (!cuts && activeScene_) {
            outgoingScene_ = activeScene_;
            outgoingParams_ = lastFinalParams_;
            transitionStartS_ = frameNowS_;
        }
        activeScene_ = requested;
        sceneJustSwitched_ = true;
    }
    return activeScene_;
}

SceneParams Renderer::resolveParams(float dt) {
    SceneParams requested;
    float morph;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        requested = requestedParams_;
        if (morphRemainSec_ > 0.0f) {
            morphRemainSec_ -= dt;
            morph = morphFadeSec_;
        } else {
            morph = 0.0f;
        }
    }
    const float fade = std::max(requested.paramFadeSec, morph);
    displayedParams_ = fade <= 0.01f ? requested : lerpParams(displayedParams_, requested, std::clamp(dt / fade, 0.0f, 1.0f));
    const auto& envValues = adsr_.tick(dt, frameFeatures_);
    AdsrEngine::lfoOffsets(adsr_.configs, envValues, envRate_, envDepth_);
    const auto& lfoValues = lfo_.tick(dt, frameFeatures_, envRate_.data(), envDepth_.data());
    SceneParams p = lfo_.apply(displayedParams_, lfoValues);
    p = AdsrEngine::apply(p, adsr_.configs, envValues);
    p = safety::apply(p, reducedMotion_);
    if (!thermalTierInfo(thermal_.tier()).optionalPasses) {
        p.flowEnabled = false;
        p.rippleOverlayEnabled = false;
    }
    lastFinalParams_ = p;
    postRotationAngle_ = grade::integrateRotation(postRotationAngle_, p.rotation, dt);
    postCyclePhase_ = grade::integrateCyclePhase(postCyclePhase_, p.cycleSpeed, dt, p.colorCycle);
    postBeatPulse_ = grade::integrateBeatPulse(postBeatPulse_, live::hit(frameFeatures_), dt);
    return p;
}

void Renderer::resolveLayerScene() {
    std::string layerId;
    std::string requestedId;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        layerId = layerSceneId_;
        requestedId = requestedSceneId_;
    }
    layerScene_ = nullptr;
    if (outgoingScene_ || layerId.empty() || layerId == requestedId) return;
    Scene* layer = sceneFor(layerId);
    if (layer && layer != activeScene_) layerScene_ = layer;
}

bool Renderer::ensureTargets() {
    if (!fboA_.ensure(renderWidth_, renderHeight_)) return false;
    if (!fboB_.ensure(renderWidth_, renderHeight_)) {
        layerScene_ = nullptr;
        outgoingScene_ = nullptr;
        outgoingParams_.reset();
    }
    return true;
}

void Renderer::deliverPcm(Scene& scene) {
    std::lock_guard<std::mutex> lock(stateLock_);
    if (pcmCount_ > 0) scene.acceptPcm(pcm_.data(), pcmCount_);
}

void Renderer::bindSecondaryTarget() {
    glBindFramebuffer(GL_FRAMEBUFFER, fboB_.fbo());
    glViewport(0, 0, renderWidth_, renderHeight_);
    glClear(GL_COLOR_BUFFER_BIT);
}

float Renderer::drawSecondaryTargets(const SceneParams& p, float dt) {
    float progress = 1.0f;
    if (layerScene_) {
        bindSecondaryTarget();
        layerScene_->setFlow(flowTex_ != 0 ? flowTex_ : compositePass_.zeroTex(), p.flowEnabled ? flowStrength_ : 0.0f);
        layerScene_->setParams(p);
        deliverPcm(*layerScene_);
        layerScene_->update(gainAdjusted(frameFeatures_, p), dt);
        layerScene_->draw(timeSeconds_);
    }
    if (outgoingScene_) {
        progress = std::clamp(static_cast<float>((frameNowS_ - transitionStartS_) * 1000.0 / transitionDurationMs_), 0.0f, 1.0f);
        if (progress >= 1.0f) {
            outgoingScene_ = nullptr;
            outgoingParams_.reset();
        } else {
            bindSecondaryTarget();
            const SceneParams& op = outgoingParams_ ? *outgoingParams_ : p;
            outgoingScene_->setParams(op);
            deliverPcm(*outgoingScene_);
            outgoingScene_->update(gainAdjusted(frameFeatures_, op), dt);
            outgoingScene_->draw(timeSeconds_);
        }
    }
    return progress;
}

void Renderer::drawSceneTarget(Scene& scene, const SceneParams& p, float dt) {
    glBindFramebuffer(GL_FRAMEBUFFER, fboA_.fbo());
    glViewport(0, 0, renderWidth_, renderHeight_);
    const float keep = scene.trailRetention(p);
    if (keep > 0.0f && !sceneJustSwitched_) {
        trailPass_.apply(p, keep, timeSeconds_, dt, fboA_, quadVao_, renderWidth_, renderHeight_);
    } else {
        glClear(GL_COLOR_BUFFER_BIT);
    }
    scene.setFlow(flowTex_ != 0 ? flowTex_ : compositePass_.zeroTex(), p.flowEnabled ? flowStrength_ : 0.0f);
    scene.setParams(p);
    deliverPcm(scene);
    scene.update(gainAdjusted(frameFeatures_, p), dt);
    scene.draw(timeSeconds_);
}

void Renderer::composite(Scene& scene, const SceneParams& p, float progress, GLuint targetFbo) {
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, width_, height_);
    glDisable(GL_BLEND);
    CompositePass::Inputs& in = compositeInputs_;
    in.texA = fboA_.tex();
    in.texB = fboB_.tex();
    GLuint flowTex = compositePass_.zeroTex();
    float flowStrength = 0.0f;
    if (p.flowEnabled) {
        if (const GLuint own = scene.velocityTexture(); own != 0) {
            flowTex = own;
            flowStrength = p.flowStrength;
        } else if (flowTex_ != 0) {
            flowTex = flowTex_;
            flowStrength = p.flowStrength;
        }
    }
    in.flowTex = flowTex;
    in.flowStrength = flowStrength;
    const bool rippleOn = rippleTex_ != 0 && rippleStrength_ > 0.0f;
    in.rippleTex = rippleOn ? rippleTex_ : compositePass_.zeroTex();
    in.rippleTexelW = rippleOn ? rippleTexelW_ : 0.0f;
    in.rippleTexelH = rippleOn ? rippleTexelH_ : 0.0f;
    in.rippleStrength = rippleOn ? std::clamp(rippleStrength_, 0.0f, 1.0f) : 0.0f;
    in.rippleSpecular = rippleOn ? std::clamp(rippleSpecular_, 0.0f, 1.0f) : 0.0f;
    in.progress = progress;
    in.layerMix = safety::layerMix(layerMix_, blendModeFromOrdinal(layerBlend_));
    in.blendOrdinal = layerBlend_;
    in.hasLayer = layerScene_ != nullptr;
    in.hasOutgoing = outgoingScene_ != nullptr;
    in.transitionId = transitionId_;
    in.transitionStyle = safety::transitionStyle(TransitionCatalog::builtIn(transitionId_).value_or(TransitionStyle::Fade));
    in.ratio = static_cast<float>(renderWidth_) / static_cast<float>(renderHeight_);
    in.timeSeconds = timeSeconds_;
    const float hit = live::hit(frameFeatures_);
    in.hitImpulse = hit;
    const SceneParams& fx = lastFinalParams_;
    in.flash = fx.flash * flashBudget_.gainFor(timeSeconds_, safety::flashImpulse(fx.flash, hit));
    in.strobeHz = safety::strobeHz();
    in.postRotationAngle = postRotationAngle_;
    in.postCyclePhase = postCyclePhase_;
    in.postBeatPulse = postBeatPulse_;
    in.quadVao = quadVao_;
    in.fx = fx;
    in.gateA = grade::gateFor(activeScene_->family()).toVec4();
    Scene* other = layerScene_ ? layerScene_ : outgoingScene_ ? outgoingScene_ : activeScene_;
    in.gateB = grade::gateFor(other->family()).toVec4();
    compositePass_.draw(in);
}

void Renderer::render(double timeSeconds, GLuint targetFbo) {
    const float dt = beginFrame(timeSeconds);
    if (thermal_.tier() != appliedTier_) applyRenderScale();
    Scene* scene = resolveActiveScene();
    if (!scene || !ensureTargets()) {
        glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
        glViewport(0, 0, width_, height_);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }
    const SceneParams p = resolveParams(dt);
    resolveLayerScene();
    touchField_.step(dt);
    const float progress = drawSecondaryTargets(p, dt);
    drawSceneTarget(*scene, p, dt);
    composite(*scene, p, progress, targetFbo);
}

}  // namespace geode::viz
