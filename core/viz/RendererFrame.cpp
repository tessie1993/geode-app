#include "viz/Renderer.hpp"

#include <algorithm>
#include <cmath>
#include <random>

#include "sound/Form.hpp"
#include "viz/Catalogue.hpp"
#include "viz/CompositeGrade.hpp"
#include "viz/EndlessZoom.hpp"
#include "viz/ParamRandomizer.hpp"
#include "viz/Quad.hpp"

namespace geode::viz {

namespace {
constexpr float kTouchMinOverlayStrength = 0.35f;
// Liquid mode's hand and sound on the flow (clip units per second into FlowField's kicks), and the
// radius of each kind of kick: a drag moves the liquid about as fast as the finger.
constexpr float kLiquidPush = 1.0f;
constexpr float kLiquidSwirl = 2.4f;
constexpr float kLiquidBurst = 3.0f;
constexpr float kPushRadius = 0.08f;
constexpr float kSwirlRadius = 0.07f;
constexpr float kBurstRadius = 0.12f;
// How hard the liquid sharpens its own back-trace, and how much extra the style's edges are re-poured.
constexpr float kLiquidSharpen = 0.35f;
constexpr float kLiquidEdge = 0.6f;

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
        // Customize › Macros, latched once per frame; Energy and Speed reach the engine as tuning.
        frameKnobs_ = macros::global();
        if (sound_ && frameKnobs_ != tunedKnobs_) {
            tunedKnobs_ = frameKnobs_;
            sound_->setTuning(macros::tuning(soundTuning_, tunedKnobs_));
        }
        if (sound_) sound_->step(dt, soundFrame_);
    }
    applyStyleSchedule();
    std::vector<std::pair<std::string, std::string>> pending;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        pending.swap(pendingShaders_);
        frameLayerMix_ = layerMix_;
        frameLayerBlend_ = layerBlend_;
        frameTransitionId_ = transitionId_;
        frameTransitionDurationMs_ = transitionDurationMs_;
        // Each pushed block reaches this frame's scenes once; no new push, no PCM (PcmHandoff).
        framePcmCount_ = pcm_.take(pcmDeliverScratch_.data());
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
        motionField_.setVariant(styleVariantSeed(requestedId));
    }
    return activeScene_;
}

uint64_t Renderer::noteSeed(uint64_t playSeed) {
    if (!noteSeeded_ || playSeed != notePlaySeed_) {
        std::random_device device;
        noteEntropy_ = (static_cast<uint64_t>(device()) << 32) ^ device();
        notePlaySeed_ = playSeed;
        noteSeeded_ = true;
    }
    return playSeed ^ noteEntropy_;
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
    // The auto-director hears the frame first: its look lies under the macros, so the user's own knobs,
    // the LFOs, the reactions, the motion field and the safety clamp all still act on top of it.
    const DirectorConfig directorConfig = directorPinned_.load(std::memory_order_acquire)
                                              ? DirectorConfig{pinnedDirectorAmount_.load(std::memory_order_relaxed),
                                                               pinnedDirectorSeed_.load(std::memory_order_relaxed)}
                                              : director::global();
    director_.configure(directorConfig);
    director_.step(soundFrame_, dt);
    if (director_.commits() != directorCommitsSeen_) {
        directorCommitsSeen_ = director_.commits();
        const Director::Reason why = director_.lastReason();
        if (why == Director::Reason::Section || why == Director::Reason::Drop) {
            directorStyleMoments_.fetch_add(1, std::memory_order_acq_rel);
        }
    }
    directedKnobs_ = director_.knobs(frameKnobs_);
    // Note & key reactions hear the frame next, inside the director's look: its hue anchors the notes'
    // colours, and its feeling (the director's emotion, stepped whether or not it drives) is the mood. Its
    // tuning and bloom look are the style's own (Customize), read before the director and macros touch them.
    notes_.configure({note_response::globalEnabled(), noteSeed(directorConfig.seed)});
    {
        const SceneParams& own = displayedParams_;
        NoteResponse::Inputs in;
        in.anchorHue = director_.look().hue;
        in.anchorWeight = director_.weight();
        in.color = own.noteColor;
        in.pitch = own.notePitch;
        in.intervals = own.noteIntervals;
        in.tension = own.noteTension;
        in.chords = own.noteChords;
        in.drums = own.noteDrums;
        in.bloom = own.noteBloom;
        in.look = static_cast<int>(catalogue::resolveBloomLook(own.bloomLook, activeScene_ ? activeScene_->id() : std::string()));
        in.camFlat = camera_.pose().flat;
        notes_.step(soundFrame_, director_.emotion(), in, dt);
    }
    const auto& envValues = adsr_.tick(dt, soundFrame_);
    AdsrEngine::lfoOffsets(adsr_.configs, envValues, envRate_, envDepth_);
    const auto& lfoValues = lfo_.tick(dt, soundFrame_, envRate_.data(), envDepth_.data());
    // The macros lay over the user's params first, so every stage after them (and the safety clamp)
    // sees the knobbed look.
    const bool palWheel = !activeScene_ || activeScene_->family() == SceneFamily::Shader;
    // Order: the director's look, the note layer inside it, then the user's macros and LFOs over both.
    SceneParams p = lfo_.apply(macros::apply(notes_.apply(director_.apply(displayedParams_, palWheel)), directedKnobs_), lfoValues);
    p = AdsrEngine::apply(p, adsr_.configs, envValues);
    // The reaction matrix: per-style routes from the sound frame to many
    // targets. Ahead of the motion field and the safety clamp, like the LFOs.
    reactions_.tick(soundFrame_, dt);
    p = reactions_.apply(p);
    // The macros' own routes, in an engine of their own so they never take a user slot.
    macroReactions_.routes = macros::routes(directedKnobs_);
    macroReactions_.tick(soundFrame_, dt);
    p = macroReactions_.apply(p);
    // The director's routes, crossfading at each decision, in engines of their own.
    p = director_.applyRoutes(p);
    // The continuous motion system: the one stage every family's parameters
    // pass through, fed the sound frame (never a single drum hit or PCM
    // impulse), ahead of the safety clamp so nothing it adds can exceed the
    // flash and motion limits.
    motionField_.step(soundFrame_, dt);
    const bool reducedMotion = reducedMotion_.load(std::memory_order_relaxed);
    p = motionField_.apply(p, reducedMotion, sceneSpin_.load(std::memory_order_relaxed));
    p = safety::apply(p, reducedMotion);
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
    Scene* layer = readyScene(layerId);
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
    if (framePcmCount_ > 0) scene.acceptPcm(pcmDeliverScratch_.data(), framePcmCount_);
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
    in.noteTint = notes_.uniforms().tint;
    compositePass_.draw(in);
    // The note blooms over every family, last; nothing while there are none.
    bloomPass_.draw(notes_.uniforms(), width_, height_, quadVao_);
}

