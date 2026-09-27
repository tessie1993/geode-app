#include "viz/Renderer.hpp"

#include <algorithm>
#include <cmath>

#include "viz/CompositeGrade.hpp"
#include "viz/Quad.hpp"

namespace geode::viz {

namespace {
constexpr float kTouchMinOverlayStrength = 0.35f;

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
        frameLayerMix_ = layerMix_;
        frameLayerBlend_ = layerBlend_;
        frameTransitionId_ = transitionId_;
        frameTransitionDurationMs_ = transitionDurationMs_;
    }
    for (const auto& [id, src] : pending) {
        if (Scene* scene = builtScene(id)) scene->setFragmentSource(src);
    }
    applyMilkRequests();
    applyOverlayUploads();
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
        const bool cuts = TransitionCatalog::builtIn(frameTransitionId_) == TransitionStyle::Cut;
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
    // std::max propagates a NaN, and a NaN fade sends the lerp below to k = NaN, which turns
    // every interpolated parameter into a NaN that then feeds back through displayedParams_ on
    // the next frame and never recovers. SceneParams::set now rejects non-finite input, so this
    // is belt-and-braces for morph state and for params set before that guard existed.
    float fade = std::max(requested.paramFadeSec, morph);
    if (!std::isfinite(fade)) fade = 0.0f;
    displayedParams_ = fade <= 0.01f ? requested : lerpParams(displayedParams_, requested, std::clamp(dt / fade, 0.0f, 1.0f));
    const auto& envValues = adsr_.tick(dt, frameFeatures_);
    AdsrEngine::lfoOffsets(adsr_.configs, envValues, envRate_, envDepth_);
    const auto& lfoValues = lfo_.tick(dt, frameFeatures_, envRate_.data(), envDepth_.data());
    SceneParams p = lfo_.apply(displayedParams_, lfoValues);
    p = AdsrEngine::apply(p, adsr_.configs, envValues);
    // The continuous motion system: the one stage every family's parameters
    // pass through, fed only the feature frame (never a one-hop PCM/drum
    // impulse), ahead of the safety clamp so nothing it adds can exceed the
    // flash and motion limits.
    motionField_.step(frameFeatures_, dt);
    const bool reducedMotion = reducedMotion_.load(std::memory_order_relaxed);
    p = motionField_.apply(p, reducedMotion);
    p = safety::apply(p, reducedMotion);
    if (!thermalTierInfo(thermal_.tier()).optionalPasses) {
        p.flowEnabled = false;
        p.rippleOverlayEnabled = false;
    }
    lastFinalParams_ = p;
    postRotationAngle_ = grade::integrateRotation(postRotationAngle_, p.rotation, dt);
    postCyclePhase_ = grade::integrateCyclePhase(postCyclePhase_, p.cycleSpeed, dt, p.colorCycle);
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
    // Copy out under the lock, then hand the scene its own scratch buffer
    // once unlocked: acceptPcm() (a copy of up to 4096 samples plus
    // fillPcmRow) must never run while stateLock_ is held, or pushPcm() on
    // the PCM producer thread blocks behind a scene upload.
    int count = 0;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        count = pcmCount_;
        if (count > 0) std::copy(pcm_.begin(), pcm_.begin() + count, pcmDeliverScratch_.begin());
    }
    if (count > 0) scene.acceptPcm(pcmDeliverScratch_.data(), count);
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
        wireFlow(*layerScene_, p);
        layerScene_->setParams(p);
        deliverPcm(*layerScene_);
        layerScene_->update(gainAdjusted(frameFeatures_, p), dt);
        layerScene_->draw(timeSeconds_);
    }
    if (outgoingScene_) {
        progress = std::clamp(static_cast<float>((frameNowS_ - transitionStartS_) * 1000.0 / frameTransitionDurationMs_), 0.0f, 1.0f);
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
    wireFlow(scene, p);
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
        fluid::FlowField* ff = overlays_.flow();
        if (const GLuint own = scene.velocityTexture(); own != 0) {
            flowTex = own;
            flowStrength = p.flowStrength;
        } else if (ff && ff->available()) {
            flowTex = ff->velocityTex();
            flowStrength = p.flowStrength;
        }
    }
    in.flowTex = flowTex;
    in.flowStrength = flowStrength;
    fluid::RippleSim* ripple = overlays_.ripple();
    const bool rippleOn = rippleOverlayOn_ && ripple != nullptr;
    in.rippleTex = rippleOn ? ripple->heightTex() : compositePass_.zeroTex();
    in.rippleTexelW = rippleOn ? ripple->texelW() : 0.0f;
    in.rippleTexelH = rippleOn ? ripple->texelH() : 0.0f;
    const float rippleStrength = smearing_ ? std::max(p.rippleOverlayStrength, kTouchMinOverlayStrength) : p.rippleOverlayStrength;
    in.rippleStrength = rippleOn ? std::clamp(rippleStrength, 0.0f, 1.0f) : 0.0f;
    in.rippleSpecular = rippleOn ? std::clamp(p.rippleOverlaySpecular, 0.0f, 1.0f) : 0.0f;
    in.progress = progress;
    in.layerMix = safety::layerMix(frameLayerMix_, blendModeFromOrdinal(frameLayerBlend_));
    in.blendOrdinal = frameLayerBlend_;
    in.hasLayer = layerScene_ != nullptr;
    in.hasOutgoing = outgoingScene_ != nullptr;
    in.transitionId = frameTransitionId_;
    in.transitionStyle = safety::transitionStyle(TransitionCatalog::builtIn(frameTransitionId_).value_or(TransitionStyle::Fade));
    in.ratio = static_cast<float>(renderWidth_) / static_cast<float>(renderHeight_);
    in.timeSeconds = timeSeconds_;
    // Wave three: nothing feeds the composite pass's transient reaction any
    // more (that read a raw transient/hit flag); flash/strobe/pulse/
    // shake themselves are already inert (see Params.hpp), and the composite
    // pass has dropped the uniforms/Inputs fields that carried them.
    const SceneParams& fx = lastFinalParams_;
    in.postRotationAngle = postRotationAngle_;
    in.postCyclePhase = postCyclePhase_;
    in.quadVao = quadVao_;
    in.fx = fx;
    in.gateA = grade::gateFor(activeScene_->family()).toVec4();
    Scene* other = layerScene_ ? layerScene_ : outgoingScene_ ? outgoingScene_ : activeScene_;
    in.gateB = grade::gateFor(other->family()).toVec4();
    // W00: read this frame's latched underlay straight from CompositePass (it owns the texture -
    // see CompositePass.hpp), the same way uFlow/uRipple above are read from Overlays.
    in.underlayTex = compositePass_.underlayTexOrZero();
    in.underlayBlend = compositePass_.underlayBlendMode();
    in.underlayAmount = compositePass_.underlayAmount();
    compositePass_.draw(in);
    // W00: overlay is its own draw call, over whatever composite() just wrote to targetFbo, so it
    // never enters postFx/transitions/Layers and comes out identical live, in the wallpaper and in
    // the offscreen export.
    compositePass_.drawOverlay(quadVao_);
}

