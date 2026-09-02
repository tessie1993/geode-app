#pragma once
#include <utility>

#include "api/geode_api.h"

namespace geode::viz::fluid {

// Port of FluidMath.kt.
namespace math {
constexpr float kMinAudioDrive = 0.2f;
constexpr float kMaxAudioDrive = 2.5f;
constexpr float kDriveCeiling = 1.5f;

float driven(float value, float audioDrive);
std::pair<float, float> curlVelocity(float x, float y, float time, float freq, float detail);
float confinementDeltaV(float curlStrength, float dx, float velDiff, float dt);
std::pair<float, float> softLimitFlow(float x, float y);
std::pair<float, float> terminalSpeedCap(float x, float y);
int stateSide(int count);
float attractorForce(float pull, float dist2);
bool isCaptured(float px, float py, float cx, float cy, float captureRadius);
std::pair<float, float> segDist(float ax, float ay, float bx, float by, float px, float py);
float dragStep(float v, float flow, float drag, float dt = 1.0f / 60.0f);
float bloomPrefilterScale(float br, float threshold, float softKnee);
}  // namespace math

// Port of FluidAudioDrive: the frame with its levels and bands scaled by the audio-drive control.
GeodeFeatureFrame scaledFeatures(const GeodeFeatureFrame& features, float audioDrive);

// Port of WaterMath.kt.
namespace water {
constexpr float kMinCatchRadius = 0.03f;
constexpr float kMaxCatchRadius = 0.3f;
constexpr float kRefCatchRadius = 0.12f;
constexpr float kDisplayBrightness = 1.0f;

bool isCatchWell(float r, float g, float b);
float catchWellRadius(float catchRadius);
float catchWellAmplitude(float speed, float catchRadius, float rippleStrength);
}  // namespace water

// Port of CurlFlowMath.kt.
namespace curl {
constexpr float kMinRetention = 0.6f;
constexpr float kOffRetention = 0.45f;
constexpr float kBaseBrightness = 0.85f;
constexpr float kBeatBrightness = 0.35f;
constexpr float kBaseAmp = 0.55f;
constexpr float kBeatAmp = 0.9f;

float beatDrive(float beatEnvelope, float beatResponse);
float fieldAmp(float audioDrive, float beatDrive);
float retention(float trailLength, bool trails = true);
float fadeAlpha(bool trails, float trailLength, float dt);
float warpDecay(float retention, float dt);
float particleBrightness(float beatEnvelope);
}  // namespace curl

}  // namespace geode::viz::fluid
