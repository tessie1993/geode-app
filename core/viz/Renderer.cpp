#include "viz/Renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

#include "sound/Tuning.hpp"
#include "util/Log.hpp"
#include "viz/GlProber.hpp"
#include "viz/ShaderOverrides.hpp"

namespace geode::viz {

namespace {
constexpr const char* kTag = "GeodeRenderer";
constexpr int kPaletteSize = 256;
constexpr int kPaletteRows = 5;
constexpr int kPcmCapacity = 512 * 8;
}  // namespace

Renderer::Renderer(AAssetManager* assets, std::string cacheDir)
    : assets_(assets),
      cacheDir_(std::move(cacheDir)),
      deviceGl_(cacheDir_),
      registry_(assets_, &programCache_, &profile_,
                SceneHost{[this](const std::string& m) { fail(m); },
                          [this] { return thermal_.pacedFps(); },
                          [this](const std::string& path) { notePresetLoaded(path); },
                          // The user's march scale under the thermal ladder's (ADR 0008: march is its third rung).
                          [this] { return marchScale_.load(std::memory_order_relaxed) * thermal_.tierInfo().marchScale; },
                          [this] { return &resolver_.noteUniforms(); },
                          [this] { return settingsLatch_.directorSeed(); },
                          [this] { return offscreen_.load(std::memory_order_relaxed); }}),
      compositePass_(assets_, &programCache_),
      pcm_(kPcmCapacity),
      pcmDeliverScratch_(kPcmCapacity, 0.0f),
      pcmSideScratch_(kPcmCapacity, 0.0f) {
    programCache_.install(cacheDir_);
}

Renderer::~Renderer() = default;

void Renderer::setParams(const SceneParams& params) {
    std::lock_guard<std::mutex> lock(stateLock_);
    requestedParams_ = params;
}

void Renderer::pinLayer(bool on) {
    std::lock_guard<std::mutex> lock(stateLock_);
    if (on) {
        layerFollower_.pin();
    } else {
        layerFollower_.unpin();
    }
}

// Under stateLock_, once a frame from beginFrame. The layer is process-wide (viz/LayerState, #364), so
// the live screen, an export, the wallpaper and the dream all draw what Customize › Layers set; the
// follower hands it over only when it changed (or once, pinned, in an export). resolveLayerScene and
// trimScenes read layerSceneId_ as before, and an export's blocking build makes the layer scene whole
// on the first frame that asks for it, like the main scene.
void Renderer::syncLayerLocked() {
    layer_state::Layer layer;
    if (!layerFollower_.poll(layer)) return;
    layerSceneId_ = std::move(layer.sceneId);
    layerMix_ = layer.mix;
    layerBlend_ = layer.blend;
}

void Renderer::setTransition(const std::string& id, int64_t durationMs) {
    std::lock_guard<std::mutex> lock(stateLock_);
    transitionId_ = safety::transitionId(id);
    // A 0ms (or negative) duration turns the progress fraction in
    // drawSecondaryTargets into 0.0/0 == NaN, and NaN never satisfies
    // `progress >= 1.0f`, so the outgoing scene would never be released.
    transitionDurationMs_ = std::max<int64_t>(durationMs, 1);
}

void Renderer::beginParamMorph(float seconds) {
    if (seconds <= 0.0f) return;
    std::lock_guard<std::mutex> lock(stateLock_);
    morphRequested_ = true;
    morphRequestSec_ = seconds;
}

void Renderer::writeAudio(const float* interleaved, int frames, int channels, int sampleRateHz) {
    if (!interleaved || frames <= 0 || channels <= 0 || sampleRateHz <= 0) return;
    {
        std::lock_guard<std::mutex> lock(soundLock_);
        // SoundEngine clamps its rate to 8 kHz; compare the clamped rate or a lower one rebuilds every call.
        if (!sound_ || sound_->sampleRateHz() != std::max(8000, sampleRateHz)) {
            sound_ = std::make_unique<sound::SoundEngine>(sampleRateHz);
            sound_->setTuning(macros::tuning(soundTuning_, tunedKnobs_));
        }
        sound_->write(interleaved, frames, channels);
    }
    // MilkDrop's raw PCM is what the engine just heard (#360): one ring, one cursor, every surface.
    // Taken under its own lock, never nested in soundLock_, so the two locks have no order to keep.
    std::lock_guard<std::mutex> lock(stateLock_);
    pcm_.write(interleaved, frames, channels, sampleRateHz);
}

void Renderer::resetAudio() {
    {
        std::lock_guard<std::mutex> lock(soundLock_);
        if (sound_) sound_->reset();
    }
    std::lock_guard<std::mutex> lock(stateLock_);
    pcm_.clear();
}

void Renderer::setSoundTuning(const GeodeSoundTuning& tuning) {
    std::lock_guard<std::mutex> lock(soundLock_);
    // Clamped before the macros scale it, so a NaN Reactivity is the default one turned, not a NaN.
    soundTuning_ = sound::tuning::clamp(tuning);
    if (sound_) sound_->setTuning(macros::tuning(soundTuning_, tunedKnobs_));
}

double Renderer::monotonicSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Renderer::queueTouchStroke(float nx, float ny, float ndx, float ndy, float dt, float strength) {
    overlays_.queueTouchStroke(nx, ny, ndx, ndy, dt, strength, monotonicSeconds());
}

void Renderer::setFluidInjectionShaders(const std::string& forceSrc, const std::string& dyeSrc) {
    std::lock_guard<std::mutex> lock(stateLock_);
    fluidForceSrc_ = forceSrc;
    fluidDyeSrc_ = dyeSrc;
    fluidInjectionDirty_ = true;
}

void Renderer::loadMilkPreset(const std::string& path) {
    std::lock_guard<std::mutex> lock(stateLock_);
    lastMilkPreset_ = path;
    milkPresetRequest_ = path;
}

void Renderer::reloadMilkPreset() {
    std::lock_guard<std::mutex> lock(stateLock_);
    milkReloadRequested_ = true;
}

void Renderer::setMilkTextureDir(const std::string& dir) {
    std::lock_guard<std::mutex> lock(stateLock_);
    milkTextureDir_ = dir;
    milkTextureDirDirty_ = true;
}

void Renderer::notePresetLoaded(const std::string& path) {
    std::lock_guard<std::mutex> lock(stateLock_);
    lastMilkPreset_ = path;
    lastGoodMilkPreset_ = path;
    milkPresetLoaded_ = path;
}

std::string Renderer::takeMilkPresetLoaded() {
    std::lock_guard<std::mutex> lock(stateLock_);
    std::string out;
    out.swap(milkPresetLoaded_);
    return out;
}

// GL thread: the preset requests wait here so the scene table is only ever touched on one thread.
void Renderer::applyMilkRequests() {
    std::string preset;
    std::string textureDir;
    bool reload = false;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        preset.swap(milkPresetRequest_);
        reload = milkReloadRequested_;
        milkReloadRequested_ = false;
        if (milkTextureDirDirty_) textureDir = milkTextureDir_;
        milkTextureDirDirty_ = false;
    }
    Scene* milk = builtScene("milkdrop");
    if (!milk) return;
    // A preset picked in silence still loads and shows: about a second of redraws.
    if (!preset.empty() || reload || !textureDir.empty()) redrawFrames_ = 60;
    if (!textureDir.empty()) milk->setMilkTextureDir(textureDir);
    if (!preset.empty()) milk->queueMilkPreset(preset);
    if (reload) milk->reloadMilkPreset();
}

