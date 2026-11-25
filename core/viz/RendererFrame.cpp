#include "viz/Renderer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <random>

#include "sound/DriveGain.hpp"
#include "sound/Form.hpp"
#include "viz/BandTrim.hpp"
#include "viz/passes/Births.hpp"
#include "viz/Catalogue.hpp"
#include "viz/passes/CompositeGrade.hpp"
#include "viz/passes/EndlessZoom.hpp"
#include "viz/FrameStep.hpp"
#include "viz/modulation/MotionField.hpp"
#include "viz/panel/ParamRandomizer.hpp"
#include "viz/gl/Quad.hpp"
#include "viz/gl/SceneTarget.hpp"
#include "viz/director/TransitionChoice.hpp"

namespace geode::viz {

namespace {
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
// Wall seconds the feedback pass may sit unused before its targets are freed.
constexpr float kFeedbackIdleSeconds = 2.0f;
// Wall seconds the UI blur may sit unused (no sheet over the visual) before its targets are freed.
constexpr float kUiBlurIdleSeconds = 1.0f;
// A sum of frame steps lands within this of its whole seconds (60 steps of 1/60 in float).
constexpr float kStepSumSlack = 1e-4f;
// A speaker that stands this long is paused (the sound engine's own starve time).
constexpr float kListenStillSeconds = 0.25f;

// Customize › Layers' Macro source: the turn of the knob it names (0: the first on screen) with what the
// waves and envelopes add this frame; NaN when the bank has no such knob on screen.
float layerKnobTurn(const signal::EngineSettings& settings, int id, const macrobank::KnobMods& mods) {
    if (settings.bank) {
        for (const macrobank::Knob& knob : settings.bank->knobs) {
            if (knob.shown && (id == 0 || knob.id == id)) return macrobank::effective(knob, &mods);
        }
    }
    return std::nanf("");
}
}  // namespace

float Renderer::beginFrame(double timeSeconds) {
    resetFrameState();
    frameNowS_ = timeSeconds;
    const float dt = frameStepSeconds(fixedFps_.load(std::memory_order_relaxed), lastTimeS_, timeSeconds);
    lastTimeS_ = timeSeconds;
    if (settleHoldSeconds_ > 0.0f) settleHoldSeconds_ -= dt;
    // "Adapt to frame time" (frame::FrameGovernor): this frame's step against the rate the pacer really paces to.
    // An export (offscreen, a fixed step) and a recording pass the user's settings through.
    {
        const bool held = offscreen_.load(std::memory_order_relaxed) || fixedFps_.load(std::memory_order_relaxed) > 0 ||
                          governorHeld_.load(std::memory_order_relaxed);
        frame::GovernorUser user;
        user.renderScale = userScale_.load(std::memory_order_relaxed);
        user.marchScale = marchScale_.load(std::memory_order_relaxed);
        user.postFx = userPostFx_.load(std::memory_order_relaxed);
        governor_.onFrame(dt, pacedFps(), ownCapFps_.load(std::memory_order_relaxed), user, settling(), held);
    }
    {
        // Before anything reads it this frame. No analysis yet (nothing heard) leaves the frame silent.
        std::lock_guard<std::mutex> lock(soundLock_);
        // The engine settings, latched once per frame for the resolver.
        frameSettings_ = latchEngineSettings();
        sound::SharedAnalysis* heard = sharedSound_ ? sharedSound_.get() : ownSound_.get();
        float fade = 1.0f;
        // What the player told the analysis (core/sound/PlayerSignals), live renderers only: an export hears
        // its own track, with no boundaries but its own and no player fades, so it keeps fadeGain at 1, and
        // counts as the player, so a silent stretch in the track holds the song like a pause does live.
        if (!offscreen_.load(std::memory_order_relaxed)) {
            const bool player = sound::player::heardAsPlayer(fedPlayersRing_.load(std::memory_order_relaxed),
                                                             sound::player::playerIsSource(),
                                                             layerAlone_.load(std::memory_order_relaxed));
            const sound::player::Boundary boundary = sound::player::boundary();
            if (boundary.epoch != boundarySeen_) {
                boundarySeen_ = boundary.epoch;
                if (heard && player) {
                    heard->trackBoundary(boundary.epoch);
                    sectionChangeOwed_ = true;
                }
            }
            if (heard) {
                heard->thread().setPlayerSource(player);
                heard->thread().setFadeGain(sound::player::fadeGain());
            }
            if (player) fade = sound::player::fadeGain();
        } else if (ownSound_) {
            // An export's analysis runs here, inline, on exactly the audio written up to this frame.
            ownSound_->thread().pump(dt);
            ownUnpumped_ = 0;
        }
        if (heard) readSound(*heard, dt, fade);
    }
    // No sound, no movement: the picture's clock is the frame's step times the music's rate. A renderer
    // nothing feeds stays still.
    musicRate_ = soundHeard_ ? sound::motionRate(soundFrame_) : 0.0f;
    timeSeconds_ = std::fmod(timeSeconds_ + dt * musicRate_, static_cast<double>(kTimeWrapSeconds));
    applyStyleSchedule(dt);
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
    applyUnderlayUpload();
    if (transition_choice::isRandom(frameTransitionId_) && nextRandomTransition_.empty()) {
        // Drawn and compiled a change ahead, so a library transition's build never lands on the switch frame.
        if (transitionRng_ == 0) {
            transitionRng_ = offscreen_.load(std::memory_order_relaxed)
                                 ? 0x5EEDull
                                 : static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) | 1u;
        }
        nextRandomTransition_ = transition_choice::pick(compositePass_.catalog().allIds(), switchTransitionId_, transitionRng_);
        compositePass_.warmTransition(nextRandomTransition_);
    }
    return dt;
}