void Renderer::stepOverlays(Scene& scene, const SceneParams& p, float dt) {
    smearing_ = overlays_.smearing(monotonicSeconds());
    // Stepped once per frame ahead of every draw so each scene reads the same anchor.
    touchField_.step(dt);
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
    camera_.setMoves(director_.moves(macros::camera(directedKnobs_)));
    // Endless zoom flies the rig only for a style that marches it (EndlessZoom.hpp); the
    // families still on the old law keep their old camera too.
    SceneParams rigParams = p;
    rigParams.endlessZoom = p.endlessZoom && activeScene_ != nullptr && activeScene_->drawsCamera() &&
                            !endless::keepsTriangle(activeScene_->id());
    // Liquid mode owns the dive: a 3D style dives by flying the rig through it, at Dive speed.
    if (liquidOn() && activeScene_ != nullptr && activeScene_->drawsCamera()) rigParams.endlessZoom = true;
    camera_.step(rigParams, look, dt, reducedMotion_.load(std::memory_order_relaxed),
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
    // Liquid mode owns the dive, so a 2D style's own endless-zoom layers step aside (nothing dives twice).
    SceneParams sceneParams = p;
    if (liquidOn() && !scene->drawsCamera()) sceneParams.endlessZoom = false;
    drawSceneTarget(*scene, sceneParams, dt);
    const GLuint liquidTex = drawLiquid(*scene, p, dt);
    composite(p, progress, targetFbo, liquidTex);
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
        liquidFlare_ = 0.0f;
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
    const bool reducedMotion = reducedMotion_.load(std::memory_order_relaxed);
    const float rate = threeD ? 0.0f : std::clamp(p.endlessZoomSpeed, 0.0f, 1.2f) * endless::musicRate(soundFrame_, p.audioDrive);
    liquidFrame_ = liquidDive_.step(dt, rate, g.steerX * aspect, g.steerY, g.steerWeight, reducedMotion);
    if (threeD) {
        liquidFrame_.scale = 1.0f;
        liquidFrame_.twist = 0.0f;
    }
    const bool drop = sound::dropped(soundFrame_);
    liquidFlare_ = liquid::dropFlare(liquidFlare_, drop, dt);
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
    if (drop && !reducedMotion) ff->queueBurst(liquidFrame_.vpX / aspect, liquidFrame_.vpY, kLiquidBurst * force, kBurstRadius);
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
    Scene* next = readyScene(bornSceneId_);
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
    f.dive = liquidFrame_;
    const float liquidity = std::clamp(p.flowStrength, 0.0f, 1.0f);
    f.inject = liquid::injectWeight(liquidity, dt);
    f.edge = kLiquidEdge * (1.0f - 0.5f * liquidity);
    f.sharpen = liveLiquid_ == liquid::Mode::Full ? kLiquidSharpen : 0.0f;
    f.bornRadius = liquid::bornRadius(liquidity);
    // A 3D style dives by flying its camera; nothing is born at a vanishing point it does not have.
    f.bornGain = threeD ? 0.0f : bornClock_.gain();
    f.bornMode = born == liquid::Born::Forms ? 3 : 0;
    f.flare = liquidFlare_;
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