void Renderer::applyPendingFluidInjection() {
    std::string force;
    std::string dye;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        if (!fluidInjectionDirty_) return;
        force = fluidForceSrc_;
        dye = fluidDyeSrc_;
    }
    // Every built scene on the fluid solver, not only the one named "fluid":
    // the fluid styles are profiles over the same engine and the user's
    // injection shaders belong to all of them.
    bool applied = false;
    for (auto& entry : scenes_) {
        if (!entry.second->isFluid()) continue;
        entry.second->setInjectionShaders(force, dye);
        applied = true;
    }
    if (!applied) return;
    {
        // A newer edit may have landed while the scenes were rebuilt; keep it dirty for next frame.
        std::lock_guard<std::mutex> lock(stateLock_);
        if (fluidForceSrc_ == force && fluidDyeSrc_ == dye) fluidInjectionDirty_ = false;
    }
}

// Any thread. Copies the pixel buffer under stateLock_ into the retained underlay buffer; the
// actual GL upload (and the ARGB->RGBA conversion) happens on the GL thread in
// applyUnderlayUpload(), a copy-now/convert-later split. blend/amount are only
// meaningful (and only stored) alongside a non-empty pixel buffer; a null/zero-size buffer clears.
void Renderer::setUnderlayRgba(const uint32_t* pixels, int width, int height, int blend, float amount) {
    std::lock_guard<std::mutex> lock(stateLock_);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        underlayPixels_.clear();
        underlayWidth_ = 0;
        underlayHeight_ = 0;
    } else {
        underlayPixels_.assign(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height));
        underlayWidth_ = width;
        underlayHeight_ = height;
        underlayBlend_ = blend;
        underlayAmount_ = amount;
    }
    underlayDirty_ = true;
}

