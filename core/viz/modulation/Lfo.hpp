#pragma once
#include <array>
#include <cstdint>

#include "api/geode_api.h"
#include "viz/params/Config.hpp"
#include "viz/modulation/ModTable.hpp"
#include "viz/params/Params.hpp"

namespace geode::viz {

enum class LfoWave { Sine, Triangle, Saw, Square, Random,
                      /* Appended: every cycle picks a new random value and glides to it. */ RandomGlide };
enum class ModSource { Lfo, Bass, Mid, Treble, Level, Brightness, Transient, StereoWidth, StereoPan };
enum class ModPolarity { Bipolar, Positive, Negative };
enum class ModCurve { Linear, Exponential, Logarithmic, Smooth };
enum class ModChainField { Rate, Depth };

enum class LfoTarget {
    None, Speed, Zoom, Rotation, Sway, Retired5 /* was Pulse */, DriftX, DriftY, Warp, Ripple, Morph, Twist, Tile, Pixelate, Posterize,
    ColorShift, PaletteMix /* was Palette blend: writes Palette 2's amount */, Saturation, Brightness, Intensity, Bloom, Temperature, Turbulence, ChromaAb, Vignette,
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
    // Appended: a macro knob (viz/modulation/MacroBank), the one LfoConfig::knob / AdsrConfig::knob names. It writes no param:
    // the resolver adds the wave or the envelope to the knob's turn, and the knob moves what it links.
    Macro,
    // Appended: the palette stack's amounts (Palette 1, 3..8; Palette 2's is PaletteMix's) and where along it
    // the colours sit (viz/colour/Palette.hpp).
    PaletteAmount1, PaletteAmount3, PaletteAmount4, PaletteAmount5, PaletteAmount6, PaletteAmount7, PaletteAmount8,
    PaletteShift,
    // Appended: the kaleidoscope's folds (1 off .. 16), the one slider that replaced its switch and count.
    KaleidoFolds,
    // Appended: Customize › Layers' mix (the layerMix param, added to the Layers page's mix; viz/LayerState).
    LayerMix,
    // Appended: the physics layers (viz/physics): the fluid's mix, the particles' mix, the force that stirs them
    // and where the fluid sits (the simFluid, simParticles, simForce and simLayerOrder params).
    SimFluid, SimParticles, SimForce, SimLayerOrder,
    // Appended: Grow and layers (viz/params/ParamTable.inc), drawn by the family libraries (ordinals 74..77).
    Spawn, Fractal, FrontLayer, BackLayer,
    // Appended: which of a style's own distance formulas draws it (viz/params/ParamTable.inc formula), ordinal 78.
    Formula,
    // Appended: how far a raymarched style takes in the shape graph (viz/params/ParamTable.inc shapes), ordinal 79.
    Shapes,
    // Appended: the soft shadows' darkness and the escape colour's reach (viz/params/ParamTable.inc shadow, escapeColor),
    // ordinals 80 and 81.
    Shadow, EscapeColor,
    // Appended: how much the picture is born and dies (viz/params/ParamTable.inc life, viz/Births), ordinal 82.
    Life,
};
// The names above are the targets as first shipped (viz/modulation/ModTable.hpp kLegacy). Every later ordinal is a field with a mod
// row in viz/params/ParamTable.inc, in slot order, with no name here: a target is data, not an enumerator.
static_assert(static_cast<int>(LfoTarget::Life) == modtable::kLegacyCount - 1, "the named targets are the legacy table's");
// The last ordinal: the wire's range for a target.
inline constexpr LfoTarget kLastLfoTarget = static_cast<LfoTarget>(modtable::kCount - 1);

struct ModChain {
    int slot;
    ModChainField field;
};

// How far one unit of a chained target (a wave's speed) moves the slot it steers, in Hz; waves and envelopes alike.
inline constexpr float kChainRateHz = 4.0f;

// The chained targets steer another slot; everything else writes a scene parameter.
bool chainOf(LfoTarget target, ModChain& out);
// A target whose field's mod row has kModLight.
bool isLuminanceTarget(LfoTarget target);

// One slot. Defaults and the period's range are the lfo rows of
// viz/params/ConfigTable.inc (#423), the same numbers the stores and the sliders read.
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
    /* -1 runs free; a Moment ordinal (viz/modulation/Reactions) restarts the cycle each time that moment comes, on the slot's
     * own period (beat timing gives way to it). */
    int moment = -1;
};

float polarized(float raw, ModPolarity polarity);
float shape(float value, ModCurve curve);
// Moves `r` itself: the per-frame chains (waves, envelopes, routes) use it so no frame copies the whole set. The field
// moves by v times its mod row's scale, held to its range (a Circular field adds unclamped).
void applyLfoTargetInPlace(SceneParams& r, LfoTarget target, float v);
// The wire slot of the field a target writes; -1 for None, the retired slots, Macro and the chained targets.
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
    // Adds each enabled slot's value to its target's field in `p`, in place.
    void apply(SceneParams& p, const std::array<float, kSlots>& values) const;

private:
    static constexpr float kTau = 6.2831853f;
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
    // Beat timing: the free clock's phase (cycles), the tempo the music gives now (0 while it gives none;
    // a beat-timed slot runs on its own period then), whether the music clock held last frame, and
    // the offset still being eased out after it came back.
    std::array<float, kSlots> syncFree_{};
    std::array<float, kSlots> syncJump_{};
    std::array<bool, kSlots> syncLocked_{};
    std::array<float, kSlots> prevPhase_{};
    float lastBpm_ = 0.0f;
};

}  // namespace geode::viz
