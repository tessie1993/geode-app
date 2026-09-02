#include "viz/scenes/ShaderScene.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "viz/LiveSignal.hpp"

namespace geode::viz {

namespace {

constexpr int kPaletteRows = 5;
constexpr int kMarchMaxSteps = 128;
constexpr int kMarchFloorSteps = 64;
constexpr float kMarchMinDetail = 0.25f;
constexpr float kMarchMaxDetail = 1.5f;

float paletteRowCoordinate(int index) {
    return (static_cast<float>(std::clamp(index, 0, kPaletteRows - 1)) + 0.5f) / static_cast<float>(kPaletteRows);
}

// Port of PcmRow.fill: each destination cell keeps the largest-magnitude sample of its span.
void fillPcmRow(float* dst, int dstSize, const float* source, int count) {
    if (count <= 0) {
        std::fill(dst, dst + dstSize, 0.0f);
        return;
    }
    for (int i = 0; i < dstSize; ++i) {
        const int from = i * count / dstSize;
        const int to = std::min(std::max((i + 1) * count / dstSize, from + 1), count);
        float extreme = 0.0f;
        for (int j = from; j < to; ++j) {
            const float v = source[j];
            const float safe = std::isfinite(v) ? v : 0.0f;
            if (std::fabs(safe) > std::fabs(extreme)) extreme = safe;
        }
        dst[i] = extreme;
    }
}

float flag(bool on) { return on ? 1.0f : 0.0f; }

}  // namespace

ShaderScene::ShaderScene(std::string id, std::string vertexSrc, std::string fragmentSrc, ProgramBinaryCache* cache, SceneHost host)
    : id_(std::move(id)),
      vertexSrc_(std::move(vertexSrc)),
      currentFragment_(std::move(fragmentSrc)),
      cache_(cache),
      host_(std::move(host)),
      pcm_(static_cast<size_t>(kAudioTexWidth) * 8, 0.0f) {
    pendingFragment_ = currentFragment_;
}

ShaderScene::~ShaderScene() { release(); }

void ShaderScene::acceptPcm(const float* samples, int count) {
    const int n = std::min(count, static_cast<int>(pcm_.size()));
    if (n <= 0) return;
    std::copy(samples + (count - n), samples + count, pcm_.begin());
    pcmCount_ = n;
}

void ShaderScene::setFlow(GLuint texture, float strength) {
    flowTex_ = texture;
    flowStrength_ = strength;
}

void ShaderScene::setFragmentSource(const std::string& source) {
    std::lock_guard<std::mutex> lock(pendingLock_);
    pendingFragment_ = source;
    pendingIsUserSource_ = true;
}

void ShaderScene::init() {
    program_ = 0;
    uniforms_ = UniformCache(0);
    {
        std::lock_guard<std::mutex> lock(pendingLock_);
        pendingFragment_ = currentFragment_;
    }
    glGenVertexArrays(1, &vao_);
    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    const bool floatLinear = extensions && std::strstr(extensions, "OES_texture_float_linear") != nullptr;
    audioTex_.createImage(GL_R32F, GL_RED, GL_FLOAT, kAudioTexWidth, 2, floatLinear ? GL_LINEAR : GL_NEAREST, GL_CLAMP_TO_EDGE);
}

void ShaderScene::resize(int width, int height) {
    width_ = width;
    height_ = height;
}

void ShaderScene::update(const GeodeFeatureFrame& features, float dt) {
    const SceneParams& p = params_;
    shaderTime_ = std::fmod(shaderTime_ + p.speed * dt, kTimeWrapSeconds);
    rotationAngle_ = std::fmod(rotationAngle_ + p.rotation * dt, kTwoPi);
    zoomPhase_ = p.endlessZoom ? std::fmod(zoomPhase_ + p.endlessZoomSpeed * dt, 1.0f) : 0.0f;
    if (p.colorCycle) cyclePhase_ = std::fmod(cyclePhase_ + p.cycleSpeed * dt, 1.0f);
    const float drive = p.audioDrive;
    bass_ = std::clamp(features.bass * drive, 0.0f, kAudioClamp);
    mid_ = std::clamp(features.mid * drive, 0.0f, kAudioClamp);
    treble_ = std::clamp(features.treble * drive, 0.0f, kAudioClamp);
    energy_ = std::clamp(features.rms * drive, 0.0f, kAudioClamp);
    const float hit = live::hit(features);
    beatPulse_ = std::max(std::max(hit, beatPulse_ - dt * kPulseDecayPerSecond), 0.0f);
    // The heard transient resets the shared pulse ramp; between hits it free-runs.
    pulsePhase_ = hit > 0.0f ? 0.0f : std::fmod(pulsePhase_ + dt * kPulsePhaseHz, 1.0f);
    for (int i = 0; i < kAudioTexWidth; ++i) {
        const int band = i * GEODE_BAND_COUNT / kAudioTexWidth;
        texData_[static_cast<size_t>(i)] = std::clamp(features.bands[band] * drive, 0.0f, kAudioClamp);
    }
    if (pcmCount_ > 0) {
        fillPcmRow(waveRow_.data(), kAudioTexWidth, pcm_.data(), pcmCount_);
        pcmCount_ = 0;
    } else {
        fillPcmRow(waveRow_.data(), kAudioTexWidth, features.waveform, GEODE_WAVEFORM_POINTS);
    }
    for (int i = 0; i < kAudioTexWidth; ++i) {
        texData_[static_cast<size_t>(kAudioTexWidth + i)] = std::clamp(waveRow_[static_cast<size_t>(i)] * drive * 0.5f + 0.5f, 0.0f, 1.0f);
    }
}

void ShaderScene::draw(float timeSeconds) {
    (void) timeSeconds;
    compilePendingIfAny();
    if (program_ == 0) return;
    glDisable(GL_BLEND);
    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, audioTex_.id());
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kAudioTexWidth, 2, GL_RED, GL_FLOAT, texData_.data());
    uploadParams();
    glUniform2f(uniforms_.loc("uResolution"), static_cast<float>(width_), static_cast<float>(height_));
    glUniform1i(uniforms_.loc("uAudioTex"), 0);
    if (flowTex_ != 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, flowTex_);
        glUniform1i(uniforms_.loc("uFlow"), 1);
        set1f("uFlowStrength", flowStrength_);
        glActiveTexture(GL_TEXTURE0);
    }
    const SceneParams& p = params_;
    const bool lutSelected = p.paletteLut >= 0 && paletteLutTex_ != 0;
    if (paletteLutTex_ != 0) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, paletteLutTex_);
        glUniform1i(uniforms_.loc("uPalLut"), 2);
        glActiveTexture(GL_TEXTURE0);
    }
    set1f("uPalLutMix", flag(lutSelected));
    set1f("uPalLutRow", paletteRowCoordinate(std::max(p.paletteLut, 0)));
    set1f("uSteps", marchSteps(p.marchDetail));
    uploadTouch();
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void ShaderScene::uploadParams() {
    const SceneParams& p = params_;
    set1f("uTime", shaderTime_);
    set1f("uBass", bass_);
    set1f("uMid", mid_);
    set1f("uTreble", treble_);
    set1f("uEnergy", energy_);
    set1f("uBeat", beatPulse_);
    set1f("uSpeed", p.speed);
    set1f("uZoom", p.zoom);
    set1f("uRotation", rotationAngle_);
    set1f("uZoomPhase", zoomPhase_);
    set1f("uColorShift", p.colorShift + cyclePhase_);
    set1f("uHueRange", p.hueRange);
    set1f("uSat", p.saturation);
    set1f("uBright", p.brightness);
    set1f("uInvert", flag(p.invert));
    set1f("uIntensity", p.intensity);
    set1f("uMirrorX", flag(p.mirror));
    set1f("uBeatResponse", p.beatResponse);
    set1f("uTurbulence", p.turbulence);
    set1f("uPalBase", p.paletteBase());
    set1f("uPalRange", p.paletteRange());
    set1f("uPal2Base", p.palette2Base());
    set1f("uPal2Range", p.palette2Range());
    set1f("uPaletteMix", p.paletteMix);
    set1f("uDuotone", flag(p.duotone));
    set1f("uBloom", p.bloom);
    set1f("uWarp", p.warp);
    set1f("uRipple", p.ripple);
    set1f("uSymmetry", static_cast<float>(p.symmetry));
    set1f("uKaleido", flag(p.kaleidoscope));
    set1f("uMorph", p.morph);
    set1f("uPixelate", p.pixelate);
    set1f("uPosterize", p.posterize);
    set1f("uSway", p.sway);
    set1f("uPulse", p.pulse);
    set1f("uBeatPhase", pulsePhase_);
    set1f("uDriftX", p.driftX);
    set1f("uDriftY", p.driftY);
    set1f("uShake", p.shake);
    set1f("uTile", p.tile);
    set1f("uTwist", p.twist);
    set1f("uTemperature", p.temperature);
    set1f("uSolarize", flag(p.solarize));
    set1f("uFlash", p.flash);
    set1f("uContrast", p.contrast);
    set1f("uGamma", p.gamma);
}