// GL thread, called once per frame from beginFrame(). Copies (not swaps, unlike
// applyPendingFluidInjection: the source buffers are retained state that must survive a surface
// recreation, not a one-shot request) the latched pixels out from under stateLock_, then does the
// actual conversion and glTexImage2D/glTexSubImage2D upload unlocked.
void Renderer::applyUnderlayUpload() {
    bool underlayDirty = false;
    std::vector<uint32_t> underlayPixels;
    int underlayWidth = 0;
    int underlayHeight = 0;
    int underlayBlend = 0;
    float underlayAmount = 0.0f;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        underlayDirty = underlayDirty_;
        underlayDirty_ = false;
        if (underlayDirty) {
            underlayPixels = underlayPixels_;
            underlayWidth = underlayWidth_;
            underlayHeight = underlayHeight_;
            underlayBlend = underlayBlend_;
            underlayAmount = underlayAmount_;
        }
    }
    if (underlayDirty) {
        if (underlayWidth > 0 && underlayHeight > 0) {
            compositePass_.uploadUnderlay(underlayPixels, underlayWidth, underlayHeight, underlayBlend, underlayAmount);
        } else {
            compositePass_.clearUnderlay();
        }
    }
}

void Renderer::setLfoConfigs(const std::array<LfoConfig, LfoEngine::kSlots>& configs) {
    std::lock_guard<std::mutex> lock(stateLock_);
    lfoConfigs_ = configs;
    lfoConfigsDirty_ = true;
}

void Renderer::setAdsrConfigs(const std::array<AdsrConfig, AdsrEngine::kCount>& configs) {
    std::lock_guard<std::mutex> lock(stateLock_);
    adsrConfigs_ = configs;
    adsrConfigsDirty_ = true;
}

void Renderer::setReactionRoutes(const std::array<ReactRoute, ReactionEngine::kRoutes>& routes) {
    std::lock_guard<std::mutex> lock(stateLock_);
    reactionRoutes_ = routes;
    reactionRoutesDirty_ = true;
}

void Renderer::fail(const std::string& message) {
    if (holdingErrors_) {
        heldErrors_.push_back(message);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        lastError_ = message;
    }
    if (!message.empty()) GEODE_LOGW(kTag, "%s", message.c_str());
}

// Any thread: returns a copy taken under stateLock_ rather than a reference,
// since fail() (GL thread) mutates lastError_ concurrently with callers here.
std::string Renderer::lastError() const {
    std::lock_guard<std::mutex> lock(stateLock_);
    return lastError_;
}

// GL thread, from readyScene (every frame's way to its scenes). The overrides are process-wide
// (viz/ShaderOverrides, #364): when their generation moves, every resident scene whose override
// changed gets the new source (or "" = its built-in one) and recompiles on its next draw through the
// same path a first build takes, so a compile error still reaches lastError(). One atomic load a
// frame otherwise.
void Renderer::syncShaderOverrides() {
    const uint64_t generation = shader_overrides::generation();
    if (generation == shaderGeneration_) return;
    shaderGeneration_ = generation;
    const auto overrides = shader_overrides::snapshot();
    const std::string stock;
    for (auto& [id, scene] : scenes_) {
        const auto found = overrides.find(id);
        const std::string& want = found == overrides.end() ? stock : found->second;
        std::string& have = appliedShaders_[id];
        if (have == want) continue;
        have = want;
        scene->setFragmentSource(want);
    }
}

