#pragma once
#include <GLES3/gl3.h>

#include <array>
#include <mutex>
#include <optional>
#include <string>

#include "viz/MotionField.hpp"
#include "viz/Program.hpp"
#include "viz/Reactions.hpp"
#include "viz/Scene.hpp"
#include "viz/Texture.hpp"

namespace geode::viz {

// Port of ShaderScene.kt: one fragment style on the fullscreen triangle, fed the shared uniform block.
class ShaderScene : public Scene {
public:
    // uSoundTex: row 0 spectrum, row 1 waveform, row 2 EQ, each resampled to this width (lib_sound).
    static constexpr int kSoundTexWidth = 128;
    static constexpr int kSoundTexRows = 3;
    // GEODE_SOUND_SCALARS rounded up to whole vec4s: uSound[kSoundVec4s].
    static constexpr int kSoundVec4s = (GEODE_SOUND_SCALARS + 3) / 4;

    ShaderScene(std::string id, std::string vertexSrc, std::string fragmentSrc, ProgramBinaryCache* cache, SceneHost host);
    ~ShaderScene() override;

    const std::string& id() const override { return id_; }
    SceneFamily family() const override { return SceneFamily::Shader; }
    void init() override;
    void setParams(const SceneParams& params) override { params_ = params; }
    void resize(int width, int height) override;
    void update(const GeodeSoundFrame& features, float dt) override;
    void draw(float timeSeconds) override;
    void release() override;

    void setFlow(GLuint texture, float strength) override;
    void setPaletteLut(GLuint texture) override { paletteLutTex_ = texture; }
    void setTouchField(const TouchField* field) override { touchField_ = field; }
    void setCamera(const CameraPose* pose) override { camera_ = pose; }
    bool drawsCamera() const override { return drawsCamera_; }
    void setFragmentSource(const std::string& source) override;

private:
    static constexpr float kTimeWrapSeconds = 7100.0f;
    static constexpr float kTwoPi = 6.2831853f;

    // ---- wave three: the continuous motion layer ----------------------------
    //
    // motionField_ is this scene's own instance (see viz/MotionField.hpp): it
    // is a pure function of the same GeodeSoundFrame/dt sequence update()
    // already receives, so every ShaderScene settles to the same continuous
    // uniforms without needing a channel back to Renderer's own instance
    // (which shapes SceneParams instead - see Renderer::resolveParams).

    void compilePendingIfAny();
    void uploadParams();
    void uploadMotion();
    void uploadTouch();
    void uploadCamera();
    void uploadSound();
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
    int width_ = 1;
    int height_ = 1;

    std::mutex pendingLock_;
    std::optional<std::string> pendingFragment_;
    bool pendingIsUserSource_ = false;

    // Wave three: the continuous replacement for the old spike-latched state
    // (uSpike/uMoveDir/uSpawnSeed/uSpawnAge/uFormPhase). See MotionField.hpp.
    MotionField motionField_;
    SceneParams params_;
    float rotationAngle_ = 0.0f;
    float zoomPhase_ = 0.0f;
    float cyclePhase_ = 0.0f;
    float shaderTime_ = 0.0f;

    GLuint flowTex_ = 0;
    float flowStrength_ = 0.0f;
    GLuint paletteLutTex_ = 0;
    const TouchField* touchField_ = nullptr;
    const CameraPose* camera_ = nullptr;
    // The frame update() was given (the renderer's, with this scene's band trims); uploaded as uSound
    // and uSoundTex in draw().
    GeodeSoundFrame sound_{};
    Texture soundTex_;
    std::array<float, kSoundVec4s * 4> soundScalars_{};
    std::array<float, kSoundTexWidth * kSoundTexRows> soundTexData_{};
    // True when the compiled program reads uCamBasis (lib_camera): the style
    // marches the rig's pose itself and the composite leaves it alone.
    bool drawsCamera_ = false;
    // This scene's own copy of the reaction sources, a pure function of the frames update() receives,
    // like motionField_: uploadSound reads its held events (uSoundEvents) and counts (uSoundCounts).
    ReactSignals reactSignals_;
    std::array<float, TouchField::kPointStride> touchAnchor_{};
    std::array<float, TouchField::kMaxPoints * TouchField::kPointStride> touchPoints_{};

    float stepsDetail_ = -1.0f;
    float stepsBudget_ = 0.0f;
};

}  // namespace geode::viz
