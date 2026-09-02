#include "viz/scenes/MilkdropScene.hpp"

#include <algorithm>
#include <array>
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

void MilkdropScene::EngineDeleter::operator()(projectm_handle h) const {
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

// The engine renders at the size the renderer asked for in resize(), like every other scene; the
// frame target is (re)built to the same size, so projectM's viewport and the FBO always agree.
void MilkdropScene::ensureEngine() {
    if (width_ <= 1 || height_ <= 1) return;
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
    if (engineWidth_ != width_ || engineHeight_ != height_) {
        projectm_set_window_size(engine_.get(), static_cast<size_t>(width_), static_cast<size_t>(height_));
        engineWidth_ = width_;
        engineHeight_ = height_;
    }
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
    if (!engine_ || !frame_.ensure(engineWidth_, engineHeight_)) return;
    loadPendingPreset(nowSeconds());
    const SceneParams& p = params_;
    projectm_set_beat_sensitivity(engine_.get(), std::clamp(0.2f + p.beatResponse, 0.2f, 3.0f));

    // The engine composites its frame straight into the scene's FBO. The window surface is never
    // read or written here, so the result does not depend on the EGL config the host chose.
    projectm_opengl_render_frame_fbo(engine_.get(), frame_.fbo());
    if (const auto error = takeError()) host_.onShaderError(*error);
    diagnoseBlackFrame();
    drainEngineErrors();

    resetFrameState();
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(0, 0, width_, height_);

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(postProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, frame_.tex());
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

// Samples the scene's own target, i.e. what the post pass is about to display; a black result here
// means the engine produced nothing, not that a copy failed somewhere in between.
void MilkdropScene::diagnoseBlackFrame() {
    if (diagDone_ || diagFrames_ >= kDiagFrames) return;
    diagFrames_++;
    if (diagFrames_ <= kDiagWarmup) return;
    const int w = frame_.width();
    const int h = frame_.height();
    std::array<unsigned char, 8> px{};
    glBindFramebuffer(GL_READ_FRAMEBUFFER, frame_.fbo());
    glReadPixels(w / 2, h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glReadPixels(w / 3, h / 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px.data() + 4);
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
                            std::to_string(w) + "x" + std::to_string(h) + " (preset=" + preset +
                            "). adb logcat -s milkdrop-jni for the native side.");
    }
}

// projectM leaves whatever GL errors its preset pipeline raised in the queue. Clear them so the
// renderer's own probes stay attributable, but report the first one: a silent drain is how the
// previous copy-from-window failure went unnoticed.
void MilkdropScene::drainEngineErrors() {
    GLenum first = GL_NO_ERROR;
    for (int drained = 0; drained < 8; ++drained) {
        const GLenum err = glGetError();
        if (err == GL_NO_ERROR) break;
        if (first == GL_NO_ERROR) first = err;
    }
    if (first != GL_NO_ERROR && !reportedEngineGlError_) {
        reportedEngineGlError_ = true;
        GEODE_LOGW(kTag, "projectM left GL error 0x%x pending after rendering (reported once)", first);
    }
}

void MilkdropScene::release() {
    engine_.reset();
    engineWidth_ = 0;
    engineHeight_ = 0;
    frame_.release();
    if (postProgram_ != 0) glDeleteProgram(postProgram_);
    postProgram_ = 0;
    postProgramOk_ = false;
    if (postVao_ != 0) glDeleteVertexArrays(1, &postVao_);
    postVao_ = 0;
    postLocs_ = UniformCache(0);
}

}  // namespace geode::viz