void Renderer::readSound(const sound::SharedAnalysis& heard, float dt, float fadeGain) {
    const sound::AnalysisThread& analysis = heard.thread();
    const int64_t head = heard.ring().headFrames();
    // Live: the speaker's sample (never past what was written); an own ring: everything written to it.
    int64_t position = sharedSound_ ? std::min(listenPosition_, head) : head;
    listenStillSeconds_ = position == lastListen_ ? listenStillSeconds_ + dt : 0.0f;
    lastListen_ = position;
    // A paused speaker, once its writer has stopped and the analysis runs on silence: the frame is that silence, as
    // the renderer's own engine heard it, not the last sample played held forever. The hops the sink still holds
    // fire when the speaker plays them, not now.
    const bool paused =
        position >= 0 && listenStillSeconds_ > kListenStillSeconds && sound::interp::FrameReader::starved(analysis);
    if (paused) position = std::max(position, analysis.newestPosition());
    trimmedValid_ = false;
    const bool heardNow = paused ? soundReader_.readStill(analysis, soundFrame_, soundEmotion_, soundHits_)
                                 : soundReader_.read(analysis, position, soundFrame_, soundEmotion_, soundHits_);
    if (heardNow) soundHeard_ = true;
    if (sectionChangeOwed_ && soundHeard_) {
        // A boundary is the event (the engine's step fired it): Form was reset and cannot fire for eight bars.
        soundFrame_.sectionChange = 1.0f;
        soundHits_.at[offsetof(GeodeSoundFrame, sectionChange) / sizeof(float)] = position;
        sectionChangeOwed_ = false;
    }
    soundFrame_.fadeGain = std::isfinite(fadeGain) ? std::clamp(fadeGain, 0.0f, 1.0f) : 1.0f;
    if (paused) {
        std::fill(std::begin(soundFrame_.waveform), std::end(soundFrame_.waveform), 0.0f);
    } else {
        soundWave_.draw(soundFrame_.waveform);
    }
}

Scene* Renderer::resolveActiveScene() {
    std::string& requestedId = requestedIdScratch_;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        requestedId = requestedSceneId_;
    }
    // Picking a style that failed says why again; picking another takes that report down.
    if (requestedId != reportedRequestId_) {
        reportedRequestId_ = requestedId;
        if (prep_.failed(requestedId)) {
            fail(prep_.report(requestedId));
        } else if (prep_.reported(lastError())) {
            fail("");
        }
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
            if (transition_choice::isRandom(frameTransitionId_)) {
                switchTransitionId_ = nextRandomTransition_.empty() ? std::string("fade") : nextRandomTransition_;
                nextRandomTransition_.clear();
            } else {
                switchTransitionId_ = frameTransitionId_;
            }
            // An export replays its switches as the user made them: never Auto's longer fade.
            const bool automatic =
                !offscreen_.load(std::memory_order_relaxed) && transition_choice::takeAuto(requested->id(), seenAutoMark_);
            switchTransitionMs_ = transition_choice::durationMs(frameTransitionDurationMs_, automatic);
        }
        activeScene_ = requested;
        sceneJustSwitched_ = true;
        // A new style gets about a second of draws even in silence: its build and first load span frames.
        redrawSeconds_ = kRedrawSeconds;
        // The resolver's motion field takes the new style's variant at its next step.
        styleSwitchPending_ = true;
        // A new style costs what it costs: "Adapt to frame time" starts again from the user's own settings, its
        // backoff cleared (frame::FrameGovernor). The build and the blend are settling frames it does not count.
        governor_.reset();
    }
    return activeScene_;
}

