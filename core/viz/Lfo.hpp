#pragma once
#include <array>
#include <random>
#include <vector>

#include "api/geode_api.h"
#include "viz/Params.hpp"

namespace geode::viz {

enum class LfoWave { Sine, Triangle, Saw, Square, Random };
enum class ModSource { Lfo, Bass, Mid, Treble, Level, Brightness, Transient, StereoWidth, StereoPan };
enum class ModPolarity { Bipolar, Positive, Negative };
enum class ModCurve { Linear, Exponential, Logarithmic, Smooth };
enum class ModChainField { Rate, Depth };

enum class LfoTarget {
    None, Speed, Zoom, Rotation, Sway, Pulse, DriftX, DriftY, Warp, Ripple, Morph, Twist, Tile, Pixelate, Posterize,
    ColorShift, PaletteMix, Saturation, Brightness, Intensity, Bloom, Temperature, Turbulence, ChromaAb, Vignette,
    Glitch, Fisheye, ParticleSize, TrailLength, FluidCurl, FluidRadius, FluidForce, FluidGlow, FluidFade,
    FluidCatchPull, FluidCatchRadius, FlowStrength, WaterRipple, RippleOverlay,
    Lfo1Rate, Lfo1Depth, Lfo2Rate, Lfo2Depth, Lfo3Rate, Lfo3Depth,
};

struct ModChain {
    int slot;
    ModChainField field;
};

// The chained targets steer another slot; everything else writes a scene parameter.
bool chainOf(LfoTarget target, ModChain& out);
bool isLuminanceTarget(LfoTarget target);
ModPolarity naturalPolarity(ModSource source);

struct LfoConfig {
    static constexpr float kDefaultRateSeconds = 2.0f;
    static constexpr float kMinRateSeconds = 0.05f;
    static constexpr float kMaxRateSeconds = 60.0f;

    bool enabled = false;
    ModSource source = ModSource::Lfo;
    LfoTarget target = LfoTarget::None;
    LfoWave wave = LfoWave::Sine;
    float rateSeconds = kDefaultRateSeconds;
    float depth = 0.3f;
    ModPolarity polarity = ModPolarity::Bipolar;
    ModCurve curve = ModCurve::Linear;
};

float polarized(float raw, ModPolarity polarity);
float shape(float value, ModCurve curve);
SceneParams applyLfoTarget(const SceneParams& r, LfoTarget target, float v);

class LfoEngine {
public:
    static constexpr int kSlots = 3;
    static constexpr float kShPhaseWrap = 64.0f;

    std::array<LfoConfig, kSlots> configs{};

    const std::array<float, kSlots>& tick(float dt, const GeodeFeatureFrame& features, const float* extRateAdd = nullptr,
                                          const float* extDepthAdd = nullptr);
    SceneParams apply(const SceneParams& p, const std::array<float, kSlots>& values) const;

private:
    static constexpr float kTau = 6.2831853f;
    static constexpr float kChainRateHz = 4.0f;
    static constexpr float kMinRateHz = 0.01f;
    static constexpr float kMaxRateHz = 30.0f;
    static constexpr float kFollowRiseSeconds = 0.02f;
    static constexpr float kFollowFallSeconds = 0.16f;

    float oscillator(int i, const LfoConfig& c, float dt);
    float follow(int i, float value, float dt);
    float followBipolar(int i, float value, float dt);

    std::array<float, kSlots> phases_{};
    std::array<float, kSlots> sampleHold_{};
    std::array<float, kSlots> totalPhase_{};
    std::array<int, kSlots> lastCycle_{-1, -1, -1};
    std::array<float, kSlots> followed_{};
    std::array<float, kSlots> out_{};
    std::array<float, kSlots> rateAdd_{};
    std::array<float, kSlots> depthAdd_{};
    std::minstd_rand rng_{12345};
};

}  // namespace geode::viz
