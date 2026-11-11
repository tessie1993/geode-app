#include "viz/Renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

#include "sound/DriveGain.hpp"
#include "sound/Form.hpp"
#include "viz/Catalogue.hpp"
#include "viz/CompositeGrade.hpp"
#include "viz/EndlessZoom.hpp"
#include "viz/FrameStep.hpp"
#include "viz/ParamRandomizer.hpp"
#include "viz/Quad.hpp"

namespace geode::viz {

namespace {
constexpr float kTouchMinOverlayStrength = 0.35f;
// Endless zoom fades in and out over this long when it is switched, on the wall clock.
constexpr float kDiveFadeSeconds = 0.6f;
// Liquid mode's hand and sound on the flow (clip units per second into FlowField's kicks), and the
// radius of each kind of kick: a drag moves the liquid about as fast as the finger.
constexpr float kLiquidPush = 1.0f;
constexpr float kLiquidSwirl = 2.4f;
constexpr float kPushRadius = 0.08f;
constexpr float kSwirlRadius = 0.07f;
// How hard the liquid sharpens its own back-trace, and how much extra the style's edges are re-poured.
constexpr float kLiquidSharpen = 0.35f;
constexpr float kLiquidEdge = 0.6f;
// About two seconds at 60 Hz.
constexpr int kFeedbackIdleFrames = 120;

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
    const float dt = frameStepSeconds(fixedFps_.load(std::memory_order_relaxed), lastTimeS_, timeSeconds);
    lastTimeS_ = timeSeconds;
    thermal_.onFrame(dt);
    {
        // Before anything reads it this frame. No engine yet (nothing written) leaves the frame silent.
        std::lock_guard<std::mutex> lock(soundLock_);
        // The engine settings, latched once per frame for the resolver.
        frameSettings_ = latchEngineSettings();
        // What the player told the engines (core/sound/PlayerSignals), live renderers only: an export hears
        // its own track, with no boundaries but its own and no player fades, so it keeps fadeGain at 1, and
        // counts as the player, so a silent stretch in the track holds the song like a pause does live.
        if (!offscreen_.load(std::memory_order_relaxed)) {
            const bool player = sound::player::heardAsPlayer(fedPlayersRing_.load(std::memory_order_relaxed),
                                                             sound::player::playerIsSource(),
                                                             layerAlone_.load(std::memory_order_relaxed));
            const sound::player::Boundary boundary = sound::player::boundary();
            if (boundary.epoch != boundarySeen_) {
                boundarySeen_ = boundary.epoch;
                if (sound_ && player) sound_->trackBoundary();
            }
            if (sound_) {
                sound_->setPlayerSource(player);
                sound_->setFadeGain(sound::player::fadeGain());
            }
        } else if (sound_) {
            sound_->setPlayerSource(true);
        }
        if (sound_) {
            sound_->step(dt, soundFrame_);
            soundEmotion_ = sound_->emotion();
        }
        soundHeard_ = sound_ != nullptr;
    }
    // No sound, no movement: the picture's clock is the frame's step times the music's rate. A renderer
    // nothing feeds stays still.
    musicRate_ = soundHeard_ ? sound::motionRate(soundFrame_) : 0.0f;
    timeSeconds_ = std::fmod(timeSeconds_ + dt * musicRate_, kTimeWrapSeconds);
    applyStyleSchedule();
    std::vector<std::pair<std::string, std::string>> pending;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        pending.swap(pendingShaders_);
        syncLayerLocked();
        frameLayerMix_ = layerMix_;
        frameLayerBlend_ = layerBlend_;
        frameTransitionId_ = transitionId_;
        frameTransitionDurationMs_ = transitionDurationMs_;
        // The samples heard since the last frame reach this frame's scenes once; none heard, no PCM (PcmHandoff).
        framePcmCount_ = pcm_.take(pcmDeliverScratch_.data(), pcmSideScratch_.data());
    }
    for (const auto& [id, src] : pending) {
        if (Scene* scene = builtScene(id)) scene->setFragmentSource(src);
    }
    applyMilkRequests();
    applyUnderlayUpload();
    return dt;
}