void Renderer::stepOverlays(Scene& scene, const SceneParams& p, float dt) {
    if (overlays_.wantsFlow(p, scene.isFluid())) overlays_.stepFlow(gainAdjusted(frameFeatures_, p), dt, p, motionField_.state());
    smearing_ = overlays_.smearing(monotonicSeconds());
    // Stepped once per frame ahead of every draw so each scene reads the same anchor.
    touchField_.step(dt);
    overlays_.drainTouchStrokes(scene);
    rippleOverlayOn_ = overlays_.rippleOverlayActive(p, smearing_, scene.isWater());
    if (rippleOverlayOn_) overlays_.stepRippleOverlay(gainAdjusted(frameFeatures_, p), p, dt);
}

void Renderer::wireFlow(Scene& target, const SceneParams& p) {
    fluid::FlowField* ff = overlays_.flow();
    if (p.flowEnabled && ff) {
        target.setFlow(ff->available() ? ff->velocityTex() : compositePass_.zeroTex(), p.flowStrength);
    } else {
        target.setFlow(compositePass_.zeroTex(), 0.0f);
    }
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
    applyPendingFluidInjection();
    resolveLayerScene();
    stepOverlays(*scene, p, dt);
    const float progress = drawSecondaryTargets(p, dt);
    drawSceneTarget(*scene, p, dt);
    composite(*scene, p, progress, targetFbo);
}

}  // namespace geode::viz
