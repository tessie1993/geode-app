#include "viz/scenes/ShaderScene.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

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

float flag(bool on) { return on ? 1.0f : 0.0f; }

}  // namespace

ShaderScene::ShaderScene(std::string id, std::string vertexSrc, std::string fragmentSrc, ProgramBinaryCache* cache, SceneHost host)
    : id_(std::move(id)),
      vertexSrc_(std::move(vertexSrc)),
      currentFragment_(std::move(fragmentSrc)),
      cache_(cache),
      host_(std::move(host)) {
    pendingFragment_ = currentFragment_;
}

ShaderScene::~ShaderScene() { release(); }

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
    motionField_.setVariant(styleVariantSeed(id_));
    program_ = 0;
    uniforms_ = UniformCache(0);
    {
        std::lock_guard<std::mutex> lock(pendingLock_);
        pendingFragment_ = currentFragment_;
    }
    glGenVertexArrays(1, &vao_);
    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    const bool floatLinear = extensions && std::strstr(extensions, "OES_texture_float_linear") != nullptr;
    soundTex_.createImage(GL_R32F, GL_RED, GL_FLOAT, kSoundTexWidth, kSoundTexRows, floatLinear ? GL_LINEAR : GL_NEAREST,
                          GL_CLAMP_TO_EDGE);
}

void ShaderScene::resize(int width, int height) {
    width_ = width;
    height_ = height;
}

void ShaderScene::update(const GeodeSoundFrame& sound, float dt) {
    sound_ = sound;
    const SceneParams& p = params_;
    shaderTime_ = std::fmod(shaderTime_ + p.speed * dt, kTimeWrapSeconds);
    rotationAngle_ = std::fmod(rotationAngle_ + p.rotation * dt, kTwoPi);
    zoomPhase_ = p.endlessZoom ? std::fmod(zoomPhase_ + p.endlessZoomSpeed * dt, 1.0f) : 0.0f;
    if (p.colorCycle) cyclePhase_ = std::fmod(cyclePhase_ + p.cycleSpeed * dt, 1.0f);
    // Wave three: the continuous motion layer, never a transient/beat trigger.
    motionField_.step(sound_, dt);
    reactSignals_.step(sound_, dt);
}

void ShaderScene::draw(float timeSeconds) {
    (void) timeSeconds;
    compilePendingIfAny();
    if (program_ == 0) return;
    glDisable(GL_BLEND);
    glUseProgram(program_);
    uploadParams();
    glUniform2f(uniforms_.loc("uResolution"), static_cast<float>(width_), static_cast<float>(height_));
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
    uploadMotion();
    uploadTouch();
    uploadCamera();
    uploadSound();
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

namespace {
// Linear resample of `src` (n points) across `width` texels, so every row of uSoundTex reads 0..1 in x
// whatever the engine's point count.
void resampleRow(const float* src, int n, float* dst, int width) {
    for (int i = 0; i < width; ++i) {
        const float x = width > 1 ? static_cast<float>(i) * static_cast<float>(n - 1) / static_cast<float>(width - 1) : 0.0f;
        const int a = std::min(static_cast<int>(x), n - 1);
        const int b = std::min(a + 1, n - 1);
        const float t = x - static_cast<float>(a);
        dst[i] = src[a] + (src[b] - src[a]) * t;
    }
}
}  // namespace

// The whole GeodeSoundFrame, raw: scalars as uSound (lib_sound names them), the arrays as uSoundTex on
// unit 3. No drive, clamp or slew here; the engine's values are already dt-scaled and bounded, and
// styles shape them themselves. Unused by a style, both cost one call the driver drops.
void ShaderScene::uploadSound() {
    const float* scalars = reinterpret_cast<const float*>(&sound_);
    std::copy(scalars, scalars + GEODE_SOUND_SCALARS, soundScalars_.begin());
    glUniform4fv(uniforms_.loc("uSound"), kSoundVec4s, soundScalars_.data());
    resampleRow(sound_.spectrum, GEODE_SOUND_SPECTRUM_POINTS, soundTexData_.data(), kSoundTexWidth);
    resampleRow(sound_.waveform, GEODE_SOUND_WAVEFORM_POINTS, soundTexData_.data() + kSoundTexWidth, kSoundTexWidth);
    resampleRow(sound_.eq, GEODE_SOUND_EQ_BANDS, soundTexData_.data() + 2 * kSoundTexWidth, kSoundTexWidth);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, soundTex_.id());
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kSoundTexWidth, kSoundTexRows, GL_RED, GL_FLOAT, soundTexData_.data());
    glUniform1i(uniforms_.loc("uSoundTex"), 3);
    glActiveTexture(GL_TEXTURE0);
    set1f("uSoundDrive", std::max(params_.audioDrive, 0.0f));
    const ReactSignals& r = reactSignals_;
    glUniform4f(uniforms_.loc("uSoundEvents"), r.value(ReactSource::HardCut), r.value(ReactSource::Surge), r.value(ReactSource::Returned),
                r.value(ReactSource::NoteChange));
    glUniform2f(uniforms_.loc("uSoundCounts"), r.onsetCount(), r.cutCount());
}

