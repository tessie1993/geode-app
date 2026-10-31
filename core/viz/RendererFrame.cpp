#include "viz/Renderer.hpp"

#include <algorithm>
#include <cmath>

#include "viz/CompositeGrade.hpp"
#include "viz/Quad.hpp"

namespace geode::viz {

namespace {
constexpr float kTouchMinOverlayStrength = 0.35f;

// The per-scene band trims (bassGain, midGain, trebGain) on the sound frame a scene reads: each band's
// level and att envelope, and its shaped low/middle/high.
GeodeSoundFrame gainAdjusted(const GeodeSoundFrame& f, const SceneParams& p) {
    if (p.bassGain == 1.0f && p.midGain == 1.0f && p.trebGain == 1.0f) return f;
    GeodeSoundFrame out = f;
    out.bass = f.bass * p.bassGain;
    out.bassAtt = f.bassAtt * p.bassGain;
    out.low = std::clamp(f.low * p.bassGain, 0.0f, 1.0f);
    out.mid = f.mid * p.midGain;
    out.midAtt = f.midAtt * p.midGain;
    out.middle = std::clamp(f.middle * p.midGain, 0.0f, 1.0f);
    out.treble = f.treble * p.trebGain;
    out.trebleAtt = f.trebleAtt * p.trebGain;
    out.high = std::clamp(f.high * p.trebGain, 0.0f, 1.0f);
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
    {
        // Before anything reads it this frame. No engine yet (nothing written) leaves the frame silent.
        std::lock_guard<std::mutex> lock(soundLock_);
        if (sound_) sound_->step(dt, soundFrame_);
    }
    std::vector<std::pair<std::string, std::string>> pending;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        pending.swap(pendingShaders_);
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
        motionField_.setVariant(styleVariantSeed(requestedId));
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
    const auto& envValues = adsr_.tick(dt, soundFrame_);
    AdsrEngine::lfoOffsets(adsr_.configs, envValues, envRate_, envDepth_);
    const auto& lfoValues = lfo_.tick(dt, soundFrame_, envRate_.data(), envDepth_.data());
    SceneParams p = lfo_.apply(displayedParams_, lfoValues);
    p = AdsrEngine::apply(p, adsr_.configs, envValues);
    // The reaction matrix: per-style routes from the sound frame to many
    // targets. Ahead of the motion field and the safety clamp, like the LFOs.
    reactions_.tick(soundFrame_, dt);
    p = reactions_.apply(p);
    // The continuous motion system: the one stage every family's parameters
    // pass through, fed the sound frame (never a single drum hit or PCM
    // impulse), ahead of the safety clamp so nothing it adds can exceed the
    // flash and motion limits.
    motionField_.step(soundFrame_, dt);
    const bool reducedMotion = reducedMotion_.load(std::memory_order_relaxed);
    p = motionField_.apply(p, reducedMotion, sceneSpin_.load(std::memory_order_relaxed));
    p = safety::apply(p, reducedMotion);
    const bool userPostFx = userPostFx_.load(std::memory_order_relaxed);
    if (!thermalTierInfo(thermal_.tier()).optionalPasses || !userPostFx) {
        p.flowEnabled = false;
        p.rippleOverlayEnabled = false;
    }
    if (!userPostFx) p.fluidBloom = false;
    lastFinalParams_ = p;
    postRotationAngle_ = grade::integrateRotation(postRotationAngle_, p.rotation, dt);
    postCyclePhase_ = grade::integrateCyclePhase(postCyclePhase_, p.cycleSpeed, dt, p.colorCycle);
    // Wave three: nothing drives the post pass's beat pulse any more, since
    // it existed to ride the transient FormDrive read.
    postBeatPulse_ = grade::integrateBeatPulse(postBeatPulse_, 0.0f, dt);
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
        layerScene_->update(gainAdjusted(soundFrame_, p), dt);
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
            outgoingScene_->update(gainAdjusted(soundFrame_, op), dt);
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
    scene.update(gainAdjusted(soundFrame_, p), dt);
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
    // more (that read live::hit(), a transient flag); flash/strobe/pulse/
    // shake themselves are already inert (see Params.hpp).
    in.hitImpulse = 0.0f;
    const SceneParams& fx = lastFinalParams_;
    in.flash = fx.flash * flashBudget_.gainFor(timeSeconds_, safety::flashImpulse(fx.flash, 0.0f));
    in.strobeHz = safety::strobeHz();
    in.postRotationAngle = postRotationAngle_;
    in.postCyclePhase = postCyclePhase_;
    in.postBeatPulse = postBeatPulse_;
    in.quadVao = quadVao_;
    in.fx = fx;
    in.gateA = grade::gateFor(activeScene_->family()).toVec4();
    Scene* other = layerScene_ ? layerScene_ : outgoingScene_ ? outgoingScene_ : activeScene_;
    in.gateB = grade::gateFor(other->family()).toVec4();
    // The rig's flat projection, for every scene that does not march the 3D pose itself.
    const CameraPose& cam = camera_.pose();
    in.camFlat = cam.flat;
    in.camTilt = cam.tilt;
    in.camGate = {activeScene_->drawsCamera() ? 0.0f : 1.0f, other->drawsCamera() ? 0.0f : 1.0f};
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
    if (overlays_.wantsFlow(p, scene.isFluid())) overlays_.stepFlow(gainAdjusted(soundFrame_, p), dt, p);
    smearing_ = overlays_.smearing(monotonicSeconds());
    // Stepped once per frame ahead of every draw so each scene reads the same anchor.
    touchField_.step(dt);
    overlays_.drainTouchStrokes(scene);
    rippleOverlayOn_ = overlays_.rippleOverlayActive(p, smearing_, scene.isWater());
    if (rippleOverlayOn_) overlays_.stepRippleOverlay(gainAdjusted(soundFrame_, p), p, dt);
}

// After stepOverlays, so the look follows this frame's touch anchor.
void Renderer::stepCamera(const SceneParams& p, float dt) {
    CameraRig::LookInput look;
    if (p.camGyro) {
        look.yaw = lookYaw_.load(std::memory_order_relaxed);
        look.pitch = lookPitch_.load(std::memory_order_relaxed);
    }
    if (p.camTouch) {
        const float s = touchField_.anchorStrength();
        look.yaw += touchField_.anchorX() * s;
        look.pitch += touchField_.anchorY() * s;
    }
    look.yaw = std::clamp(look.yaw, -1.0f, 1.0f);
    look.pitch = std::clamp(look.pitch, -1.0f, 1.0f);
    camera_.step(p, look, dt, reducedMotion_.load(std::memory_order_relaxed), sceneSpin_.load(std::memory_order_relaxed) != 0.0f,
                 &soundFrame_);
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
    if (thermal_.tier() != appliedTier_ || userScale_.load(std::memory_order_relaxed) != appliedUserScale_) {
        applyRenderScale();
    }
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
    stepCamera(p, dt);
    const float progress = drawSecondaryTargets(p, dt);
    drawSceneTarget(*scene, p, dt);
    composite(*scene, p, progress, targetFbo);
}

}  // namespace geode::viz