const GeodeSoundFrame& Renderer::trimmedSound(const SceneParams& p) {
    if (bandTrimsAreIdentity(p)) return soundFrame_;
    const std::array<float, 3> key{p.bassGain, p.midGain, p.trebGain};
    if (!trimmedValid_ || key != trimmedKey_) {
        bandTrimInto(soundFrame_, p, trimmedSound_);
        trimmedKey_ = key;
        trimmedValid_ = true;
    }
    return trimmedSound_;
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
            sound::SharedAnalysis* heard = sharedSound_ ? sharedSound_.get() : ownSound_.get();
            if (heard) heard->newPlay(frameSettings_.director.seed);
        }
        emotionSeed_ = frameSettings_.director.seed;
        emotionSeeded_ = true;
        heard = &soundEmotion_;
    }
    // The chain itself, param fade to safety clamp (signal/Resolver).
    // The band trims reach the whole chain, not only the scene's own read of the frame: the reactions,
    // the motion field and the director hear the bass the user turned up.
    const signal::Resolver::Frame& out =
        resolver_.step(requested, trimmedSound(requested), dt, frameSettings_, style, heard, wallDt);
    if (out.styleMoments > 0) {
        // The intent first, so a take that sees the count finds the moment's intent with it.
        {
            std::lock_guard<std::mutex> guard(directorIntentLock_);
            directorIntent_ = resolver_.director().styleIntent();
        }
        directorStyleMoments_.fetch_add(out.styleMoments, std::memory_order_acq_rel);
    }
    // The knobs' live modulation for the shell's second arc: an on-screen surface's, never an export's.
    if (!offscreen_.load(std::memory_order_relaxed)) macrobank::publishLive(out.knobMods);
    SceneParams p = out.params;
    // Settings › Performance's extra effects, under the governor's steps.
    const bool userPostFx = governor_.governed().postFx;
    // Liquid mode is a mode the user switched on, not an optional pass.
    liveLiquid_ = p.flowEnabled ? liquid::Mode::Full : liquid::Mode::Off;
    liquidMode_.store(static_cast<int>(liveLiquid_), std::memory_order_relaxed);
    if (!userPostFx) p.fluidBloom = false;
    uploadPaletteRamp(p);
    stepShapeGraph(p, dt, wallDt);
    lastFinalParams_ = p;
    postRotationAngle_ = grade::integrateRotation(postRotationAngle_, p.rotation, dt);
    lifeClock_ = std::fmod(lifeClock_ + p.speed * dt, static_cast<double>(kTimeWrapSeconds));
    return p;
}

// Once a frame, after the params resolve: the shape graph follows the drawn params (Spawn, Morph, Formula,
// Fractal, Shapes, Speed), so the reaction matrix, the waves, the envelopes, the macro knobs and the Auto
// Director move it and nothing else does. Its glides and crossfades run on the wall step, as the resolver's
// slider glides do; its drift on the music's step times Speed, the uTime rule, so a pause or silence holds it.
// The seed is the frame's latched play seed (what settingsLatch_.directorSeed() answers, an export's pinned
// one), so a new play gives the graph new shapes and an export replays the screen's.
void Renderer::stepShapeGraph(const SceneParams& p, float dt, float wallDt) {
    shapes::Inputs in;
    in.seed = frameSettings_.director.seed;
    in.spawn = p.spawn;
    in.morph = p.morph;
    in.formula = p.formula;
    in.fractal = p.fractal;
    in.shapes = p.shapes;
    in.speed = p.speed;
    shapeGraph_.tick(in, wallDt, dt);
}

void Renderer::uploadPaletteRamp(const SceneParams& p) {
    const bool changed = paletteRamp_.update(frameSettings_.palettes, palette::amountsOf(p));
    // A still frame is drawn again with the new colours.
    if (changed) redrawFrames_ = std::max(redrawFrames_, 1);
    if (!paletteRamp_.active() || (!changed && paletteRampTex_)) return;
    // The storage and the sampler are made once; a change only rewrites the texels.
    const bool created = !paletteRampTex_;
    if (created) paletteRampTex_.reset(gl_gen::texture());
    glBindTexture(GL_TEXTURE_2D, paletteRampTex_.get());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (created) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, palette::kRampSize, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, paletteRamp_.bytes().data());
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, palette::kRampSize, 1, GL_RGB, GL_UNSIGNED_BYTE, paletteRamp_.bytes().data());
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::resolveLayerScene() {
    // Who asked the stager for what: the layer resolves first in a frame, the next style later (in
    // drawLiquid), so the layer reads the next style's ask from the frame before.
    bornAskedBefore_ = std::move(bornAsked_);
    bornAsked_.clear();
    layerAsked_.clear();
    layer_state::Layer setting;
    std::string& requestedId = requestedIdScratch_;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        setting = layerSetting_;
        requestedId = requestedSceneId_;
    }
    // The setting's source made this renderer's style (viz/LayerPick): the user's pick, Auto Director's at its
    // style moments or a knob's turn, never the style on screen while there is another to draw.
    const signal::Resolver::Frame& heard = resolver_.frame();
    std::string layerId = layerPicker_.pick(setting, requestedId, heard.styleMoments, frameSettings_.director.seed,
                                            layerKnobTurn(frameSettings_, setting.knob, heard.knobMods));
    if (!registry_.knows(layerId)) layerId.clear();
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        layerSceneId_ = layerId;
    }
    if (!offscreen_.load(std::memory_order_relaxed) && layerId != layerShown_) {
        layerShown_ = layerId;
        layer_state::publishShown(layerId);
    }
    if (layerId.empty()) {
        layerScene_ = nullptr;
        fboL_.release();
        return;
    }
    // A built layer draws at once, whatever else is staging. One not built yet is staged only once the
    // requested scene is built and no transition runs (one target at a time); until then the layer it drew
    // last keeps drawing, so a style change or a new pick never blanks it.
    Scene* layer = builtScene(layerId) ? readyScene(layerId) : nullptr;
    if (!layer && !outgoingScene_ && !requestedPending_) {
        layerAsked_ = layerId;
        layer = readySecondary(layerId, bornAskedBefore_);
    }
    if (layer) layerScene_ = layer;
    // A scene draws once a frame: never the one on screen or the one leaving it.
    if (layerScene_ == activeScene_ || layerScene_ == outgoingScene_) layerScene_ = nullptr;
    if (!layerScene_) return;
    // Its own target: a fresh one (new, or at a new size) holds nothing yet, so the layer draws into it.
    const bool kept = fboL_.ok() && fboL_.width() == renderWidth_ && fboL_.height() == renderHeight_;
    if (!fboL_.ensure(renderWidth_, renderHeight_)) {
        layerScene_ = nullptr;
    } else if (!kept) {
        heldLayer_ = nullptr;
    }
}