void Renderer::onSurfaceCreated() {
    profile_ = deviceGl_.profileWithCurrentContext();
    GEODE_LOGI(kTag, "surface created: %s", profile_.summary().c_str());
    thermal_.onSurfaceRecreated();
    // The shell's label belongs to the old surface's collectors: say the mode again on the new one.
    liquidModeTaken_.store(-1, std::memory_order_relaxed);
    programCache_.prime();
    // The old context's staged programs died with it: drop their ids before releaseScenes clears.
    stager_.onContext(profile_.report.extensions.count("GL_KHR_parallel_shader_compile") > 0);
    // Every name the old context made is released here, before this context's first glGen* (#374).
    // GL numbers names per context from 1, so an old name deleted after a new object was made can be
    // that new object: the passes' create() used to release first, after the overlays and the trail
    // pass had already made names on the new context, and deleted them. Deleting a name a fresh
    // context never made is a no-op, so releasing first is safe whether or not the context is new.
    releaseScenes();
    // The frame passes (frame::Pass) drop their names before anything below generates new ones.
    edgeGlowPass_.release();
    trailPass_.release();
    compositePass_.release();
    uiBlurPass_.release();
    liquidPass_.release();
    overlays_.release();
    if (paletteLutTex_ != 0) {
        glDeleteTextures(1, &paletteLutTex_);
        paletteLutTex_ = 0;
    }
    if (quadVao_ != 0) {
        glDeleteVertexArrays(1, &quadVao_);
        quadVao_ = 0;
    }
    touchField_.reset();
    fboA_.release();
    fboB_.release();
    bornFbo_.release();
    liquidWasOn_ = false;
    lastBornSceneId_.clear();
    activeScene_ = nullptr;
    heldScene_ = nullptr;
    heldLayer_ = nullptr;
    outgoingScene_ = nullptr;
    outgoingParams_.reset();
    overlays_.recreate();
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        if (!fluidForceSrc_.empty() || !fluidDyeSrc_.empty()) fluidInjectionDirty_ = true;
        // W00: the compositePass_.release() call above already dropped the underlay
        // GL texture, so re-arm from the retained pixel buffer to reupload it once
        // compositePass_.create() (below) has run on the new context.
        if (!underlayPixels_.empty()) underlayDirty_ = true;
    }

    std::string error;
    const auto quadVert = assets_.load("quad_vert.glsl", &error);
    const auto fadeVert = assets_.load("fade_vert.glsl", &error);
    if (!quadVert || !fadeVert) {
        fail(error);
        return;
    }
    quadVert_ = *quadVert;
    if (!trailPass_.create(assets_, &programCache_, *fadeVert, &error)) fail(error);
    if (!compositePass_.create(*fadeVert, &error)) fail(error);
    // The glass sheet's blur is an extra: without it the sheet shows the visual unblurred through its tint.
    if (!uiBlurPass_.create(assets_, &programCache_, *fadeVert, &error)) GEODE_LOGW(kTag, "ui blur off: %s", error.c_str());
    // And the wallpaper's edge glow: without it the wallpaper shows the full visual instead of the rim.
    if (!edgeGlowPass_.create(assets_, &programCache_, *fadeVert, &error)) GEODE_LOGW(kTag, "edge glow off: %s", error.c_str());
    // Liquid mode: half float where this GPU renders it, so a deep pour-back does not band.
    if (!liquidPass_.create(assets_, &programCache_, *fadeVert, fluid::probeFormats().ok, &error)) fail(error);

    // The cyclic palette LUT: five 256-entry RGB rows.
    const auto palette = assets_.readAsset("shaders/cyclic_palettes.bin");
    if (palette && palette->size() >= static_cast<size_t>(kPaletteSize * kPaletteRows * 3)) {
        glGenTextures(1, &paletteLutTex_);
        glBindTexture(GL_TEXTURE_2D, paletteLutTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, kPaletteSize, kPaletteRows, 0, GL_RGB, GL_UNSIGNED_BYTE, palette->data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }

    glGenVertexArrays(1, &quadVao_);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    lastTimeS_ = -1.0;
    resolver_.reset();
    camera_.reset();
}

void Renderer::surfaceChanged(int width, int height) {
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    glViewport(0, 0, width_, height_);
    applyRenderScale();
}

