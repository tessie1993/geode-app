#pragma once
#include <GLES3/gl3.h>

#include <memory>
#include <string>

#include "viz/GlProfile.hpp"
#include "viz/Program.hpp"
#include "viz/Scene.hpp"
#include "viz/SoundUniforms.hpp"
#include "viz/compute/SimPass.hpp"
#include "viz/scenes/ProgramLoader.hpp"
#include "viz/scenes/SceneCommon.hpp"
#include "viz/scenes/StyleCatalog.hpp"

namespace geode::viz {

// Port of SilkScene.kt: an advected strand field stepped by the compute layer and shown by silk_show.
//
// The sound engine plays it like an instrument. update() integrates the rich frame into a handful of
// forces and phases on the CPU (dt-scaled, clamped, clocks only ever forward), and the step reads those
// as plain uniforms, so the compute path needs no extra texture: kick, snare and hat are three separate
// forces, the melody is an emitter that moves with the pitch, counted kicks re-seat the strokes and step
// the fold and zoom, and a hard cut swaps the style to its alternate field and material. The present
// pass reads the whole frame through SoundUniforms (lib_sound) and lights the dye as a relief (lib_relief).
class SilkScene : public Scene {
public:
    SilkScene(const styles::SilkStyle& style, ProgramLoader loader, const GlProfile* profile, SceneHost host)
        : id_(style.id), style_(style), loader_(loader), profile_(profile), host_(std::move(host)) {}
    ~SilkScene() override { release(); }

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
    static constexpr int kSimRes = 320;
    static constexpr float kByteStateScale = 8.0f;
    static constexpr float kEnvRisePerSec = 8.0f;
    static constexpr float kEnvFallPerSec = 2.2f;
    static constexpr float kRingSpeed = 2.6f;
    static constexpr float kRingMax = 3.4f;
    // Strokes re-seat on every fourth kick; with no kicks they still re-seat this often.
    static constexpr float kSeedFallbackSeconds = 12.0f;
    // The seed epoch wraps here: small enough that the stroke hash keeps full precision on highp.
    static constexpr float kCountWrap = 256.0f;
    static constexpr int kSoundTexUnit = 3;

    void applySimSize();
    void bindStep(sim::SimUniforms& u);
    void stepSound(const GeodeSoundFrame& f, float dt);

    std::string id_;
    styles::SilkStyle style_;
    ProgramLoader loader_;
    const GlProfile* profile_;
    SceneHost host_;
    SceneParams params_;
    int width_ = 1;
    int height_ = 1;
    std::unique_ptr<sim::SimPass> sim_;
    GLuint showProgram_ = 0;
    UniformCache showLocs_{0};
    bool programOk_ = false;
    GLuint vao_ = 0;
    SoundUniforms sound_;
    PcmPulse pcmPulse_;
    float pcmStrike_ = 0.0f;
    // Band lanes and the three drum forces.
    float envBass_ = 0.0f;
    float envMid_ = 0.0f;
    float envTreble_ = 0.0f;
    float kick_ = 0.0f;
    float snare_ = 0.0f;
    float hat_ = 0.0f;
    float snareSign_ = 1.0f;
    // The ring a kick (or a hard cut, or sound returning) throws out, from a centre the kick picks.
    float ringRadius_ = -1.0f;
    float ringCenterX_ = 0.5f;
    float ringCenterY_ = 0.5f;
    float ringTint_[3] = {0.0f, 0.0f, 0.0f};
    // Last values of the sound clocks, for their per-frame advance.
    float lastBassTime_ = 0.0f;
    float lastMidTime_ = 0.0f;
    float lastCurvedTime_ = 0.0f;
    float lastNoteTime_ = 0.0f;
    float lastHitTime_ = 0.0f;
    // Phases, each only ever moving forward.
    float slabTurn_ = 0.0f;
    float foldPhase_ = 0.0f;
    float drift_ = 0.0f;
    float bPhase_ = 0.0f;
    float melodyPhase_ = 0.0f;
    // Slewed shapes of the music.
    float flowRate_ = 0.3f;
    float surge_ = 0.0f;
    float hush_ = 0.0f;
    float flush_ = 0.0f;
    float trend_ = 0.5f;
    float writhe_ = 0.0f;
    float windX_ = 0.0f;
    float bJump_ = 0.0f;
    float bJumpTarget_ = 0.0f;
    float scale_ = 1.0f;
    // The melody emitter: position (uv), brightness, signed swirl, and the pitch-class hue.
    float emitX_ = 0.5f;
    float emitY_ = 0.4f;
    float emit_ = 0.0f;
    float emitSwirl_ = 0.0f;
    float swirlSign_ = 1.0f;
    float melodyHue_ = 0.0f;
    float melodyAmt_ = 0.0f;
    // Counted changes.
    float seedEpoch_ = 0.0f;
    float sinceSeed_ = 0.0f;
    int kick16Count_ = 0;
    // Uniform values for the step, computed in update().
    float stepB_ = 0.0f;
    float stepAdvect_ = 0.0f;
    float stepDecay_ = 0.0f;
    float stepDrive_ = 0.0f;
    float exposure_ = 1.0f;
    float depth_ = 0.05f;
    const TouchField* touch_ = nullptr;
};

}  // namespace geode::viz
