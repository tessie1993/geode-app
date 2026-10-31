#pragma once
#include <GLES3/gl3.h>

#include <array>
#include <optional>
#include <string>

#include "viz/Program.hpp"
#include "viz/Scene.hpp"
#include "viz/SoundUniforms.hpp"
#include "viz/fluid/FluidBuffers.hpp"
#include "viz/scenes/ProgramLoader.hpp"
#include "viz/scenes/SceneCommon.hpp"
#include "viz/scenes/StyleCatalog.hpp"

namespace geode::viz {

// Port of LifeScene.kt: Lenia and Gray-Scott cellular fields, reseeded when a census finds them dead.
//
// The sound engine plays the organism, it does not flash it. Every reading is folded in update() (so a
// one-frame trigger is never lost between draws) into a few quantities the solver understands: matter
// to inject (kick envelope births, melody buds, hat spores), a rule that breathes (snare and surge widen
// the growth window, kick16 and hard cuts morph to another of the style's four rule presets), a time
// rate (quiet music freezes the colonies into still forms, busy music sets them squirming) and a drift
// on the curved sound clock, which only ever moves forward. The present pass reads the field as a lit
// relief (lib_relief) with two materials picked by cell age, swapped on each hard cut.
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
    void update(const GeodeSoundFrame& features, float dt) override;
    void draw(float timeSeconds) override;
    void release() override;
    void acceptPcm(const float* samples, int count) override { pcmPulse_.accept(samples, count); }
    void setTouchField(const TouchField* field) override { touch_ = field; }

private:
    static constexpr int kSimRes = 288;
    static constexpr int kPresets = 4;
    static constexpr float kSeedSeconds = 0.5f;
    static constexpr float kGoldenAngle = 2.399963f;
    static constexpr float kCensusSeconds = 4.0f;
    static constexpr float kStarved = 0.004f;
    static constexpr float kOvergrown = 0.985f;
    // Solver rate in silence: low enough that colonies read as still forms, not zero so they never die of it.
    static constexpr float kStillRate = 0.1f;
    static constexpr float kRateSeconds = 0.8f;
    // A rule preset change morphs over this time constant, so patterns transform instead of snapping.
    static constexpr float kMorphSeconds = 1.6f;
    // Drift in sim texels per unit of the curved clock (~1 per second of typical music).
    static constexpr float kDriftTexelsPerClock = 4.0f;

    // The rule the solver runs: Lenia (mu, sigma) or Gray-Scott (feed, kill).
    struct Rule {
        float mu;
        float sigma;
        float feed;
        float kill;
    };

    fluid::DoubleFbo* ensureState();
    void census(fluid::DoubleFbo& field, GLint restoreFbo);
    Rule preset(int index) const;
    void steer(const GeodeSoundFrame& f, float dt);

    std::string id_;
    styles::LifeStyle style_;
    ProgramLoader loader_;
    SceneHost host_;
    SceneParams params_;
    SoundUniforms sound_;
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
    PcmPulse pcmPulse_;
    float pcmStrike_ = 0.0f;
    float seedRemain_ = 0.0f;
    float seedSalt_ = 0.0f;
    float censusAge_ = 0.0f;
    const TouchField* touch_ = nullptr;

    // Steering, folded from the sound frame in update() and consumed by draw().
    float rate_ = kStillRate;
    float kickFeed_ = 0.0f;
    float kickAngle_ = 0.0f;
    float kickX_ = 0.5f;
    float kickY_ = 0.5f;
    float kickSize_ = 1.0f;
    float budFeed_ = 0.0f;
    float budX_ = 0.5f;
    float budY_ = 0.5f;
    float ink_ = 0.5f;
    float sprinkle_ = 0.0f;
    Rule rule_{};
    Rule live_{};
    int preset_ = 0;
    float heading_ = 0.0f;
    float headingTarget_ = 0.0f;
    float driftX_ = 0.0f;
    float driftY_ = 0.0f;
    float lastCurved_ = 0.0f;
    bool clockPrimed_ = false;
};

}  // namespace geode::viz