void Renderer::clearToBlack(GLuint targetFbo) {
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, width_, height_);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::setUserQuality(float renderScale, float marchScale, bool postFx, bool adaptive) {
    // std::clamp passes NaN through, and a NaN scale would never equal appliedUserScale_ again,
    // resizing every frame; a corrupt pref means full quality instead.
    const auto unit = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.25f, 1.0f) : 1.0f; };
    userScale_.store(unit(renderScale), std::memory_order_relaxed);
    marchScale_.store(unit(marchScale), std::memory_order_relaxed);
    userPostFx_.store(postFx, std::memory_order_relaxed);
    thermal_.setAdaptive(adaptive);
}

void Renderer::setUiBlurRegion(const UiBlurRegion& region) {
    // A non-finite value from the shell turns the blur off rather than reaching a scissor.
    const auto unit = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; };
    UiBlurRegion r;
    r.left = unit(region.left);
    r.top = unit(region.top);
    r.right = unit(region.right);
    r.bottom = unit(region.bottom);
    // Any non-finite edge means the rect is unknown: switch the blur off rather than clamp an edge to 0 or 1.
    const bool edgesFinite = std::isfinite(region.left) && std::isfinite(region.top) && std::isfinite(region.right) &&
        std::isfinite(region.bottom);
    r.strength = edgesFinite ? unit(region.strength) : 0.0f;
    r.cornerPx = std::isfinite(region.cornerPx) ? std::max(region.cornerPx, 0.0f) : 0.0f;
    std::lock_guard<std::mutex> lock(stateLock_);
    uiBlurRegion_ = r;
    uiBlurOn_.store(r.active(), std::memory_order_relaxed);
}

void Renderer::setEdgeGlow(const frame::edge_glow::Options& options) {
    std::lock_guard<std::mutex> lock(stateLock_);
    edgeGlowOptions_ = options;
    edgeGlowOn_.store(options.enabled, std::memory_order_relaxed);
}

void Renderer::applyRenderScale() {
    appliedTier_ = thermal_.tier();
    appliedUserScale_ = userScale_.load(std::memory_order_relaxed);
    // A user scale below 1 also drops the small-screen supersample: someone who asked for fewer
    // pixels did not mean 1.4x of fewer pixels.
    const float supersample = appliedUserScale_ < 1.0f ? 1.0f : supersampleFactor(width_, height_);
    const float scale = supersample * appliedUserScale_ * thermalTierInfo(appliedTier_).renderScale;
    renderWidth_ = std::max(static_cast<int>(width_ * scale), 1);
    renderHeight_ = std::max(static_cast<int>(height_ * scale), 1);
    // A hotter tier keeps fewer spares: release them before anything is resized (#378). Then only the
    // scenes drawn this frame are resized; a spare is resized when readyScene hands it out again, so
    // a tier change costs one resize per scene shown, not one per scene ever built.
    ++sizeStamp_;
    trimScenes({});
    for (auto& entry : scenes_) {
        Scene* s = entry.second.get();
        if (s != activeScene_ && s != outgoingScene_ && s != layerScene_) continue;
        s->resize(renderWidth_, renderHeight_);
        s->setWindowSize(width_, height_);
        residency_.markSized(entry.first, sizeStamp_);
    }
    overlays_.resize(renderWidth_, renderHeight_);
    fboA_.ensure(renderWidth_, renderHeight_);
    fboB_.ensure(renderWidth_, renderHeight_);
}

float Renderer::supersampleFactor(int width, int height) {
    const int longest = std::max(width, height);
    if (longest >= 2200) return 1.0f;
    if (longest >= 1600) return 1.25f;
    return 1.4f;
}

bool Renderer::setScene(const std::string& sceneId) {
    if (!registry_.knows(sceneId)) return false;
    std::lock_guard<std::mutex> lock(stateLock_);
    requestedSceneId_ = sceneId;
    sceneSpin_.store(SceneRegistry::spinRate(sceneId), std::memory_order_relaxed);
    return true;
}

void Renderer::setStyleSchedule(std::vector<StyleCue> cues, std::vector<std::string> beatCutIds, int everyBeats,
                                const std::vector<std::string>& lockedIds) {
    StyleSchedule schedule;
    schedule.set(std::move(cues), std::move(beatCutIds), everyBeats, lockedIds);
    std::lock_guard<std::mutex> lock(stateLock_);
    styleScheduleRequest_ = std::move(schedule);
}