void ShaderScene::uploadParams() {
    const SceneParams& p = params_;
    set1f("uTime", shaderTime_);
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

// Every style is handed these; the ones that read none of them link them away
// and glUniform on a -1 location is a no-op, so this costs nothing on a style
// that has not been converted.
void ShaderScene::uploadMotion() {
    // Wave three: the continuous replacement for the spike-latched state.
    // Nothing here is a trigger; every value is a running average, a
    // phase-locked oscillator gated by confidence, or a slow re-target that
    // eases over seconds. See viz/MotionField.hpp for the derivation.
    const MotionField::State& m = motionField_.state();
    set1f("uEnergyRel", m.energyRel);
    set1f("uBassRel", m.bassRel);
    set1f("uMidRel", m.midRel);
    set1f("uTrebRel", m.trebRel);
    set1f("uMotionBright", m.bright);
    set1f("uHarmony", m.harmony);
    set1f("uKeyHue", m.keyHue);
    set1f("uKeyStrength", m.keyStrength);
    set1f("uBeatOsc", m.beatOsc);
    set1f("uBarOsc", m.barOsc);
    glUniform2f(uniforms_.loc("uOrbit"), m.orbitX, m.orbitY);
    set1f("uDrift", m.drift);
    set1f("uBreath", m.breath);
    set1f("uFlowPhase", m.flowPhase);
    set1f("uMotion", std::clamp(params_.motionAmount, 0.0f, 1.0f));

    // Legacy since wave three, held at their neutral constant until R08
    // deletes the readers: a spike that never fires, a spawn born once and
    // never re-rolled, a direction that never turns.
    set1f("uSpike", 0.0f);
    set1f("uSpawnSeed", 0.0f);
    set1f("uSpawnAge", 1000.0f);
    set1f("uFormPhase", 0.0f);
    glUniform2f(uniforms_.loc("uMoveDir"), 1.0f, 0.0f);
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

void ShaderScene::uploadCamera() {
    if (camera_) {
        const CameraPose& c = *camera_;
        const float basis[9] = {c.right[0], c.right[1], c.right[2], c.up[0], c.up[1], c.up[2], c.fwd[0], c.fwd[1], c.fwd[2]};
        glUniform3fv(uniforms_.loc("uCamPos"), 1, c.pos.data());
        glUniformMatrix3fv(uniforms_.loc("uCamBasis"), 1, GL_FALSE, basis);
        set1f("uCamFocal", c.focal);
        set1f("uCamTravel", c.travel);
        set1f("uCamDolly", c.dolly);
        glUniform3f(uniforms_.loc("uCamLook"), c.yaw, c.pitch, c.roll);
    }
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
    drawsCamera_ = uniforms_.loc("uCamBasis") >= 0;
    currentFragment_ = *pending;
    host_.onShaderError("");
    if (fromUser) host_.onUserSourceCompiled(id_, currentFragment_);
}

void ShaderScene::release() {
    if (program_ != 0) glDeleteProgram(program_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    soundTex_.release();
    program_ = 0;
    vao_ = 0;
    uniforms_ = UniformCache(0);
}

}  // namespace geode::viz
