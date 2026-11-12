#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "api/geode_api.h"
#include "viz/Config.hpp"
#include "viz/Params.hpp"

namespace geode::viz {

enum class LfoWave { Sine, Triangle, Saw, Square, Random,
                      /* Appended: every cycle picks a new random value and glides to it. */ RandomGlide };
enum class ModSource { Lfo, Bass, Mid, Treble, Level, Brightness, Transient, StereoWidth, StereoPan };
enum class ModPolarity { Bipolar, Positive, Negative };
enum class ModCurve { Linear, Exponential, Logarithmic, Smooth };
enum class ModChainField { Rate, Depth };

enum class LfoTarget {
    None, Speed, Zoom, Rotation, Sway, Retired5 /* was Pulse */, DriftX, DriftY, Warp, Ripple, Morph, Twist, Tile, Pixelate, Posterize,
    ColorShift, PaletteMix, Saturation, Brightness, Intensity, Bloom, Temperature, Turbulence, ChromaAb, Vignette,
    Retired25 /* was Glitch: the slot keeps every later ordinal */, Fisheye, ParticleSize, TrailLength, FluidCurl, FluidRadius, FluidForce, FluidGlow, FluidFade,
    FluidCatchPull, FluidCatchRadius, FlowStrength,
    Retired37 /* was WaterRipple */, Retired38 /* was RippleOverlay */,
    Lfo1Rate, Lfo1Depth, Lfo2Rate, Lfo2Depth, Lfo3Rate, Lfo3Depth,
    // Appended: the camera rig's offsets (viz/CameraRig). Ordinals above are wire values.
    CamFly, CamOrbit, CamPush, CamYaw, CamPitch, CamRoll, CamFov, CamShake,
    // Appended: Liquid mode's Swirl, Push and Dive (viz/liquid), after the camera rig's.
    FlowCurl, FlowForce, EndlessZoomSpeed,
    // Appended: the dive's spiral and where it points (viz/EndlessZoom.hpp).
    DiveTwist, DiveAimX, DiveAimY,
    // Appended: a macro knob (viz/MacroBank), the one LfoConfig::knob / AdsrConfig::knob names. It writes no param:
    // the resolver adds the wave or the envelope to the knob's turn, and the knob moves what it links.
    Macro,
    // Appended: the palette stack's amounts (Palette 1, 3..8; Palette 2's is PaletteMix's once the renderer draws
    // the stack) and where along it the colours sit (viz/Palette.hpp).
    PaletteAmount1, PaletteAmount3, PaletteAmount4, PaletteAmount5, PaletteAmount6, PaletteAmount7, PaletteAmount8,
    PaletteShift,
};
// The last ordinal: the wire's range for a target.
inline constexpr LfoTarget kLastLfoTarget = LfoTarget::PaletteShift;

struct ModChain {
    int slot;
    ModChainField field;
};

// The chained targets steer another slot; everything else writes a scene parameter.
bool chainOf(LfoTarget target, ModChain& out);
bool isLuminanceTarget(LfoTarget target);

// One slot as geode_viz_set_lfo hands it over. Defaults and the period's range are the lfo rows of
// viz/ConfigTable.inc (#423), the same numbers the stores and the sliders read.
struct LfoConfig {
    static constexpr float kDefaultRateSeconds = config::spec(config::Slot::lfo_rateSeconds).defaultValue;
    static constexpr float kMinRateSeconds = config::spec(config::Slot::lfo_rateSeconds).min;
    static constexpr float kMaxRateSeconds = config::spec(config::Slot::lfo_rateSeconds).max;

    bool enabled = false;
    ModSource source = ModSource::Lfo;
    LfoTarget target = LfoTarget::None;
    LfoWave wave = LfoWave::Sine;
    float rateSeconds = kDefaultRateSeconds;
    float depth = config::spec(config::Slot::lfo_depth).defaultValue;
    ModPolarity polarity = ModPolarity::Bipolar;
    ModCurve curve = ModCurve::Linear;
    /* 0 = free (rateSeconds). 1, 2, 4, 8, 16 or 32 = one cycle per that many beats of the music clock. */
    float beats = 0.0f;
    /* The macro knob's id a Macro target turns; 0 is none. */
    int knob = 0;
    /* -1 runs free; a Moment ordinal (viz/Reactions) restarts the cycle each time that moment comes, on the slot's
     * own period (beat timing gives way to it). */
    int moment = -1;
};

float polarized(float raw, ModPolarity polarity);
float shape(float value, ModCurve curve);
SceneParams applyLfoTarget(const SceneParams& r, LfoTarget target, float v);
// The wire slot of the field a target writes; -1 for None, the retired slot and the chained targets.
int paramSlotOf(LfoTarget target);

class LfoEngine {
public:
    static constexpr int kSlots = 3;
    // The depth an oscillator runs at after an envelope or an earlier slot adds to it. Wider than the
    // control's 0..1 (the lfo_depth row) on purpose: chained modulation may push a full slot past it.
    static constexpr float kMaxModulatedDepth = 2.0f;

    std::array<LfoConfig, kSlots> configs{};
    // What the two random waves draw from: a slot's value in a cycle is a hash of this, the slot and the
    // cycle's number, so it is the same at any frame rate and differs per slot.
    std::uint32_t seed = 0x9E3779B9u;

    // `moments` is the bit per Moment that came this frame (MomentWatch).
    const std::array<float, kSlots>& tick(float dt, const GeodeSoundFrame& sound, const float* extRateAdd = nullptr,
                                          const float* extDepthAdd = nullptr, std::uint32_t moments = 0);
    SceneParams apply(const SceneParams& p, const std::array<float, kSlots>& values) const;

private:
    static constexpr float kTau = 6.2831853f;
    static constexpr float kChainRateHz = 4.0f;
    static constexpr float kMinRateHz = 0.01f;
    static constexpr float kMaxRateHz = 30.0f;
    static constexpr float kFollowRiseSeconds = 0.02f;
    static constexpr float kFollowFallSeconds = 0.16f;

    float oscillator(int i, const LfoConfig& c, float dt, const GeodeSoundFrame& sound);
    /* The cycle position (0..1) a beat-timed slot sits at on the measured tempo. */
    float syncedPhase(int i, const LfoConfig& c, float dt, const GeodeSoundFrame& sound);
    /* The random waves' value for one cycle of a slot, -1..1. */
    float held(int i, std::uint32_t cycle) const;
    float follow(int i, float value, float dt);
    float followBipolar(int i, float value, float dt);

    std::array<float, kSlots> phases_{};
    // Whole cycles since the slot started: forward on a wrap, back when the music clock steps back over one.
    std::array<std::uint32_t, kSlots> cycle_{};
    std::array<float, kSlots> followed_{};
    std::array<float, kSlots> out_{};
    std::array<float, kSlots> rateAdd_{};
    std::array<float, kSlots> depthAdd_{};
    // Beat timing: the free clock's phase (cycles), the last measured tempo (0 until the music gives one;
    // a beat-timed slot runs on its own period till then), whether the music clock held last frame, and
    // the offset still being eased out after it came back.
    std::array<float, kSlots> syncFree_{};
    std::array<float, kSlots> syncJump_{};
    std::array<bool, kSlots> syncLocked_{};
    std::array<float, kSlots> prevPhase_{};
    float lastBpm_ = 0.0f;
};

}  // namespace geode::viz