void Renderer::applyStyleSchedule() {
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        if (styleScheduleRequest_) {
            styleSchedule_ = std::move(*styleScheduleRequest_);
            styleScheduleRequest_.reset();
        }
    }
    if (styleSchedule_.empty()) return;
    const StyleCue* cue = styleSchedule_.step(frameNowS_ * 1000.0, soundFrame_.beatCount);
    if (!cue) return;
    std::string requestedScene;
    std::string requestedMilk;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        requestedScene = requestedSceneId_;
        requestedMilk = lastMilkPreset_;
    }
    // The segment's first look is already requested (prepare()); asking again would only reload it.
    if (cue->sceneId != requestedScene) {
        setScene(cue->sceneId);
        if (scheduledSceneHook_) scheduledSceneHook_(cue->sceneId);
    }
    if (!cue->milkPath.empty() && cue->milkPath != requestedMilk) loadMilkPreset(cue->milkPath);
}

void Renderer::warmTransition(const std::string& id) { compositePass_.warmTransition(id); }

void Renderer::cut() {
    activeScene_ = nullptr;
    heldScene_ = nullptr;
    heldLayer_ = nullptr;
    outgoingScene_ = nullptr;
    outgoingParams_.reset();
    layerScene_ = nullptr;
}

void Renderer::dropScene(const std::string& id) {
    for (auto it = scenes_.begin(); it != scenes_.end(); ++it) {
        if (it->first != id) continue;
        Scene* s = it->second.get();
        if (s == activeScene_ || s == outgoingScene_ || s == layerScene_) return;
        s->release();
        scenes_.erase(it);
        residency_.remove(id);
        return;
    }
}

void Renderer::admitScene(const std::string& id, const Scene& scene) {
    residency_.insert(id, scene.isFluid(), sizeStamp_);
    trimScenes(id);
}

void Renderer::trimScenes(const std::string& keepId, MemoryTrim trim) {
    std::string requestedId;
    std::string layerId;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        requestedId = requestedSceneId_;
        layerId = layerSceneId_;
    }
    const auto pinned = [&](const std::string& id) {
        if (id == keepId || id == requestedId || id == layerId || id == bornSceneId_) return true;
        const Scene* s = builtScene(id);
        return s != nullptr && (s == activeScene_ || s == outgoingScene_ || s == layerScene_);
    };
    if (trim != MemoryTrim::None) {
        // The passes' own targets are the larger share of what a hidden app holds (the liquid's two
        // sides alone are six screens of pixels). Each is built again by the frame that needs it.
        liquidPass_.releaseTargets();
        trailPass_.releaseTargets();
        uiBlurPass_.releaseTargets();
        bornFbo_.release();
    }
    for (const std::string& victim : residency_.evict(pinned, SceneResidency::spareBudget(appliedTier_, trim))) {
        for (auto it = scenes_.begin(); it != scenes_.end(); ++it) {
            if (it->first != victim) continue;
            it->second->release();
            scenes_.erase(it);
            break;
        }
    }
}

Scene* Renderer::builtScene(const std::string& id) {
    for (auto& entry : scenes_) {
        if (entry.first == id) return entry.second.get();
    }
    return nullptr;
}

// A live frame's way to the scene it wants: the built scene, or null while it is still being
// staged (the caller keeps drawing what it drew). Each call does a bounded slice of work: one
// attempt (setup plus the programs already compiled) or one budgeted ProgramStager step. Exports
// (blockingBuilds_) and scenes that keep deferring (ScenePrep::kMaxAttempts) build inline.
Scene* Renderer::readyScene(const std::string& id) {
    syncShaderOverrides();
    if (Scene* scene = builtScene(id)) {
        residency_.touch(id);
        // Skipped by a tier or scale change while it was not drawn: sized now, as it comes back.
        if (residency_.needsResize(id, sizeStamp_)) {
            scene->resize(renderWidth_, renderHeight_);
            scene->setWindowSize(width_, height_);
            residency_.markSized(id, sizeStamp_);
        }
        return scene;
    }
    Scene* scene = nullptr;
    if (blockingBuilds_.load(std::memory_order_relaxed)) {
        scene = sceneFor(id);
    } else {
        if (prep_.retarget(id)) stager_.clear();
        switch (prep_.next(stager_.idle())) {
            case ScenePrep::Step::Wait:
                stager_.step(monotonicSeconds());
                return nullptr;
            case ScenePrep::Step::Attempt:
                scene = attemptScene(id);
                if (!scene) return nullptr;
                break;
            case ScenePrep::Step::Block:
                GEODE_LOGW(kTag, "%s kept deferring its programs; building it inline", id.c_str());
                scene = sceneFor(id);
                break;
        }
    }
    if (scene) {
        prep_.done();
        stager_.clear();
    }
    return scene;
}