void ShaderScene::uploadTouch() {
    if (touchField_) {
        touchAnchor_ = {touchField_->anchorX(), touchField_->anchorY(), touchField_->anchorStrength(), touchField_->anchorAge()};
        touchPoints_ = touchField_->points();
    } else {
        touchAnchor_.fill(0.0f);
        touchPoints_.fill(0.0f);
    }
    glUniform4fv(uniforms_.loc("uTouchAnchor"), 1, touchAnchor_.data());
    // Only as many elements as survived linking: uploading past the live length is an INVALID_OPERATION.
    glUniform4fv(uniforms_.loc("uTouchPoints"), uniforms_.arrayCount("uTouchPoints", TouchField::kMaxPoints), touchPoints_.data());
    glUniform1i(uniforms_.loc("uTouchCount"), touchField_ ? touchField_->count() : 0);
    glUniform1i(uniforms_.loc("uTouchGesture"), touchField_ ? touchField_->gesture() : TouchField::kGestureNone);
    glUniform2f(uniforms_.loc("uTouchAxis"), touchField_ ? touchField_->axisX() : 0.0f, touchField_ ? touchField_->axisY() : 0.0f);
    set1f("uTouchSpin", touchField_ ? touchField_->spin() : 0.0f);
}

void ShaderScene::set1f(const char* name, float value) {
    glUniform1f(uniforms_.loc(name), value);
}

