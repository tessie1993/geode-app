#include "viz/Renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "util/Log.hpp"

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
                          [this](const std::string& id, const std::string& src) { rememberCustomShader(id, src); },
                          [this] { return thermal_.pacedFps(); }}),
      compositePass_(assets_, &programCache_),
      pcm_(kPcmCapacity, 0.0f) {
    programCache_.install(cacheDir_);
}

Renderer::~Renderer() = default;

void Renderer::setParams(const SceneParams& params) {
    std::lock_guard<std::mutex> lock(stateLock_);
    requestedParams_ = params;
}

bool Renderer::setParam(const std::string& key, float value) {
    std::lock_guard<std::mutex> lock(stateLock_);
    return requestedParams_.set(key, value);
}

void Renderer::setFeatures(const GeodeFeatureFrame& features) {
    std::lock_guard<std::mutex> lock(stateLock_);
    features_ = features;
}

void Renderer::setLayer(const std::string& sceneId, float mix, int blendOrdinal) {
    std::lock_guard<std::mutex> lock(stateLock_);
    layerSceneId_ = sceneId;
    layerMix_ = mix;
    layerBlend_ = blendOrdinal;
}

void Renderer::setTransition(const std::string& id, int64_t durationMs) {
    std::lock_guard<std::mutex> lock(stateLock_);
    transitionId_ = safety::transitionId(id);
    transitionDurationMs_ = durationMs;
}

void Renderer::beginParamMorph(float seconds) {
    if (seconds <= 0.0f) return;
    std::lock_guard<std::mutex> lock(stateLock_);
    morphFadeSec_ = seconds;
    morphRemainSec_ = seconds * 3.0f;
}

void Renderer::pushPcm(const float* samples, int count) {
    std::lock_guard<std::mutex> lock(stateLock_);
    const int n = std::min(count, kPcmCapacity);
    if (n <= 0) return;
    std::copy(samples + (count - n), samples + count, pcm_.begin());
    pcmCount_ = n;
}

void Renderer::setCustomShader(const std::string& sceneId, const std::string& fragmentSource) {
    std::lock_guard<std::mutex> lock(stateLock_);
    pendingShaders_.emplace_back(sceneId, fragmentSource);
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

void Renderer::applyPendingFluidInjection() {
    std::string force;
    std::string dye;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        if (!fluidInjectionDirty_) return;
        force = fluidForceSrc_;
        dye = fluidDyeSrc_;
    }
    Scene* fluid = builtScene("fluid");
    if (!fluid) return;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        fluidInjectionDirty_ = false;
    }
    fluid->setInjectionShaders(force, dye);
}

void Renderer::setLfoConfigs(const std::array<LfoConfig, LfoEngine::kSlots>& configs) {
    std::lock_guard<std::mutex> lock(stateLock_);
    lfo_.configs = configs;
}

void Renderer::setAdsrConfigs(const std::array<AdsrConfig, AdsrEngine::kCount>& configs) {
    std::lock_guard<std::mutex> lock(stateLock_);
    adsr_.configs = configs;
}

void Renderer::fail(const std::string& message) {
    lastError_ = message;
    if (!message.empty()) GEODE_LOGW(kTag, "%s", message.c_str());
}

void Renderer::rememberCustomShader(const std::string& sceneId, const std::string& source) {
    std::lock_guard<std::mutex> lock(stateLock_);
    for (auto& entry : customShaders_) {
        if (entry.first == sceneId) {
            entry.second = source;
            return;
        }
    }
    customShaders_.emplace_back(sceneId, source);
}

std::string Renderer::customShaderFor(const std::string& sceneId) const {
    std::lock_guard<std::mutex> lock(stateLock_);
    for (const auto& entry : customShaders_) {
        if (entry.first == sceneId) return entry.second;
    }
    return {};
}

void Renderer::onSurfaceCreated() {
    profile_ = deviceGl_.profileWithCurrentContext();
    GEODE_LOGI(kTag, "surface created: %s", profile_.summary().c_str());
    thermal_.onSurfaceRecreated();
    programCache_.prime();
    releaseScenes();
    if (paletteLutTex_ != 0) {
        glDeleteTextures(1, &paletteLutTex_);
        paletteLutTex_ = 0;
    }
    touchField_.reset();
    fboA_.release();
    fboB_.release();
    compositePass_.releaseStaleTextures();
    activeScene_ = nullptr;
    outgoingScene_ = nullptr;
    outgoingParams_.reset();
    overlays_.recreate();
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        if (!fluidForceSrc_.empty() || !fluidDyeSrc_.empty()) fluidInjectionDirty_ = true;
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

    // Port of CyclicPalettes.createTexture: five 256-entry RGB rows.
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
}

void Renderer::surfaceChanged(int width, int height) {
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    glViewport(0, 0, width_, height_);
    applyRenderScale();
}

void Renderer::applyRenderScale() {
    appliedTier_ = thermal_.tier();
    const float scale = supersampleFactor(width_, height_) * thermalTierInfo(appliedTier_).renderScale;
    renderWidth_ = std::max(static_cast<int>(width_ * scale), 1);
    renderHeight_ = std::max(static_cast<int>(height_ * scale), 1);
    for (auto& entry : scenes_) entry.second->resize(renderWidth_, renderHeight_);
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
    return true;
}

void Renderer::warmTransition(const std::string& id) { compositePass_.warmTransition(id); }

void Renderer::cut() {
    activeScene_ = nullptr;
    outgoingScene_ = nullptr;
    outgoingParams_.reset();
    layerScene_ = nullptr;
}

Scene* Renderer::builtScene(const std::string& id) {
    for (auto& entry : scenes_) {
        if (entry.first == id) return entry.second.get();
    }
    return nullptr;
}

Scene* Renderer::sceneFor(const std::string& id) {
    if (Scene* scene = builtScene(id)) return scene;
    auto built = buildScene(id);
    if (!built) return nullptr;
    Scene* scene = built.get();
    scenes_.emplace_back(id, std::move(built));
    return scene;
}

std::unique_ptr<Scene> Renderer::buildScene(const std::string& id) {
    auto scene = registry_.create(id, quadVert_);
    if (!scene) return nullptr;
    if (paletteLutTex_ != 0) scene->setPaletteLut(paletteLutTex_);
    scene->setTouchField(&touchField_);
    scene->init();
    scene->setParams(requestedParams_);
    scene->resize(renderWidth_, renderHeight_);
    const std::string custom = customShaderFor(id);
    if (!custom.empty()) scene->setFragmentSource(custom);
    std::string force;
    std::string dye;
    {
        std::lock_guard<std::mutex> lock(stateLock_);
        force = fluidForceSrc_;
        dye = fluidDyeSrc_;
    }
    if (!force.empty() || !dye.empty()) scene->setInjectionShaders(force, dye);
    return scene;
}

void Renderer::releaseScenes() {
    for (auto& entry : scenes_) entry.second->release();
    scenes_.clear();
    activeScene_ = nullptr;
    outgoingScene_ = nullptr;
    layerScene_ = nullptr;
}

}  // namespace geode::viz
