#pragma once
#include <GLES3/gl3.h>

#include <array>
#include <optional>
#include <string>

#include "viz/Program.hpp"
#include "viz/Reactions.hpp"
#include "viz/Scene.hpp"
#include "viz/SoundUniforms.hpp"
#include "viz/fluid/FluidBuffers.hpp"
#include "viz/scenes/ProgramLoader.hpp"
#include "viz/scenes/SceneCommon.hpp"
#include "viz/scenes/StyleCatalog.hpp"

namespace geode::viz {

// Port of AcidScene.kt: a video-feedback loop on an RGBA8 ping-pong.
//
// The scene is a conductor: update() reads the sound engine's whole frame (drums, sections, melody,
// energy, stereo, clocks) and turns it into per-frame moves of the loop (zoom punch, spin, vortex,
// vanishing point, mirror, hue steps, recipe switches), which the step pass then only applies. The
// show pass reads the loop as a lit relief (lib_relief) through SoundUniforms.
class AcidScene : public Scene {
public:
    AcidScene(const styles::AcidStyle& style, ProgramLoader loader, SceneHost host)
        : id_(style.id), style_(style), loader_(loader), host_(std::move(host)) {}
    ~AcidScene() override { release(); }

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
    static constexpr int kSimRes = 540;
    static constexpr float kFeedbackCap = 0.975f;
    static constexpr float kGlitchThreshold = 0.32f;
    static constexpr float kGlitchDecay = 2.4f;
    static constexpr float kEnvRisePerSec = 9.0f;
    static constexpr float kEnvFallPerSec = 2.4f;
    static constexpr int kSpokes = 12;

    // Per-recipe tables (indexed by AcidStyle::mode, 0..9): the recipe a hard cut switches to, the
    // two relief looks and the relief depth.
    static int altMode(int mode);
    static int lookBase(int mode);
    static int lookAlt(int mode);
    static float reliefDepth(int mode);

    fluid::DoubleFbo* ensureState();
    void fillSpokes(const float* bands);
    // Steps every musical state below from one frame (called once per frame from update, so no
    // one-frame trigger is lost when two updates land before a draw).
    void conduct(const GeodeSoundFrame& f, float dt);

    std::string id_;
    styles::AcidStyle style_;
    ProgramLoader loader_;
    SceneHost host_;
    SceneParams params_;
    GeodeSoundFrame last_{};
    int width_ = 1;
    int height_ = 1;
    float lastDt_ = 1.0f / 60.0f;
    UniformCache step_{0};
    UniformCache show_{0};
    bool programOk_ = false;
    GLuint vao_ = 0;
    std::optional<fluid::DoubleFbo> state_;
    SoundUniforms sound_;
    PcmPulse pcmPulse_;
    float pcmStrike_ = 0.0f;
    float envBass_ = 0.0f;
    float envMid_ = 0.0f;
    float envTreble_ = 0.0f;
    float beatPulse_ = 0.0f;
    float glitch_ = 0.0f;
    float glitchEpoch_ = 0.0f;
    const TouchField* touch_ = nullptr;
    std::array<float, kSpokes> spokes_{};

    // ---- the conductor's state ----
    float clock_ = 0.0f;          // sound clock: only ever advances, faster when the music is loud
    float spinRate_ = 0.0f;       // eased rate of the curved clock, 0..3 (1 = typical loudness)
    float lastCurved_ = -1.0f;    // last frame's curvedTime, for its rate (-1 = not primed)
    float twistDir_ = 1.0f;       // eased -1..1, follows the kick toggle
    float lunge4_ = 0.0f;         // every 4th kick: a zoom lunge, decays
    float lunge8_ = 0.0f;         // every 8th kick: a spin surge, decays
    int kick16Count_ = 0;         // every 16th kick: the source recipe swaps
    float centreX_ = 0.0f;        // eased vanishing point, q units
    float centreY_ = 0.0f;
    float hue_ = 0.0f;            // eased hue offset (note steps, section jumps), turns
    float hueTarget_ = 0.0f;
    float calm_ = 0.0f;           // 0 playing .. 1 silent or just returned: the echo settles
    float feedbackTrim_ = 0.0f;   // eased echo-length trim from the energy trend
    float drift_ = 0.0f;          // eased stereo pan, -1..1
    float wobble_ = 0.0f;         // eased glide + vibrato, 0..1
    float melodyAmt_ = 0.0f;      // eased pitch confidence, drawn as the melody dot
};

}  // namespace geode::viz
