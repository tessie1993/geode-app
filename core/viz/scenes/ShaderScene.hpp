#pragma once
#include <GLES3/gl3.h>

#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "viz/MotionField.hpp"
#include "viz/Program.hpp"
#include "viz/Scene.hpp"
#include "viz/SoundUniforms.hpp"
#include "viz/Texture.hpp"
#include "viz/pool/PoolMath.hpp"
#include "viz/world/MorphDriver.hpp"

namespace geode::viz {

// One fragment style on the fullscreen triangle, fed the shared uniform block.
class ShaderScene : public Scene {
public:
    ShaderScene(std::string id, std::string vertexSrc, std::string fragmentSrc, ProgramBinaryCache* cache, SceneHost host);
    ~ShaderScene() override;

    const std::string& id() const override { return id_; }
    SceneFamily family() const override { return SceneFamily::Shader; }
    void init() override;
    // The style's program, which draw() would otherwise compile on the first frame.
    void prepare() override { compilePendingIfAny(); }
    bool buildFailed() const override { return program_ == 0; }
    void setParams(const SceneParams& params) override { params_ = params; }
    void resize(int width, int height) override;
    void update(const GeodeSoundFrame& features, float dt) override;
    void draw(float timeSeconds) override;
    void release() override;

    void setPaletteLut(GLuint texture) override { paletteLutTex_ = texture; }
    void setTouchField(const TouchField* field) override { touchField_ = field; }
    void setCamera(const CameraPose* pose) override { camera_ = pose; }
    bool drawsCamera() const override { return drawsCamera_; }
    void setFragmentSource(const std::string& source) override;
    // Morph World's baked assets (WorldAssets: world_shapes.bin, world_textures.bin), uploaded on the
    // GL thread at the first draw. The shapes also go to the CPU port, process-wide.
    void setWorldAssets(const std::string& shapes, const std::string& maps);

    // A simulation's field, for a ShaderScene that draws a simulation's present (core/viz/present/FieldPresent)
    // rather than a style: the field texture as uField on unit 4 (units 1..3 are the flow, the palette LUT and
    // the sound), its size in texels, and the numbers lib_field3d reads beside the shared block. The present
    // keeps it alive while this scene draws; null (the default) uploads nothing.
    static constexpr int kFieldLooks = 5;
    struct Field {
        GLuint texture = 0;
        std::array<float, 2> size{};
        std::array<float, 4> geom{};
        std::array<float, 4> shape{};
        std::array<float, 4 * kFieldLooks> look{};
    };
    void setField(const Field* field) { field_ = field; }

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
    void uploadWorld();
    void uploadPool();
    void uploadField();
    pool::Controls poolControls() const;
    void specializeWorld();
    void set1f(const char* name, float value);
    float marchSteps(float detail);

    std::string id_;
    std::string vertexSrc_;
    std::string currentFragment_;
    // The built-in source, so clearing a user's shader override (setFragmentSource("")) draws it again.
    std::string stockFragment_;
    ProgramBinaryCache* cache_;
    SceneHost host_;

    GLuint program_ = 0;
    UniformCache uniforms_;
    GLuint vao_ = 0;
    int width_ = 1;
    int height_ = 1;

    std::mutex pendingLock_;
    std::optional<std::string> pendingFragment_;

    // Wave three: the continuous replacement for the old spike-latched state
    // (uMoveDir/uSpawnSeed/uSpawnAge/uFormPhase). See MotionField.hpp.
    MotionField motionField_;
    SceneParams params_;
    float rotationAngle_ = 0.0f;
    float cyclePhase_ = 0.0f;
    float shaderTime_ = 0.0f;
    // The sound engine's bass, mid and treble clocks at Speed's rate (uSoundTimeSped,
    // viz/SpeedClock.hpp), beside uTime and the motion field's uFlowTime: integrated,
    // never clock * uSpeed, so moving Speed changes how fast they run, not where they are.
    std::array<speedclock::Follower, 3> soundTimeSped_{};

    GLuint paletteLutTex_ = 0;
    const TouchField* touchField_ = nullptr;
    const CameraPose* camera_ = nullptr;
    // The frame update() was given (the renderer's, with this scene's band trims) and this scene's own
    // held events from it, a pure function of those frames like motionField_; uploaded in draw().
    SoundUniforms sound_;
    // True when the compiled program reads uCamBasis (lib_camera): the style
    // marches the rig's pose itself and the composite leaves it alone.
    bool drawsCamera_ = false;
    std::array<float, TouchField::kPointStride> touchAnchor_{};
    std::array<float, TouchField::kMaxPoints * TouchField::kPointStride> touchPoints_{};

    // Morph World (shaders/morph_world_frag, lib_world): the World Genome's driver, built only for that
    // style. Stepped in update() from the sound and the controls, moved along by the camera in draw().
    std::unique_ptr<world::MorphDriver> world_;
    float worldDt_ = 0.0f;
    std::string worldShapes_;
    std::string worldMaps_;
    GLuint worldShapesTex_ = 0;
    GLuint worldMapsTex_ = 0;
    bool worldSeeded_ = false;
    // The lib_world parts (MorphDriver::parts) the program in program_ was built with; 0 while it is
    // the stock source, which has every part.
    uint32_t worldParts_ = 0;

    // Pool (shaders/pool_frag): the surface's waves, rings and wakes, built only for that style.
    std::unique_ptr<pool::Pool> pool_;
    const Field* field_ = nullptr;

    float stepsDetail_ = -1.0f;
    float stepsBudget_ = 0.0f;
};

}  // namespace geode::viz