// A secondary target's way in (the layer, Liquid's next style): waits while the stager works for the
// other secondary target, so the two take turns instead of clearing each other's staging (#369).
Scene* Renderer::readySecondary(const std::string& id, const std::string& other) {
    if (!builtScene(id) && prep_.yieldsTo(id, other)) return nullptr;
    return readyScene(id);
}

// One staged build of `id`: the whole setup buildScene does, with every program build answered
// by the stager. Deferred (a program still compiling) drops the scene and its placeholders.
Scene* Renderer::attemptScene(const std::string& id) {
    std::unique_ptr<Scene> scene;
    bool deferred = false;
    holdingErrors_ = true;
    heldErrors_.clear();
    {
        ProgramStager::Attempt attempt(stager_);
        scene = buildScene(id);
        if (scene) scene->prepare();
        deferred = attempt.deferred();
    }
    holdingErrors_ = false;
    std::vector<std::string> errors;
    errors.swap(heldErrors_);
    if (deferred) {
        if (scene) scene->release();
        // Placeholders are unlinked programs; whatever the setup asked of them raised GL errors.
        prober::drainErrors();
        return nullptr;
    }
    for (const auto& message : errors) fail(message);
    if (!scene) return nullptr;
    Scene* raw = scene.get();
    scenes_.emplace_back(id, std::move(scene));
    admitScene(id, *raw);
    return raw;
}

Scene* Renderer::sceneFor(const std::string& id) {
    if (Scene* scene = builtScene(id)) return scene;
    auto built = buildScene(id);
    if (!built) return nullptr;
    Scene* scene = built.get();
    scenes_.emplace_back(id, std::move(built));
    admitScene(id, *scene);
    return scene;
}

std::unique_ptr<Scene> Renderer::buildScene(const std::string& id) {
    auto scene = registry_.create(id, quadVert_);
    if (!scene) return nullptr;
    // Snapshot every cross-thread field this function needs in one lock, then
    // do all the (non-locked) scene setup below so no scene call happens
    // while stateLock_ is held.
    SceneParams initialParams;
    std::string force;
    std::string dye;
    std::string preset;
    std::string goodPreset;
    std::string textureDir;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        initialParams = requestedParams_;
        force = fluidForceSrc_;
        dye = fluidDyeSrc_;
        preset = lastMilkPreset_;
        goodPreset = lastGoodMilkPreset_;
        textureDir = milkTextureDir_;
    }
    if (paletteLutTex_ != 0) scene->setPaletteLut(paletteLutTex_);
    scene->setTouchField(&touchField_);
    scene->setCamera(&camera_.pose());
    scene->init();
    scene->setParams(initialParams);
    scene->resize(renderWidth_, renderHeight_);
    // The user's edited source for this style, whichever renderer this is (live, export, wallpaper,
    // dream): one process-wide table (viz/ShaderOverrides, #364). Recorded so syncShaderOverrides
    // only re-applies what changed after this build.
    const std::string custom = shader_overrides::get(id);
    appliedShaders_[id] = custom;
    if (!custom.empty()) scene->setFragmentSource(custom);
    if (!force.empty() || !dye.empty()) scene->setInjectionShaders(force, dye);
    scene->setWindowSize(width_, height_);
    if (!textureDir.empty()) scene->setMilkTextureDir(textureDir);
    if (!goodPreset.empty()) scene->setMilkFallbackPreset(goodPreset);
    if (!preset.empty()) scene->queueMilkPreset(preset);
    return scene;
}

void Renderer::releaseScenes() {
    stager_.clear();
    prep_.done();
    for (auto& entry : scenes_) entry.second->release();
    scenes_.clear();
    residency_.clear();
    activeScene_ = nullptr;
    heldScene_ = nullptr;
    heldLayer_ = nullptr;
    outgoingScene_ = nullptr;
    layerScene_ = nullptr;
}

}  // namespace geode::viz
