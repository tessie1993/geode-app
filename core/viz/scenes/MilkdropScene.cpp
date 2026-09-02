#include "viz/scenes/MilkdropScene.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "util/Log.hpp"
#include "viz/CompositeGrade.hpp"
#include "viz/LiveSignal.hpp"
#include "viz/Quad.hpp"

namespace geode::viz {

namespace {

constexpr const char* kTag = "milkdrop-jni";

std::string parentDir(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos || slash == 0 ? "/" : path.substr(0, slash);
}

std::string stemOf(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

}  // namespace

void MilkdropScene::EngineDeleter::operator()(std::remove_pointer_t<projectm_handle> h) const {
    projectm_destroy(h);
}

void MilkdropScene::onPresetSwitchFailed(const char* presetFilename, const char* message, void* userData) {
    auto* self = static_cast<MilkdropScene*>(userData);
    if (!self) return;
    std::lock_guard<std::mutex> lock(self->errorLock_);
    self->lastError_ = std::string(presetFilename ? presetFilename : "?") + ": " + (message ? message : "unknown error");
    GEODE_LOGE(kTag, "preset switch failed: %s", self->lastError_.c_str());
}

double MilkdropScene::nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::optional<std::string> MilkdropScene::takeError() {
    std::lock_guard<std::mutex> lock(errorLock_);
    if (lastError_.empty()) return std::nullopt;
    std::string out;
    out.swap(lastError_);
    return out;
}

void MilkdropScene::acceptPcm(const float* samples, int count) {
    const int n = std::min(count, kPcmCapacity);
    if (n <= 0) return;
    if (pcmCount_ + n > kPcmCapacity) pcmCount_ = 0;
    std::copy(samples + (count - n), samples + count, pcm_.begin() + pcmCount_);
    pcmCount_ += n;
}

void MilkdropScene::queueMilkPreset(const std::string& path) {
    std::lock_guard<std::mutex> lock(presetLock_);
    pendingPresetPath_ = path;
}

void MilkdropScene::reloadMilkPreset() {
    std::lock_guard<std::mutex> lock(presetLock_);
    if (lastPresetPath_.empty()) return;
    pendingPresetPath_ = lastPresetPath_;
    lastLoadSeconds_ = 0.0;
}

void MilkdropScene::setMilkTextureDir(const std::string& dir) {
    std::lock_guard<std::mutex> lock(presetLock_);
    sharedTextureDir_ = dir;
}

void MilkdropScene::setWindowSize(int width, int height) {
    windowWidth_ = width;
    windowHeight_ = height;
}

void MilkdropScene::init() {
    release();
    reportedCreateFailure_ = false;
    diagFrames_ = 0;
    diagDone_ = false;
    std::string error;
    postProgram_ = loader_.build("fade_vert.glsl", "pm_post_frag.glsl", &error);
    if (postProgram_ == 0) {
        host_.onShaderError("MilkDrop unavailable on this GPU: " + error);
        return;
    }
    postProgramOk_ = true;
    postLocs_ = UniformCache(postProgram_);
    glGenVertexArrays(1, &postVao_);
}

void MilkdropScene::resize(int width, int height) {
    width_ = width;
    height_ = height;
}

void MilkdropScene::ensureEngine() {
    const int w = effectiveWindowWidth();
    const int h = effectiveWindowHeight();
    if (w <= 1 || h <= 1) return;
    if (!engine_) {
        engine_.reset(projectm_create());
        if (!engine_) {
            if (!reportedCreateFailure_) {
                reportedCreateFailure_ = true;
                host_.onShaderError("projectM engine failed to initialize (adb logcat -s milkdrop-jni)");
            }
            GEODE_LOGE(kTag, "projectm_create returned NULL");
            return;
        }
        projectm_set_fps(engine_.get(), 60);
        projectm_set_mesh_size(engine_.get(), 48, 32);
        projectm_set_soft_cut_duration(engine_.get(), 3.0);
        projectm_set_preset_duration(engine_.get(), 999999.0);
        projectm_set_preset_locked(engine_.get(), true);
        projectm_set_aspect_correction(engine_.get(), true);
        projectm_set_preset_switch_failed_event_callback(engine_.get(), onPresetSwitchFailed, this);
        {
            std::lock_guard<std::mutex> lock(presetLock_);
            if (!lastPresetPath_.empty()) pendingPresetPath_ = lastPresetPath_;
        }
        engineWidth_ = 0;
    }
    if (engineWidth_ != w || engineHeight_ != h) {
        projectm_set_window_size(engine_.get(), static_cast<size_t>(w), static_cast<size_t>(h));
        engineWidth_ = w;
        engineHeight_ = h;
    }
}

void MilkdropScene::ensureFrameTexture() {
    const int w = engineWidth_;
    const int h = engineHeight_;
    if (w <= 1 || h <= 1) return;
    if (frameTex_ != 0 && texWidth_ == w && texHeight_ == h) return;
    releaseFrameTexture();
    glGenTextures(1, &frameTex_);
    glBindTexture(GL_TEXTURE_2D, frameTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    texWidth_ = w;
    texHeight_ = h;
}

void MilkdropScene::update(const GeodeFeatureFrame& features, float dt) {
    const SceneParams& p = params_;
    rotationAngle_ = std::fmod(rotationAngle_ + p.rotation * dt, kTwoPi);
    zoomPhase_ = p.endlessZoom ? std::fmod(zoomPhase_ + p.endlessZoomSpeed * dt, 1.0f) : 0.0f;
    if (p.colorCycle) cyclePhase_ = std::fmod(cyclePhase_ + p.cycleSpeed * dt, 1.0f);
    beatPulse_ = std::max(std::max(live::hit(features), beatPulse_ - dt * 3.0f), 0.0f);
    if (!engine_) return;
    if (pcmCount_ > 0) {
        const int n = std::min(pcmCount_, kEnginePcmSamples);
        if (n < pcmCount_) std::copy(pcm_.begin() + (pcmCount_ - n), pcm_.begin() + pcmCount_, pcm_.begin());
        pcmCount_ = 0;
        projectm_pcm_add_float(engine_.get(), pcm_.data(), static_cast<unsigned int>(n), PROJECTM_MONO);
    } else {
        const int n = std::min(GEODE_WAVEFORM_POINTS, kEnginePcmSamples);
        projectm_pcm_add_float(engine_.get(), features.waveform + (GEODE_WAVEFORM_POINTS - n), static_cast<unsigned int>(n), PROJECTM_MONO);
    }
}

void MilkdropScene::loadPendingPreset(double now) {
    std::string path;
    std::string shared;
    {
        std::lock_guard<std::mutex> lock(presetLock_);
        if (pendingPresetPath_.empty() || now - lastLoadSeconds_ < kLoadDebounceSeconds) return;
        path.swap(pendingPresetPath_);
        shared = sharedTextureDir_;
    }
    lastLoadSeconds_ = now;
    const std::string dir = parentDir(path);
    // The per-preset link directory goes first: search order is the only precedence projectM has.
    std::vector<std::string> dirs = {dir + "/textures/.links/" + stemOf(path), dir, dir + "/textures"};
    if (!shared.empty()) dirs.push_back(shared);
    std::vector<const char*> paths;
    for (const auto& d : dirs) paths.push_back(d.c_str());
    projectm_set_texture_search_paths(engine_.get(), paths.data(), paths.size());
    {
        std::lock_guard<std::mutex> lock(errorLock_);
        lastError_.clear();
    }
    projectm_load_preset_file(engine_.get(), path.c_str(), params_.milkdropBlendPresets);
    projectm_set_preset_locked(engine_.get(), true);
    const auto error = takeError();
    host_.onShaderError(error.value_or(""));
    if (!error) {
        {
            std::lock_guard<std::mutex> lock(presetLock_);
            lastPresetPath_ = path;
        }
        if (host_.onMilkPresetLoaded) host_.onMilkPresetLoaded(path);
    }
}

void MilkdropScene::draw(float timeSeconds) {
    (void) timeSeconds;
    if (!postProgramOk_) return;
    GLint prevFbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
    // Reset before the engine as well as after: projectM assumes default GL state and restores little.
    resetFrameState();
    ensureEngine();
    ensureFrameTexture();
    if (!engine_ || frameTex_ == 0) return;
    loadPendingPreset(nowSeconds());
    const SceneParams& p = params_;
    projectm_set_beat_sensitivity(engine_.get(), std::clamp(0.2f + p.beatResponse, 0.2f, 3.0f));

    projectm_opengl_render_frame(engine_.get());
    if (const auto error = takeError()) host_.onShaderError(*error);

    GLint prevReadFbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glBindTexture(GL_TEXTURE_2D, frameTex_);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, texWidth_, texHeight_);
    diagnoseBlackFrame();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));

