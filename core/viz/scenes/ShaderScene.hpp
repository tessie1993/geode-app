#pragma once
#include <GLES3/gl3.h>

#include <array>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "viz/Program.hpp"
#include "viz/Scene.hpp"
#include "viz/Texture.hpp"

namespace geode::viz {

// Port of ShaderScene.kt: one fragment style on the fullscreen triangle, fed the shared uniform block.
class ShaderScene : public Scene {
public:
    static constexpr int kAudioTexWidth = 512;

    ShaderScene(std::string id, std::string vertexSrc, std::string fragmentSrc, ProgramBinaryCache* cache, SceneHost host);
    ~ShaderScene() override;

    const std::string& id() const override { return id_; }
    SceneFamily family() const override { return SceneFamily::Shader; }
    void init() override;
    void setParams(const SceneParams& params) override { params_ = params; }
    void resize(int width, int height) override;
    void update(const GeodeFeatureFrame& features, float dt) override;
    void draw(float timeSeconds) override;
    void release() override;

    void acceptPcm(const float* samples, int count) override;
    void setFlow(GLuint texture, float strength) override;
    void setPaletteLut(GLuint texture) override { paletteLutTex_ = texture; }
    void setTouchField(const TouchField* field) override { touchField_ = field; }
    void setFragmentSource(const std::string& source) override;

private:
    static constexpr float kTimeWrapSeconds = 7100.0f;
    static constexpr float kTwoPi = 6.2831853f;
    static constexpr float kPulsePhaseHz = 0.6f;
    static constexpr float kPulseDecayPerSecond = 3.0f;
    static constexpr float kAudioClamp = 1.5f;

    // ---- the smoothed motion layer -----------------------------------------
    //
    // A fragment style has no frame-to-frame state, so everything a style could
    // use to move SMOOTHLY has to be integrated here and handed over as a
    // uniform. The raw uBass/uMid/uTreble envelopes still ship unchanged; these
    // are the slew-limited companions a style reads when it wants the picture
    // to breathe rather than to jump.
    //
    // Rise is deliberately slower than fall is fast, and both are slow enough
    // that no single frame can move a value far: an 8 Hz one-pole covers about
    // 13% of the gap in one 60fps frame, so a band spiking 0 -> 1 in one frame
    // moves the smoothed value by 0.13, not by 1. That is what stops the flash.
    static constexpr float kBandRiseHz = 8.0f;
    static constexpr float kBandFallHz = 2.6f;
    // The overall swell is slower again: it is the "how loud is this passage"
    // signal, not the "what just happened" one.
    static constexpr float kSwellRiseHz = 1.6f;
    static constexpr float kSwellFallHz = 0.8f;

    // A spike is a transient loud enough to mean something, and it is only
    // allowed to mean something once per refractory window - otherwise a busy
    // drum line re-rolls the spawn every frame and the result is the strobing
    // the smoothing exists to remove.
    static constexpr float kSpikeThreshold = 0.35f;
    static constexpr float kSpikeRefractorySeconds = 0.28f;
    // The spike envelope has a RISE, not a step: a style keying brightness off
    // it cannot produce a one-frame flash because the value takes ~120ms to
    // arrive and ~600ms to leave.
    static constexpr float kSpikeRiseHz = 8.0f;
    static constexpr float kSpikeFallHz = 1.7f;

    // How fast the latched values travel to the plateau a spike chose. Low
    // enough that the change reads as a morph; the spike sets the destination,
    // never the picture.
    static constexpr float kFormGlideHz = 0.9f;
    static constexpr float kDirGlideHz = 1.1f;
    // The step a spike adds to the form phase. The golden-ratio conjugate
    // walks the 0..1 circle without ever repeating a plateau or landing near
    // the one before it, so consecutive spikes always pick a visibly different
    // fractal rather than dithering around one.
    static constexpr float kFormStep = 0.6180339f;
    // The widest a spike may turn the travel direction, in radians. Bounded
    // well under a half turn so the field never appears to reverse.
    static constexpr float kDirMaxTurn = 1.9f;
    // The travel phase always advances, so nothing a style scrolls can run
    // backwards; loudness only sets how fast.
    static constexpr float kFlowBaseHz = 0.08f;
    static constexpr float kFlowEnergyHz = 0.22f;
    // Well past the longest spawnGrow() horizon any style asks for.
    static constexpr float kSpawnAgeMax = 60.0f;

    // One-pole toward `target`, framerate independent, with its own rate for
    // rising and falling. Returns the new value.
    static float slew(float current, float target, float dt, float riseHz, float fallHz);
    // Advances the spike-latched state (spawn seed, form phase, travel
    // direction, travel phase) for one frame.
    void stepMotion(float hit, float dt);
    float nextSeed();

    void compilePendingIfAny();
    void uploadParams();
    void uploadMotion();
    void uploadTouch();
    void set1f(const char* name, float value);
    float marchSteps(float detail);

    std::string id_;
    std::string vertexSrc_;
    std::string currentFragment_;
    ProgramBinaryCache* cache_;
    SceneHost host_;

    GLuint program_ = 0;
    UniformCache uniforms_;
    GLuint vao_ = 0;
    Texture audioTex_;
    int width_ = 1;
    int height_ = 1;

    std::mutex pendingLock_;
    std::optional<std::string> pendingFragment_;
    bool pendingIsUserSource_ = false;

    std::array<float, kAudioTexWidth * 2> texData_{};
    float bass_ = 0.0f;
    float mid_ = 0.0f;
    float treble_ = 0.0f;
    float energy_ = 0.0f;
    float beatPulse_ = 0.0f;
    float pulsePhase_ = 0.0f;

    // The motion layer's integrated state. All of it survives from frame to
    // frame; none of it can be reconstructed inside the shader.
    float smoothBass_ = 0.0f;
    float smoothMid_ = 0.0f;
    float smoothTreble_ = 0.0f;
    float smoothEnergy_ = 0.0f;
    float swell_ = 0.0f;
    float spikeEnv_ = 0.0f;
    // A countdown, not a timestamp. Comparing against a wrapping clock would stop
    // detecting spikes altogether the first time the clock wrapped, roughly two
    // hours in - exactly the session length a wallpaper runs to.
    float spikeLockout_ = 0.0f;
    float spawnSeed_ = 0.0f;
    float spawnAge_ = 0.0f;
    float formPhase_ = 0.0f;
    float formTarget_ = 0.0f;
    float dirAngle_ = 0.0f;
    float dirTarget_ = 0.0f;
    float flowPhase_ = 0.0f;
    // Deterministic per scene instance: the same audio gives the same picture,
    // which is what makes an exported frame reproducible.
    unsigned int seedState_ = 0x9e3779b9u;
    SceneParams params_;
    float rotationAngle_ = 0.0f;
    float zoomPhase_ = 0.0f;
    float cyclePhase_ = 0.0f;
    float shaderTime_ = 0.0f;

    std::vector<float> pcm_;
    int pcmCount_ = 0;
    std::array<float, kAudioTexWidth> waveRow_{};

    GLuint flowTex_ = 0;
    float flowStrength_ = 0.0f;
    GLuint paletteLutTex_ = 0;
    const TouchField* touchField_ = nullptr;
    std::array<float, TouchField::kPointStride> touchAnchor_{};
    std::array<float, TouchField::kMaxPoints * TouchField::kPointStride> touchPoints_{};

    float stepsDetail_ = -1.0f;
    float stepsBudget_ = 0.0f;
};

}  // namespace geode::viz