bool Renderer::ensureTargets() {
    if (!fboA_.ensure(renderWidth_, renderHeight_)) return false;
    // The outgoing style's target lives only while a transition runs, as the layer's (fboL_) lives only
    // while a layer draws: made when one starts, released on the frame after it ends.
    if (!outgoingScene_) {
        fboB_.release();
    } else if (!fboB_.ensure(renderWidth_, renderHeight_)) {
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
    // At mix 0 the composite shows the base alone (its mix() weighs the layer by exactly 0), so a shader layer
    // there steps but does not draw. A shader style is a function of its clock, so its next frame at a mix above
    // 0 is whole; a simulation draws on, or it would freeze while hidden.
    const bool unseen = layerScene_ && layerScene_->family() == SceneFamily::Shader &&
                        safety::layerMix(frameLayerMix_ + p.layerMix, blendModeFromOrdinal(frameLayerBlend_)) <= 0.0f;
    if (layerScene_ && holds(*layerScene_, heldLayer_)) {
        // fboL_ keeps the layer's last frame.
    } else if (unseen) {
        layerScene_->setParams(p);
        deliverPcm(*layerScene_);
        layerScene_->update(trimmedSound(p), dt);
    } else if (layerScene_) {
        heldLayer_ = layerScene_;
        glBindFramebuffer(GL_FRAMEBUFFER, fboL_.fbo());
        glViewport(0, 0, renderWidth_, renderHeight_);
        glClear(GL_COLOR_BUFFER_BIT);
        layerScene_->setParams(p);
        deliverPcm(*layerScene_);
        layerScene_->update(trimmedSound(p), dt);
        const SceneTarget target(fboL_.fbo(), renderWidth_, renderHeight_);
        layerScene_->draw(static_cast<float>(timeSeconds_));
    }
    if (outgoingScene_) {
        progress = std::clamp(static_cast<float>((frameNowS_ - transitionStartS_) * 1000.0 / switchTransitionMs_), 0.0f, 1.0f);
        if (progress >= 1.0f) {
            outgoingScene_ = nullptr;
            outgoingParams_.reset();
        } else {
            bindSecondaryTarget();
            const SceneParams& op = outgoingParams_ ? *outgoingParams_ : p;
            outgoingScene_->setParams(op);
            deliverPcm(*outgoingScene_);
            outgoingScene_->update(trimmedSound(op), dt);
            const SceneTarget target(fboB_.fbo(), renderWidth_, renderHeight_);
            outgoingScene_->draw(static_cast<float>(timeSeconds_));
        }
    }
    return progress;
}

// A simulation or particle style steps once per draw whatever the step, so a stopped clock keeps
// the frame it last drew. A shader style is a function of the clock and redraws the same picture.
bool Renderer::holds(const Scene& scene, const Scene* held) const {
    return frameStill_ && held == &scene && scene.family() != SceneFamily::Shader;
}

void Renderer::drawSceneTarget(Scene& scene, const SceneParams& p, float dt) {
    if (!sceneJustSwitched_ && holds(scene, heldScene_)) {
        // The field holds, but a scene that draws the rig owes a moved eye a new picture of it.
        if (!scene.drawsCamera() || camera_.pose() == heldPose_) return;
        heldPose_ = camera_.pose();
        glBindFramebuffer(GL_FRAMEBUFFER, fboA_.fbo());
        glViewport(0, 0, renderWidth_, renderHeight_);
        glClear(GL_COLOR_BUFFER_BIT);
        scene.redrawHeld();
        return;
    }
    heldScene_ = &scene;
    heldPose_ = camera_.pose();
    glBindFramebuffer(GL_FRAMEBUFFER, fboA_.fbo());
    glViewport(0, 0, renderWidth_, renderHeight_);
    const float keep = scene.trailRetention(p);
    if (keep > 0.0f && !sceneJustSwitched_) {
        trailPass_.apply(p, keep, static_cast<float>(timeSeconds_), dt, fboA_, quadVao_.get(), renderWidth_, renderHeight_);
    } else {
        glClear(GL_COLOR_BUFFER_BIT);
    }
    scene.setParams(p);
    deliverPcm(scene);
    scene.update(trimmedSound(p), dt);
    // After the trail pass: its echo trades fboA_'s framebuffer for its own.
    const SceneTarget target(fboA_.fbo(), renderWidth_, renderHeight_);
    scene.draw(static_cast<float>(timeSeconds_));
}

void Renderer::composite(const SceneParams& p, float progress, GLuint targetFbo, GLuint liquidTex) {
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, width_, height_);
    glDisable(GL_BLEND);
    if (targetFbo == 0) {
        // The composite's full-screen triangle writes every pixel of the surface, so a tiler need not reload
        // last frame's colour (or any depth) into tile memory first.
        static constexpr GLenum kSurfaceAttachments[] = {GL_COLOR, GL_DEPTH};
        glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, kSurfaceAttachments);
    }
    CompositePass::Inputs& in = compositeInputs_;
    in.texA = fboA_.tex();
    in.texB = physicsOutgoingTex_ != 0 ? physicsOutgoingTex_ : fboB_.tex();
    if (liquidTex != 0) in.texA = liquidTex;
    // The smear bends the finished picture by the flow field; Liquid mode rides that field itself.
    fluid::FlowField* ff = overlays_.flow();
    const bool smear = smearWarp_ > 0.0f && ff != nullptr && ff->available();
    in.flowTex = smear ? ff->velocityTex() : compositePass_.zeroTex();
    in.flowStrength = smear ? smearWarp_ : 0.0f;
    in.progress = progress;
    // The Layers page's mix (base) plus what waves, envelopes, routes and knobs add (the layerMix param).
    in.layerMix = safety::layerMix(frameLayerMix_ + p.layerMix, blendModeFromOrdinal(frameLayerBlend_));
    in.blendOrdinal = frameLayerBlend_;
    // At mix 0 the composite shows the base alone, so the layer's post chain is not worth running per pixel.
    in.hasLayer = layerScene_ != nullptr && in.layerMix > 0.0f;
    in.texL = layerScene_ ? fboL_.tex() : compositePass_.zeroTex();
    in.hasOutgoing = outgoingScene_ != nullptr;
    const std::string& transitionId = outgoingScene_ ? switchTransitionId_ : frameTransitionId_;
    in.transitionId = transitionId;
    in.transitionStyle = safety::transitionStyle(TransitionCatalog::builtIn(transitionId).value_or(TransitionStyle::Fade));
    in.ratio = static_cast<float>(renderWidth_) / static_cast<float>(renderHeight_);
    in.timeSeconds = static_cast<float>(timeSeconds_);
    const SceneParams& fx = lastFinalParams_;
    in.postRotationAngle = postRotationAngle_;
    in.twistSeed = styleVariantSeed(activeScene_->id());
    in.postBirths = births::compute(static_cast<float>(lifeClock_), fx.life);
    in.quadVao = quadVao_.get();
    in.fx = fx;
    in.gateA = grade::gateFor(activeScene_->family(), activeScene_->drawsCamera()).toVec4();
    Scene* other = outgoingScene_ ? outgoingScene_ : activeScene_;
    in.gateB = grade::gateFor(other->family(), other->drawsCamera()).toVec4();
    Scene* layer = layerScene_ ? layerScene_ : activeScene_;
    in.gateL = grade::gateFor(layer->family(), layer->drawsCamera()).toVec4();
    in.camGateL = layer->drawsCamera() ? 0.0f : 1.0f;
    // The rig's flat projection, for every scene that does not march the 3D pose itself.
    const CameraPose& cam = camera_.pose();
    in.camFlat = cam.flat;
    in.camTilt = cam.tilt;
    in.camGate = {activeScene_->drawsCamera() ? 0.0f : 1.0f, other->drawsCamera() ? 0.0f : 1.0f};
    // W00: read this frame's latched underlay straight from CompositePass (it owns the texture -
    // see CompositePass.hpp), the same way uFlow above is read from Overlays.
    in.underlayTex = compositePass_.underlayTexOrZero();
    in.underlayBlend = compositePass_.underlayBlendMode();
    in.underlayAmount = compositePass_.underlayAmount();
    compositePass_.draw(in);
}