    for (int drained = 0; glGetError() != GL_NO_ERROR && drained < 8; ++drained) {
    }
    resetFrameState();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(0, 0, width_, height_);

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(postProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, frameTex_);
    glUniform1i(postLocs_.loc("uTex"), 0);
    set1f("uZoom", p.zoom * (1.0f + beatPulse_ * p.beatResponse * 0.08f));
    set1f("uRotation", rotationAngle_);
    set1f("uZoomPhase", zoomPhase_);
    set1f("uMirrorX", p.mirror ? 1.0f : 0.0f);
    set1f("uPalBase", p.paletteBase());
    set1f("uPalSpan", grade::paletteSpan(p.hueRange, p.paletteRange()));
    set1f("uPalTint", grade::paletteTintAmount(p.milkdropPaletteTint));
    set1f("uHue", p.colorShift + cyclePhase_);
    set1f("uSat", p.saturation);
    set1f("uBright", p.brightness);
    set1f("uContrast", p.contrast);
    set1f("uGamma", p.gamma);
    set1f("uInvert", p.invert ? 1.0f : 0.0f);
    set1f("uIntensity", p.intensity);
    glBindVertexArray(postVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

void MilkdropScene::diagnoseBlackFrame() {
    if (diagDone_ || diagFrames_ >= kDiagFrames) return;
    diagFrames_++;
    if (diagFrames_ <= kDiagWarmup) return;
    std::array<unsigned char, 8> px{};
    glReadPixels(texWidth_ / 2, texHeight_ / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glReadPixels(texWidth_ / 3, texHeight_ / 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px.data() + 4);
    const bool sawLight = px[0] > 8 || px[1] > 8 || px[2] > 8 || px[4] > 8 || px[5] > 8 || px[6] > 8;
    if (sawLight) {
        diagDone_ = true;
    } else if (diagFrames_ >= kDiagFrames) {
        diagDone_ = true;
        std::string preset;
        {
            std::lock_guard<std::mutex> lock(presetLock_);
            preset = lastPresetPath_.empty() ? "idle" : lastPresetPath_.substr(lastPresetPath_.find_last_of('/') + 1);
        }
        host_.onShaderError("MilkDrop diagnostic: the engine painted a black frame for " + std::to_string(kDiagFrames) + " frames at " +
                            std::to_string(texWidth_) + "x" + std::to_string(texHeight_) + " (preset=" + preset +
                            "). adb logcat -s milkdrop-jni for the native side.");
    }
}

void MilkdropScene::releaseFrameTexture() {
    if (frameTex_ != 0) glDeleteTextures(1, &frameTex_);
    frameTex_ = 0;
    texWidth_ = 0;
    texHeight_ = 0;
}

void MilkdropScene::release() {
    engine_.reset();
    engineWidth_ = 0;
    engineHeight_ = 0;
    releaseFrameTexture();
    if (postProgram_ != 0) glDeleteProgram(postProgram_);
    postProgram_ = 0;
    postProgramOk_ = false;
    if (postVao_ != 0) glDeleteVertexArrays(1, &postVao_);
    postVao_ = 0;
    postLocs_ = UniformCache(0);
}

}  // namespace geode::viz
