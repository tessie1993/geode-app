#pragma once
#include <string>

#include "viz/Lfo.hpp"
#include "viz/Params.hpp"

namespace geode::viz {

enum class TransitionStyle { Cut, Fade, Melt, Slide, Zoom };
enum class BlendMode { Normal, Screen, Add, Multiply, Difference, Overlay, Lighten, Darken };

const char* transitionStyleId(TransitionStyle style);
BlendMode blendModeFromOrdinal(int i);

// Port of VisualSafety.kt: the unconditional flash and luminance clamp.
namespace safety {

constexpr float kWcagFlashesPerSecond = 3.0f;
constexpr float kMaxFlashDepth = 0.25f;
constexpr float kStrobeShaderDepth = 0.85f;
constexpr float kFlashShaderDepth = 0.6f;
constexpr float kReducedMotionScale = 0.4f;

SceneParams apply(const SceneParams& p, bool reducedMotion = false);
inline float flashImpulse(float flash, float beatImpulse) { return flash * beatImpulse * kFlashShaderDepth; }
inline float strobeHz() { return kWcagFlashesPerSecond; }
float limitLfoRate(float rateHz, LfoTarget target);
float beatMinIntervalMs(float requestedMs);
TransitionStyle transitionStyle(TransitionStyle requested);
std::string transitionId(const std::string& requested);
float layerMix(float requested, BlendMode mode);

}  // namespace safety

// Port of FlashBudget.kt: three rising edges per rolling second.
class FlashBudget {
public:
    static constexpr float kRiskThreshold = 0.08f;

    explicit FlashBudget(float maxPerSecond = safety::kWcagFlashesPerSecond) : maxPerSecond_(maxPerSecond) {}
    void reset();
    float gainFor(float timeSeconds, float impulse);

private:
    static constexpr float kSuppressedScale = 0.6f;
    static constexpr float kMinGain = 0.05f;
    static constexpr float kWindowSeconds = 1.0f;
    static constexpr int kCapacity = 16;

    void dropOlderThan(float cutoff);
    void record(float timeSeconds);

    float maxPerSecond_;
    float edges_[kCapacity] = {};
    int head_ = 0;
    int count_ = 0;
    bool above_ = false;
    float lastTime_ = -3.0e38f;
};

}  // namespace geode::viz