Scene* Renderer::resolveActiveScene() {
    std::string requestedId;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        requestedId = requestedSceneId_;
    }
    // Live, a scene not built yet comes back null for a few frames while it is staged; the frame
    // keeps drawing activeScene_ (or a clear when there is none) until it is whole.
    Scene* requested = readyScene(requestedId);
    requestedPending_ = requested == nullptr;
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
        // A new style gets about a second of draws even in silence: its build and first load span frames.
        redrawFrames_ = 60;
        // The resolver's motion field takes the new style's variant at its next step.
        styleSwitchPending_ = true;
    }
    return activeScene_;
}

SceneParams Renderer::resolveParams(float dt, float wallDt) {
    SceneParams requested;
    {
        // The frame latch: the shell's latest params, morph and configs reach the resolver as copies.
        std::lock_guard<std::mutex> lock(stateLock_);
        requested = requestedParams_;
        if (morphRequested_) {
            resolver_.beginMorph(morphRequestSec_);
            morphRequested_ = false;
        }
        if (lfoConfigsDirty_) {
            resolver_.setLfoConfigs(lfoConfigs_);
            lfoConfigsDirty_ = false;
        }
        if (adsrConfigsDirty_) {
            resolver_.setAdsrConfigs(adsrConfigs_);
            adsrConfigsDirty_ = false;
        }
        if (reactionRoutesDirty_) {
            resolver_.setReactionRoutes(reactionRoutes_);
            reactionRoutesDirty_ = false;
        }
    }
    signal::StyleContext style;
    style.id = activeScene_ ? &activeScene_->id() : nullptr;
    style.spin = sceneSpin_.load(std::memory_order_relaxed);
    style.switched = styleSwitchPending_;
    styleSwitchPending_ = false;
    // The feeling comes from the engine, stepped on the sample clock, so it is the same at every frame rate
    // and in an export. A new seed is a new play: the director forgets the song, the feeling too.
    const sound::Emotion::State* heard = nullptr;
    if (soundHeard_) {
        if (emotionSeeded_ && frameSettings_.director.seed != emotionSeed_) {
            std::lock_guard<std::mutex> lock(soundLock_);
            if (sound_) {
                sound_->resetEmotion();
                soundEmotion_ = sound_->emotion();
            }
        }
        emotionSeed_ = frameSettings_.director.seed;
        emotionSeeded_ = true;
        heard = &soundEmotion_;
    }
    // The chain itself, param fade to safety clamp (signal/Resolver).
    // The band trims reach the whole chain, not only the scene's own read of the frame: the reactions,
    // the motion field and the director hear the bass the user turned up.
    const signal::Resolver::Frame& out =
        resolver_.step(requested, gainAdjusted(soundFrame_, requested), dt, frameSettings_, style, heard, wallDt);
    if (out.styleMoments > 0) directorStyleMoments_.fetch_add(out.styleMoments, std::memory_order_acq_rel);
    SceneParams p = out.params;
    // The device's masks, after the clamp: the thermal tier and Settings › Performance.
    const bool userPostFx = userPostFx_.load(std::memory_order_relaxed);
    if (!thermalTierInfo(thermal_.tier()).optionalPasses || !userPostFx) p.rippleOverlayEnabled = false;
    // Liquid mode is a mode the user switched on, not an optional pass: a hot phone simplifies or pauses
    // it as Settings › Performance says, and says so (liquidMode), but never drops it silently.
    const auto hot = liquid::hotPolicyFrom(liquidHotPolicy_.load(std::memory_order_relaxed));
    liveLiquid_ = p.flowEnabled ? liquid::modeFor(thermal_.tier(), hot) : liquid::Mode::Off;
    liquidMode_.store(static_cast<int>(liveLiquid_), std::memory_order_relaxed);
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
    // Who asked the stager for what: the layer resolves first in a frame, the next style later (in
    // drawLiquid), so the layer reads the next style's ask from the frame before.
    bornAskedBefore_ = std::move(bornAsked_);
    bornAsked_.clear();
    layerAsked_.clear();
    std::string layerId;
    std::string requestedId;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        layerId = layerSceneId_;
        requestedId = requestedSceneId_;
    }
    layerScene_ = nullptr;
    // The layer is staged too, but only once the requested scene is built: one target at a time.
    if (outgoingScene_ || layerId.empty() || layerId == requestedId || requestedPending_) return;
    layerAsked_ = layerId;
    Scene* layer = readySecondary(layerId, bornAskedBefore_);
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
    // beginFrame took this frame's block (if any) into the scratch buffer; acceptPcm() runs unlocked.
    if (framePcmCount_ > 0) scene.acceptPcmStereo(pcmDeliverScratch_.data(), pcmSideScratch_.data(), framePcmCount_);
}

