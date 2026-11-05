#pragma once
#include <string>

#include "viz/Lfo.hpp"
#include "viz/Params.hpp"

namespace geode::viz {

enum class TransitionStyle { Cut, Fade, Melt, Slide, Zoom };
enum class BlendMode { Normal, Screen, Add, Multiply, Difference, Overlay, Lighten, Darken };

const char* transitionStyleId(TransitionStyle style);
BlendMode blendModeFromOrdinal(int i);

// The unconditional flash and luminance clamp on the params. The measured limit on what is actually
// drawn is FlashLimiter.hpp.
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
TransitionStyle transitionStyle(TransitionStyle requested);
std::string transitionId(const std::string& requested);
float layerMix(float requested, BlendMode mode);

}  // namespace safety

}  // namespace geode::viz