float ShaderScene::marchSteps(float detail) {
    if (detail != stepsDetail_) {
        stepsDetail_ = detail;
        const float t = std::clamp((detail - kMarchMinDetail) / (kMarchMaxDetail - kMarchMinDetail), 0.0f, 1.0f);
        stepsBudget_ = static_cast<float>(std::lround(kMarchFloorSteps + (kMarchMaxSteps - kMarchFloorSteps) * t));
    }
    return stepsBudget_;
}

void ShaderScene::compilePendingIfAny() {
    std::optional<std::string> pending;
    bool fromUser = false;
    {
        std::lock_guard<std::mutex> lock(pendingLock_);
        pending.swap(pendingFragment_);
        fromUser = pendingIsUserSource_;
        pendingIsUserSource_ = false;
    }
    if (!pending) return;
    std::string error;
    const GLuint built = program::build(vertexSrc_, *pending, cache_, &error);
    if (built == 0) {
        host_.onShaderError(error);
        return;
    }
    if (program_ != 0) glDeleteProgram(program_);
    program_ = built;
    uniforms_ = UniformCache(built);
    currentFragment_ = *pending;
    host_.onShaderError("");
    if (fromUser) host_.onUserSourceCompiled(id_, currentFragment_);
}

void ShaderScene::release() {
    if (program_ != 0) glDeleteProgram(program_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    audioTex_.release();
    program_ = 0;
    vao_ = 0;
    uniforms_ = UniformCache(0);
}

}  // namespace geode::viz
