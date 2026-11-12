#pragma once
#include <string>

#include "viz/Lfo.hpp"
#include "viz/Params.hpp"

namespace geode::viz {

enum class TransitionStyle { Cut, Fade, Melt, Slide, Zoom };
// Customize › Layers' blends; the ordinal is the wire value and composite_frag.glsl's layerBlend case.
enum class BlendMode { Normal, Screen, Add, Multiply, Difference, Overlay, Lighten, Darken };
constexpr int kBlendModes = static_cast<int>(BlendMode::Darken) + 1;

const char* transitionStyleId(TransitionStyle style);
BlendMode blendModeFromOrdinal(int i);
const char* blendModeLabel(BlendMode mode);
// Every label in ordinal order, each followed by '\n' (geode_layer_blend_names).
const std::string& blendModeLabels();

// The unconditional flash and luminance clamp on the params.
namespace safety {

constexpr float kWcagFlashesPerSecond = 3.0f;
constexpr float kMaxFlashDepth = 0.25f;
constexpr float kReducedMotionScale = 0.4f;

SceneParams apply(const SceneParams& p, bool reducedMotion = false);
float limitLfoRate(float rateHz, LfoTarget target);
TransitionStyle transitionStyle(TransitionStyle requested);
std::string transitionId(const std::string& requested);
float layerMix(float requested, BlendMode mode);

}  // namespace safety

}  // namespace geode::viz