void Renderer::bindSecondaryTarget() {
    glBindFramebuffer(GL_FRAMEBUFFER, fboB_.fbo());
    glViewport(0, 0, renderWidth_, renderHeight_);
    glClear(GL_COLOR_BUFFER_BIT);
}

float Renderer::drawSecondaryTargets(const SceneParams& p, float dt) {
    float progress = 1.0f;
    if (layerScene_ && holds(*layerScene_, heldLayer_)) {
        // fboB_ keeps the layer's last frame.
    } else if (layerScene_) {
        heldLayer_ = layerScene_;
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

// A simulation, particle or MilkDrop style steps once per draw whatever the step, so a stopped clock keeps
// the frame it last drew. A shader style is a function of the clock and redraws the same picture.
bool Renderer::holds(const Scene& scene, const Scene* held) const {
    return frameStill_ && held == &scene && scene.family() != SceneFamily::Shader;
}

void Renderer::drawSceneTarget(Scene& scene, const SceneParams& p, float dt) {
    if (!sceneJustSwitched_ && holds(scene, heldScene_)) return;
    heldScene_ = &scene;
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

void Renderer::composite(const SceneParams& p, float progress, GLuint targetFbo, GLuint liquidTex) {
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, width_, height_);
    glDisable(GL_BLEND);
    CompositePass::Inputs& in = compositeInputs_;
    in.texA = fboA_.tex();
    in.texB = fboB_.tex();
    // Liquid mode shows the liquid in place of the scene target; the composite's old flow nudge is retired.
    if (liquidTex != 0) in.texA = liquidTex;
    in.flowTex = compositePass_.zeroTex();
    in.flowStrength = 0.0f;
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
    // uPostFlash rides uBeat, which is hitImpulse: 0 since wave three, so this term draws nothing.
    in.flash = fx.flash;
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
}

void Renderer::stepOverlays(Scene& scene, const SceneParams& p, float dt) {
    smearing_ = overlays_.smearing(monotonicSeconds());
    // Stepped once per frame ahead of every draw so each scene reads the same anchor.
    touchField_.step(dt);
    // Smear: the fingers the touch field holds drag the picture too (viz/TouchSmear), drained just below.
    const touch::SmearSettings smear{smearOn_.load(std::memory_order_relaxed), smearStrength_.load(std::memory_order_relaxed)};
    const double now = monotonicSeconds();
    smearTrail_.step(touchField_.points(), dt, smear, [&](const touch::SmearStroke& st) {
        overlays_.queueTouchStroke(st.nx, st.ny, st.ndx, st.ndy, st.dt, st.strength, now);
    });
    // In Liquid mode a finger moves the liquid (stepLiquid) instead of dropping ripples.
    overlays_.drainTouchStrokes(scene, liquidOn());
    stepLiquid(scene, p, dt);
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
    camera_.setMoves(resolver_.frame().cameraMoves);
    camera_.step(p, look, dt, frameSettings_.reducedMotion,
                 sceneSpin_.load(std::memory_order_relaxed) != 0.0f, &soundFrame_);
}

void Renderer::wireFlow(Scene& target, const SceneParams& p) {
    fluid::FlowField* ff = overlays_.flow();
    if (liquidOn() && ff) {
        target.setFlow(ff->available() ? ff->velocityTex() : compositePass_.zeroTex(), p.flowStrength);
    } else {
        target.setFlow(compositePass_.zeroTex(), 0.0f);
    }
}

void Renderer::render(double timeSeconds, GLuint targetFbo) {
    const float wallDt = beginFrame(timeSeconds);
    const float dt = wallDt * musicRate_;
    if (thermal_.tier() != appliedTier_ || userScale_.load(std::memory_order_relaxed) != appliedUserScale_) {
        applyRenderScale();
    }
    // Memory pressure (#854): shed the spare scenes on this GL thread; what is drawn is pinned. This reaches
    // a surface that is still drawing; one that pauses sheds them at the pause (trimSpares).
    if (SceneResidency::trimSignal().poll(seenTrim_)) trimScenes({}, MemoryTrim::UiHidden);
    Scene* scene = resolveActiveScene();
    // Staging, a fresh switch, a transition's two scenes or a secondary's compiles: not the phone's pace.
    // A target that gave up staging (ScenePrep::Block) and still will not build is not settling.
    const bool staging = requestedPending_ && prep_.active() && prep_.attempts() < ScenePrep::kMaxAttempts;
    if (staging || sceneJustSwitched_ || outgoingScene_ || !stager_.idle()) thermal_.noteSettling();
    if (!offscreen_.load(std::memory_order_relaxed)) logResidency(wallDt);
    if (!scene || !ensureTargets()) {
        glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
        glViewport(0, 0, width_, height_);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }
    const SceneParams p = resolveParams(dt, wallDt);
    applyPendingFluidInjection();
    resolveLayerScene();
    if (!layerScene_) heldLayer_ = nullptr;
    // Still: the clock is stopped and nothing the scenes draw from changed since the last frame.
    std::array<float, SceneParams::kFieldCount> wire;
    p.toWire(wire.data());
    const bool sameLook = wire == heldWire_ && heldWidth_ == renderWidth_ && heldHeight_ == renderHeight_;
    if (redrawFrames_ > 0) redrawFrames_--;
    frameStill_ = !(dt > 0.0f) && sameLook && redrawFrames_ == 0 && !outgoingScene_;
    heldWire_ = wire;
    heldWidth_ = renderWidth_;
    heldHeight_ = renderHeight_;
    stepOverlays(*scene, p, dt);
    stepCamera(p, dt);
    const float progress = drawSecondaryTargets(p, dt);
    // A shader style paints its whole frame, so its Trails cannot live in the scene target: they are
    // the feedback pass's sustain. The particle styles keep their own trail (trailRetention). A paused
    // Liquid holds the pass's targets.
    const bool trails = liveLiquid_ == liquid::Mode::Off && p.trails && scene->family() == SceneFamily::Shader &&
                        scene->trailRetention(p) <= 0.0f && liquidPass_.ensure(renderWidth_, renderHeight_);
    if (!trails) trailsWereOn_ = false;
    // A paused Liquid keeps its place, so only Off counts as unused.
    if (trails || liveLiquid_ != liquid::Mode::Off) {
        feedbackIdleFrames_ = 0;
    } else if (++feedbackIdleFrames_ == kFeedbackIdleFrames) {
        liquidPass_.releaseTargets();
        bornFbo_.release();
    }
    drawSceneTarget(*scene, p, dt);
    GLuint shown = trails ? drawTrails(p, dt, wallDt) : drawLiquid(*scene, p, dt);
    shown = drawDive(*scene, p, dt, wallDt, shown);
    composite(p, progress, targetFbo, shown);
    drawEdgeGlow(targetFbo, timeSeconds, dt);
    if (compositeHook_) compositeHook_(targetFbo, width_, height_);
    drawUiBlur(targetFbo);
}

void Renderer::drawUiBlur(GLuint targetFbo) {
    if (offscreen_.load(std::memory_order_relaxed) || !uiBlurOn_.load(std::memory_order_relaxed)) return;
    UiBlurRegion region;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        region = uiBlurRegion_;
    }
    uiBlurPass_.draw(region, targetFbo, width_, height_, quadVao_);
}

void Renderer::drawEdgeGlow(GLuint targetFbo, double timeSeconds, float dt) {
    // Off, the pass still hears the switch, so turning it on again starts its envelope from rest.
    if (!edgeGlowOn_.load(std::memory_order_relaxed)) {
        edgeGlowPass_.setOptions(frame::edge_glow::Options{});
        return;
    }
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        edgeGlowPass_.setOptions(edgeGlowOptions_);
    }
    // The renderer has no surface kind of its own yet: the wallpaper is the only caller that turns the
    // glow on, so an on glow means a wallpaper frame, unless this renderer is an export's.
    const frame::Surface surface =
        offscreen_.load(std::memory_order_relaxed) ? frame::Surface::Export : frame::Surface::Wallpaper;
    const frame::FrameCtx ctx{width_, height_, timeSeconds, dt, surface, &soundFrame_};
    if (!edgeGlowPass_.wants(ctx)) return;
    edgeGlowPass_.prepare(ctx);
    edgeGlowPass_.draw(frame::PassInputs{}, frame::PassTarget{targetFbo, width_, height_});
}

