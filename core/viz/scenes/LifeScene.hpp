#pragma once
#include <GLES3/gl3.h>

#include <array>
#include <optional>
#include <string>

#include "viz/MotionField.hpp"
#include "viz/Program.hpp"
#include "viz/Scene.hpp"
#include "viz/fluid/FluidBuffers.hpp"
#include "viz/scenes/ProgramLoader.hpp"
#include "viz/scenes/SceneCommon.hpp"
#include "viz/scenes/StyleCatalog.hpp"

namespace geode::viz {

// Port of LifeScene.kt: Lenia and Gray-Scott cellular fields, reseeded when a census finds them dead.
class LifeScene : public Scene {
public:
    LifeScene(const styles::LifeStyle& style, ProgramLoader loader, SceneHost host)
        : id_(style.id), style_(style), loader_(loader), host_(std::move(host)) {}
    ~LifeScene() override { release(); }

    const std::string& id() const override { return id_; }
    SceneFamily family() const override { return SceneFamily::Fluid; }
    void init() override;
    void setParams(const SceneParams& params) override { params_ = params; }
    void resize(int width, int height) override;
    void update(const GeodeFeatureFrame& features, float dt) override;
    void draw(float timeSeconds) override;
    void release() override;
    void setTouchField(const TouchField* field) override { touch_ = field; }

private:
    static constexpr int kSimRes = 288;
    static constexpr float kSeedSeconds = 0.5f;
    static constexpr float kCensusSeconds = 4.0f;
    static constexpr float kStarved = 0.004f;
    static constexpr float kOvergrown = 0.985f;
    static constexpr float kEnvRisePerSec = 9.0f;
    static constexpr float kEnvFallPerSec = 2.4f;

    fluid::DoubleFbo* ensureState();
    void census(fluid::DoubleFbo& field, GLint restoreFbo);

    std::string id_;
    styles::LifeStyle style_;
    ProgramLoader loader_;
    SceneHost host_;
    SceneParams params_;
    bool hasPending_ = false;
    GeodeFeatureFrame pending_{};
    int width_ = 1;
    int height_ = 1;
    float time_ = 0.0f;
    float lastDt_ = 1.0f / 60.0f;
    GLuint stepProgram_ = 0;
    GLuint showProgram_ = 0;
    UniformCache stepLocs_{0};
    UniformCache showLocs_{0};
    bool programOk_ = false;
    GLuint vao_ = 0;
    std::optional<fluid::Formats> formats_;
    std::optional<fluid::DoubleFbo> state_;
    bool byteState_ = false;
    // Wave three: this scene's own continuous motion state, stepped every
    // update() from the real frame - see viz/MotionField.hpp.
    MotionField motionField_;
    float envTreble_ = 0.0f;
    float seedRemain_ = 0.0f;
    float seedStrength_ = 0.0f;
    float seedX_ = 0.5f;
    float seedY_ = 0.5f;
    float censusAge_ = 0.0f;
    const TouchField* touch_ = nullptr;
};

}  // namespace geode::viz