void Renderer::stepOverlays(Scene& scene, const SceneParams& p, float dt) {
    // Stepped once per frame ahead of every draw so each scene reads the same anchor.
    touchField_.step(dt);
    stepLiquid(scene, p, dt);
    stepSmear(dt);
}

void Renderer::stepSmear(float dt) {
    smearWarp_ = 0.0f;
    const touch::SmearSettings s{smearOn_.load(std::memory_order_relaxed), smearStrength_.load(std::memory_order_relaxed)};
    // Never in an export, never beside Liquid mode, and, as flow distortion, one of the optional passes: off
    // without Performance's extra effects. The field is built only once smear can run.
    const bool wanted = s.on && !offscreen_.load(std::memory_order_relaxed) && liveLiquid_ == liquid::Mode::Off &&
                        governor_.governed().postFx;
    fluid::FlowField* ff = wanted ? overlays_.useFlow() : nullptr;
    const bool can = ff != nullptr && ff->available();
    if (!can) {
        smearTrail_.reset();
        smearLinger_.reset();
        return;
    }
    smearTrail_.step(touchField_.points(), dt, s, [&](const touch::SmearKick& k) {
        ff->queueKick(k.x, k.y, k.vx, k.vy, touch::kSmearRadius);
        smearLinger_.kicked();
    });
    if (!smearLinger_.live()) return;
    if (dt > 0.0f) ff->stepTouch(dt, touch::kSmearDissipation);
    smearLinger_.step(dt);
    smearWarp_ = touch::smearWarp(s, smearLinger_.fade(), frameSettings_.reducedMotion);
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

void Renderer::render(double timeSeconds, GLuint targetFbo) {
    repeated_ = false;
    const float wallDt = beginFrame(timeSeconds);
    const float dt = wallDt * musicRate_;
    if (governor_.governed().renderScale != appliedUserScale_) applyRenderScale();
    // Memory pressure (#854): shed the spare scenes on this GL thread; what is drawn is pinned. This reaches
    // a surface that is still drawing, so its passes keep their history (#999); one that pauses sheds more
    // at the pause (trimSpares).
    if (SceneResidency::trimSignal().poll(seenTrim_)) trimScenes({}, SceneResidency::shedFor(MemoryTrim::UiHidden, true));
    Scene* scene = resolveActiveScene();
    // Staging, a fresh switch, a transition's two scenes or a secondary's compiles: not the phone's pace.
    // A target that gave up staging (ScenePrep::Block) and still will not build is not settling.
    const bool staging = requestedPending_ && prep_.active() && prep_.attempts() < ScenePrep::kMaxAttempts;
    if (staging || sceneJustSwitched_ || outgoingScene_ || !stager_.idle()) noteSettling();
    if (!offscreen_.load(std::memory_order_relaxed)) logResidency(wallDt);
    if (!scene || !ensureTargets()) {
        stillDrawn_ = false;
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
    if (redrawSeconds_ > 0.0f) {
        redrawSeconds_ -= wallDt;
        if (redrawSeconds_ < kStepSumSlack) redrawSeconds_ = 0.0f;
    }
    frameStill_ = !(dt > 0.0f) && sameLook && redrawFrames_ == 0 && redrawSeconds_ == 0.0f && !outgoingScene_;
    heldWire_ = wire;
    heldWidth_ = renderWidth_;
    heldHeight_ = renderHeight_;
    stepOverlays(*scene, p, dt);
    stepCamera(p, dt);
    const float progress = drawSecondaryTargets(p, dt);
    // A shader style paints its whole frame, so its Trails cannot live in the scene target: they are
    // the feedback pass's sustain. The particle styles keep their own trail (trailRetention).
    const bool trails = liveLiquid_ == liquid::Mode::Off && p.trails && scene->family() == SceneFamily::Shader &&
                        scene->trailRetention(p) <= 0.0f && liquidPass_.ensure(renderWidth_, renderHeight_);
    if (!trails) trailsWereOn_ = false;
    if (trails || liveLiquid_ != liquid::Mode::Off) {
        feedbackIdleSeconds_ = 0.0f;
    } else if (feedbackIdleSeconds_ < kFeedbackIdleSeconds - kStepSumSlack) {
        feedbackIdleSeconds_ += wallDt;
        if (feedbackIdleSeconds_ >= kFeedbackIdleSeconds - kStepSumSlack) {
            liquidPass_.releaseTargets();
            bornFbo_.release();
        }
    }
    if (stillRepeats(*scene, p, wallDt, trails, targetFbo)) {
        // The window already shows this frame. What the draw would have freed on its idle clocks is free already.
        repeated_ = true;
        trailPass_.releaseIdle(wallDt, kFeedbackIdleSeconds - kStepSumSlack);
        uiBlurPass_.releaseIdle(wallDt, kUiBlurIdleSeconds - kStepSumSlack);
        return;
    }
    drawSceneTarget(*scene, p, dt);
    // The scene target's trail echo (TrailPass) frees its own target on the same idle window.
    trailPass_.releaseIdle(wallDt, kFeedbackIdleSeconds - kStepSumSlack);
    GLuint shown = trails ? drawTrails(p, dt, wallDt) : drawLiquid(*scene, p, dt);
    shown = drawDive(*scene, p, dt, wallDt, shown);
    shown = drawPhysics(p, dt, wallDt, shown);
    uiBlurPass_.releaseIdle(wallDt, kUiBlurIdleSeconds - kStepSumSlack);
    // With a blur running the frame is drawn into the blur's own image, which draw() then copies to the
    // window: the window is written once and never read back.
    UiBlurRegion blurRegion;
    GLuint frameFbo = targetFbo;
    if (uiBlurWanted(blurRegion)) {
        if (const GLuint f = uiBlurPass_.beginFrame(blurRegion, width_, height_)) frameFbo = f;
    }
    composite(p, progress, frameFbo, shown);
    drawEdgeGlow(frameFbo, timeSeconds, dt);
    if (compositeHook_) compositeHook_(frameFbo, width_, height_);
    if (frameFbo != targetFbo || blurRegion.active()) uiBlurPass_.draw(blurRegion, targetFbo, width_, height_, quadVao_.get());
    updatePcmWanted();
    noteStillDrawn();
}

// How long every input must have stood the same before a frame is left undrawn: past the idle windows that free
// the feedback, trail, blur and physics targets (they run on the draw), so a skipped frame holds none of them.
constexpr float kRepeatAfterSeconds = 3.0f;
static_assert(kRepeatAfterSeconds > kFeedbackIdleSeconds && kRepeatAfterSeconds > kUiBlurIdleSeconds &&
                  kRepeatAfterSeconds > physics::kIdleSeconds,
              "a repeated frame runs after every idle window has freed its target");

// True when drawing this frame would repeat the last drawn one exactly, so the buffer on screen is already it.
// Every input the picture reads: the clock stopped (frameStill_: dt 0, same params, no redraw owed), the sound
// frame the shader uniforms and the sound texture come from (and no new PCM block), the touch points, the camera, the size; and no pass
// that runs on the wall clock or feeds back on itself (trails, Liquid mode, dive, physics layers, smear, a layer or
// outgoing scene, the blur and the edge glow, the recording, cast and still taps, an offscreen target). Only a
// shader style, a function of its clock; the others keep their own hold.
bool Renderer::stillRepeats(const Scene& scene, const SceneParams& p, float wallDt, bool trails, GLuint targetFbo) {
    const auto lookYaw = lookYaw_.load(std::memory_order_relaxed);
    const auto lookPitch = lookPitch_.load(std::memory_order_relaxed);
    const bool inputsSame = stillDrawn_ && std::memcmp(&stillSound_, &soundFrame_, sizeof soundFrame_) == 0 &&
                            stillTouch_ == touchField_.points() && stillPose_ == camera_.pose() &&
                            stillYaw_ == lookYaw && stillPitch_ == lookPitch;
    const bool quiet = inputsSame && frameStill_ && framePcmCount_ == 0 && heldScene_ == &scene && !sceneJustSwitched_ &&
                       scene.family() == SceneFamily::Shader && !requestedPending_ && stager_.idle() &&
                       layerScene_ == nullptr && outgoingScene_ == nullptr && !trails && !liquidOn() &&
                       diveMix_ == 0.0f && !p.endlessZoom && CameraRig::flatDive(p) == 0.0f &&
                       !physics::planFor(p).any() && smearWarp_ == 0.0f && !smearLinger_.live() &&
                       !uiBlurOn_.load(std::memory_order_relaxed) && !edgeGlowOn_.load(std::memory_order_relaxed) &&
                       !offscreen_.load(std::memory_order_relaxed) && targetFbo == 0;
    stillSeconds_ = quiet ? stillSeconds_ + wallDt : 0.0f;
    return repeatAllowed_ && quiet && stillSeconds_ >= kRepeatAfterSeconds;
}

// After a frame that drew: what it saw, for the next frame to compare with.
void Renderer::noteStillDrawn() {
    stillDrawn_ = true;
    stillSound_ = soundFrame_;
    stillTouch_ = touchField_.points();
    stillPose_ = camera_.pose();
    stillYaw_ = lookYaw_.load(std::memory_order_relaxed);
    stillPitch_ = lookPitch_.load(std::memory_order_relaxed);
}

// The PCM tail (PcmHandoff) is mixed down only while a built scene reads it, or a build is under way that soon will:
// a scene built in the frame it is drawn finds the block it would always have found.
void Renderer::updatePcmWanted() {
    bool wanted = requestedPending_ || prep_.active() || !stager_.idle();
    for (const auto& entry : scenes_) {
        if (entry.second && entry.second->readsPcm()) {
            wanted = true;
            break;
        }
    }
    if (pcmWanted_.load(std::memory_order_relaxed) == wanted) return;
    // Reopened: a held remainder from before the gap is not part of the next block.
    if (wanted) {
        std::lock_guard<std::mutex> lock(stateLock_);
        pcm_.clear();
    }
    pcmWanted_.store(wanted, std::memory_order_relaxed);
}

bool Renderer::uiBlurWanted(UiBlurRegion& region) {
    if (offscreen_.load(std::memory_order_relaxed) || !uiBlurOn_.load(std::memory_order_relaxed)) return false;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        region = uiBlurRegion_;
    }
    return region.active();
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
        liquidWasOn_ = false;
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
    // The field is built on the first frame Liquid mode pours (Overlays::useFlow).
    fluid::FlowField* ff = plan.velocityGrid > 0 ? overlays_.useFlow() : nullptr;
    if (ff == nullptr || !ff->available()) return;
    ff->setGrid(plan.velocityGrid, plan.macCormack);
    const float force = std::clamp(p.flowForce, 0.0f, 3.0f);
    if (g.push) ff->queueKick(g.x, g.y, g.vx * kLiquidPush * force, g.vy * kLiquidPush * force, kPushRadius);
    if (std::fabs(g.swirl) > 0.02f) ff->queueVortex(g.x, g.y, g.swirl * kLiquidSwirl * force, kSwirlRadius);
    // The mids drive the swirl: Swirl is what a full mid band reaches.
    SceneParams fp = p;
    fp.flowCurl = std::clamp(p.flowCurl, 0.0f, 50.0f) * (0.4f + 0.6f * std::clamp(soundFrame_.middle, 0.0f, 1.0f));
    overlays_.stepFlow(trimmedSound(p), dt, fp, !offscreen_.load(std::memory_order_relaxed));
}

GLuint Renderer::drawBorn(Scene& scene, const SceneParams& p, float dt) {
    const liquid::Born born = liquid::bornFrom(p.liquidBorn);
    // A re-roll and the next style each draw a whole second scene, so only the full liquid affords them.
    if (liveLiquid_ != liquid::Mode::Full || born == liquid::Born::Live || born == liquid::Born::Forms) return fboA_.tex();
    if (!bornFbo_.ensure(renderWidth_, renderHeight_)) return fboA_.tex();
    if (born == liquid::Born::Rerolled) {
        // A stateful family (the fluid) cannot draw twice in one frame without stepping twice.
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
        {
            const SceneTarget target(bornFbo_.fbo(), renderWidth_, renderHeight_);
            scene.draw(static_cast<float>(timeSeconds_));
        }
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
    next->update(trimmedSound(p), dt);
    const SceneTarget target(bornFbo_.fbo(), renderWidth_, renderHeight_);
    next->draw(static_cast<float>(timeSeconds_));
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
    f.time = static_cast<float>(timeSeconds_);
    f.quadVao = quadVao_.get();
    return liquidPass_.run(f);
}

GLuint Renderer::drawDive(const Scene& scene, const SceneParams& p, float dt, float wallDt, GLuint shown) {
    const float wall = std::isfinite(wallDt) ? std::clamp(wallDt, 0.0f, 0.1f) : 0.0f;
    diveFollow_.step(soundFrame_, wall, p.audioDrive, p.diveFollow);
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
    if (on) {
        const float octaves = endless::travel((zoom ? p.endlessZoomSpeed : 0.0f) + fly, diveFollow_);
        divePhase_ = endless::advance(divePhase_, endless::copiesPerSecond(octaves, p.diveSpacing), dt);
    }
    const auto look = endless::tunnel::lookFor(p, static_cast<float>(renderWidth_) / static_cast<float>(std::max(renderHeight_, 1)));
    const bool plain = shown == 0;
    const GLuint dived =
        divePass_.run(plain ? fboA_.tex() : shown, plain ? renderWidth_ : liquidPass_.width(),
                      plain ? renderHeight_ : liquidPass_.height(), renderWidth_, renderHeight_, divePhase_,
                      diveMix_ * diveMix_ * (3.0f - 2.0f * diveMix_), look, quadVao_.get());
    return dived != 0 ? dived : shown;
}

GLuint Renderer::drawPhysics(const SceneParams& p, float dt, float wallDt, GLuint shown) {
    physicsOutgoingTex_ = 0;
    physics::PhysicsLayers::Frame f;
    f.styleTex = shown != 0 ? shown : fboA_.tex();
    f.width = renderWidth_;
    f.height = renderHeight_;
    // The music's clock, as every simulation steps: silence holds the layers, an export steps exactly 1/fps.
    f.dt = dt;
    f.wallDt = wallDt;
    f.live = !offscreen_.load(std::memory_order_relaxed);
    f.time = static_cast<float>(timeSeconds_);
    f.zeroTex = compositePass_.zeroTex();
    f.quadVao = quadVao_.get();
    f.blocking = blockingBuilds_.load(std::memory_order_relaxed);
    const GLuint layered = physicsLayers_.run(physics::planFor(p), f);
    if (physicsLayers_.building()) noteSettling();
    if (layered == f.styleTex) return shown;
    if (outgoingScene_) physicsOutgoingTex_ = physicsLayers_.over(fboB_.tex());
    return layered;
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
    f.time = static_cast<float>(timeSeconds_);
    f.quadVao = quadVao_.get();
    return liquidPass_.run(f);
}

}  // namespace geode::viz