void Renderer::stepLiquid(Scene& scene, const SceneParams& p, float dt) {
    if (!liquidOn()) {
        // Paused keeps its place: the liquid comes back as it was once the phone cools.
        if (liveLiquid_ == liquid::Mode::Off) liquidWasOn_ = false;
        return;
    }
    if (!liquidWasOn_) {
        // Truly random each play (the user's call): the device's entropy, never the clock or the track.
        std::random_device rd;
        const uint64_t seed = (static_cast<uint64_t>(rd()) << 32) ^ rd();
        liquidDive_.reseed(seed);
        bornClock_.reseed(seed * 0x9E3779B97F4A7C15ull + 1);
        liquidGestures_.reset();
        liquidPass_.reset();
        bornRolled_ = false;
        bornSceneId_.clear();
        liquidWasOn_ = true;
    }
    const liquid::Plan plan = liquid::planFor(liveLiquid_);
    // The fingers down now (TouchField keeps released ones fading at strength < 1).
    const auto& pts = touchField_.points();
    float xy[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int fingers = 0;
    for (int i = 0; i < TouchField::kMaxPoints; i++) {
        const size_t base = static_cast<size_t>(i * TouchField::kPointStride);
        if (pts[base + 2] < 0.999f) break;
        if (fingers < 2) {
            xy[fingers * 2] = pts[base];
            xy[fingers * 2 + 1] = pts[base + 1];
        }
        fingers++;
    }
    const liquid::GestureOut g = liquidGestures_.step(std::min(fingers, 2), xy, dt);
    const float aspect = static_cast<float>(renderWidth_) / static_cast<float>(std::max(renderHeight_, 1));
    const bool threeD = scene.drawsCamera();
    const bool reducedMotion = frameSettings_.reducedMotion;
    const float rate = threeD ? 0.0f : std::clamp(p.endlessZoomSpeed, 0.0f, endless::kMaxSpeed) * diveFollow_.rate();
    liquidFrame_ = liquidDive_.step(dt, rate, g.steerX * aspect, g.steerY, g.steerWeight, reducedMotion);
    if (threeD) {
        liquidFrame_.scale = 1.0f;
        liquidFrame_.twist = 0.0f;
    }
    const bool trigger = soundFrame_.phrase > 0.5f || sound::sectionChanged(soundFrame_);
    if (bornClock_.step(dt, trigger, soundFrame_.beatConf > 0.35f)) {
        bornRolled_ = false;
        bornSceneId_.clear();
    }
    fluid::FlowField* ff = overlays_.flow();
    if (ff == nullptr || !ff->available() || plan.velocityGrid <= 0) return;
    ff->setGrid(plan.velocityGrid, plan.macCormack);
    const float force = std::clamp(p.flowForce, 0.0f, 3.0f);
    if (g.push) ff->queueKick(g.x, g.y, g.vx * kLiquidPush * force, g.vy * kLiquidPush * force, kPushRadius);
    if (std::fabs(g.swirl) > 0.02f) ff->queueVortex(g.x, g.y, g.swirl * kLiquidSwirl * force, kSwirlRadius);
    // The mids drive the swirl: Swirl is what a full mid band reaches.
    SceneParams fp = p;
    fp.flowCurl = std::clamp(p.flowCurl, 0.0f, 50.0f) * (0.4f + 0.6f * std::clamp(soundFrame_.middle, 0.0f, 1.0f));
    overlays_.stepFlow(gainAdjusted(soundFrame_, p), dt, fp);
}

GLuint Renderer::drawBorn(Scene& scene, const SceneParams& p, float dt) {
    const liquid::Born born = liquid::bornFrom(p.liquidBorn);
    // A re-roll and the next style each draw a whole second scene, so only the full liquid affords them.
    if (liveLiquid_ != liquid::Mode::Full || born == liquid::Born::Live || born == liquid::Born::Forms) return fboA_.tex();
    if (!bornFbo_.ensure(renderWidth_, renderHeight_)) return fboA_.tex();
    if (born == liquid::Born::Rerolled) {
        // A stateful family (MilkDrop, the fluid) cannot draw twice in one frame without stepping twice.
        if (scene.family() != SceneFamily::Shader) return fboA_.tex();
        if (!bornRolled_) {
            const uint64_t seed = bornClock_.rollSeed();
            bornParams_ = randomizer::randomize(p, seed, randomizer::Tab::Shape, scene.id(), {});
            bornParams_ = randomizer::randomize(bornParams_, seed ^ 0x5DEECE66Dull, randomizer::Tab::Color, scene.id(), {});
            bornRolled_ = true;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, bornFbo_.fbo());
        glViewport(0, 0, renderWidth_, renderHeight_);
        glClear(GL_COLOR_BUFFER_BIT);
        scene.setParams(bornParams_);
        scene.draw(timeSeconds_);
        scene.setParams(p);
        return bornFbo_.tex();
    }
    // The next style: a flat shader style the born clock picked, staged like the layer scene, one target
    // at a time. The one it replaces is released, so a long play does not keep every style it passed.
    if (requestedPending_ || outgoingScene_ != nullptr) return fboA_.tex();
    if (bornSceneId_.empty()) {
        std::vector<std::string> pool;
        for (const std::string& id : catalogue::allIds()) {
            if (id != scene.id() && catalogue::isShaderStyle(id) && !catalogue::isMarched(id) && registry_.knows(id)) pool.push_back(id);
        }
        if (pool.empty()) return fboA_.tex();
        bornSceneId_ = pool[static_cast<size_t>(bornClock_.rollSeed() % pool.size())];
        if (!lastBornSceneId_.empty() && lastBornSceneId_ != bornSceneId_) dropScene(lastBornSceneId_);
        lastBornSceneId_ = bornSceneId_;
    }
    bornAsked_ = bornSceneId_;
    Scene* next = readySecondary(bornSceneId_, layerAsked_);
    if (next == nullptr || next == &scene || next == layerScene_) return fboA_.tex();
    glBindFramebuffer(GL_FRAMEBUFFER, bornFbo_.fbo());
    glViewport(0, 0, renderWidth_, renderHeight_);
    glClear(GL_COLOR_BUFFER_BIT);
    next->setParams(p);
    deliverPcm(*next);
    next->update(gainAdjusted(soundFrame_, p), dt);
    next->draw(timeSeconds_);
    return bornFbo_.tex();
}

GLuint Renderer::drawTrails(const SceneParams& p, float dt, float wallDt) {
    namespace motion = liquid::motion;
    if (!trailsWereOn_) {
        // Starts from this frame's style: an old pour must not show.
        liquidPass_.reset();
        trailsWereOn_ = true;
    }
    // The motion is per second and already held to the silence rule, so it steps on the wall clock;
    // the frame clock (dt) has that rule in it too and would apply it twice.
    const motion::Motion heard = motion::trailMotion({p.trailZoom, p.trailWarp}, soundFrame_, frameSettings_.reducedMotion);
    liquid::LiquidPass::Frame f;
    f.styleTex = fboA_.tex();
    f.bornTex = fboA_.tex();
    f.velocityTex = compositePass_.zeroTex();
    f.styleWidth = renderWidth_;
    f.styleHeight = renderHeight_;
    f.dt = dt;
    f.motion = motion::perFrame(heard, wallDt);
    f.inject = endless::feedback::sustainInject(p.trailLength, dt);
    f.edge = kLiquidEdge;
    f.sharpen = kLiquidSharpen;
    f.bornGain = 0.0f;
    f.feedback = true;
    f.time = timeSeconds_;
    f.quadVao = quadVao_;
    return liquidPass_.run(f);
}

GLuint Renderer::drawDive(const Scene& scene, const SceneParams& p, float dt, float wallDt, GLuint shown) {
    const float wall = std::isfinite(wallDt) ? std::clamp(wallDt, 0.0f, 0.1f) : 0.0f;
    diveFollow_.step(soundFrame_, wall, p.audioDrive);
    // Liquid mode's liquid has a dive of its own on a flat style (LiquidMath's Dive), which takes Dive
    // speed there; a 3D style in Liquid mode has none, so the stage is its dive.
    const bool flat = !scene.drawsCamera();
    const bool liquid3d = liquidOn() && !flat;
    const bool zoom = (p.endlessZoom && !(liquidOn() && flat)) || liquid3d;
    // Travel zoom: a flat style's Fly speed (its sign too) travels the same stage.
    const float fly = flat && !liquidOn() ? CameraRig::flatDive(p) : 0.0f;
    const bool on = zoom || fly != 0.0f;
    const bool was = diveMix_ > 0.0f;
    diveMix_ = std::clamp(diveMix_ + (on ? wall : -wall) / kDiveFadeSeconds, 0.0f, 1.0f);
    if (!(diveMix_ > 0.0f)) {
        divePhase_ = 0.0f;
        if (was) divePass_.releaseTarget();
        return shown;
    }
    // dt is the music's clock: no sound, no travel.
    if (on) divePhase_ = endless::advance(divePhase_, endless::travel((zoom ? p.endlessZoomSpeed : 0.0f) + fly, diveFollow_), dt);
    const bool plain = shown == 0;
    const GLuint dived =
        divePass_.run(plain ? fboA_.tex() : shown, plain ? renderWidth_ : liquidPass_.width(),
                      plain ? renderHeight_ : liquidPass_.height(), renderWidth_, renderHeight_, divePhase_,
                      diveMix_ * diveMix_ * (3.0f - 2.0f * diveMix_), quadVao_);
    return dived != 0 ? dived : shown;
}

GLuint Renderer::drawLiquid(Scene& scene, const SceneParams& p, float dt) {
    if (!liquidOn()) return 0;
    const liquid::Plan plan = liquid::planFor(liveLiquid_);
    const int w = std::max(static_cast<int>(static_cast<float>(renderWidth_) * plan.liquidScale), 1);
    const int h = std::max(static_cast<int>(static_cast<float>(renderHeight_) * plan.liquidScale), 1);
    if (!liquidPass_.ensure(w, h)) return 0;
    fluid::FlowField* ff = overlays_.flow();
    const bool flowing = ff != nullptr && ff->available() && plan.velocityGrid > 0;
    const bool threeD = scene.drawsCamera();
    const liquid::Born born = liquid::bornFrom(p.liquidBorn);
    liquid::LiquidPass::Frame f;
    f.styleTex = fboA_.tex();
    f.bornTex = threeD ? fboA_.tex() : drawBorn(scene, p, dt);
    f.velocityTex = flowing ? ff->velocityTex() : compositePass_.zeroTex();
    f.velScaleX = flowing ? ff->velScaleX() : 0.0f;
    f.velScaleY = flowing ? ff->velScaleY() : 0.0f;
    f.styleWidth = renderWidth_;
    f.styleHeight = renderHeight_;
    f.dt = dt;
    f.motion = liquid::motion::fromDive(liquidFrame_, static_cast<float>(w) / static_cast<float>(h));
    f.vpX = liquidFrame_.vpX;
    f.vpY = liquidFrame_.vpY;
    const float liquidity = std::clamp(p.flowStrength, 0.0f, 1.0f);
    f.inject = liquid::injectWeight(liquidity, dt);
    f.edge = kLiquidEdge * (1.0f - 0.5f * liquidity);
    f.sharpen = liveLiquid_ == liquid::Mode::Full ? kLiquidSharpen : 0.0f;
    f.bornRadius = liquid::bornRadius(liquidity);
    // A 3D style dives by flying its camera; nothing is born at a vanishing point it does not have.
    f.bornGain = threeD ? 0.0f : bornClock_.gain();
    f.bornMode = born == liquid::Born::Forms ? 3 : 0;
    f.age = static_cast<int>(liquid::ageFrom(p.liquidAge));
    f.surface = static_cast<int>(liquid::surfaceFrom(p.liquidSurface));
    f.feedback = plan.feedback;
    f.shimmer = std::clamp(soundFrame_.high, 0.0f, 1.0f) * 0.8f;
    f.light = std::clamp(soundFrame_.level, 0.0f, 1.0f);
    f.time = timeSeconds_;
    f.quadVao = quadVao_;
    return liquidPass_.run(f);
}

}  // namespace geode::viz
