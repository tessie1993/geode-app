#pragma once
#include "api/geode_api.h"

namespace geode::viz::fluid {

// The audio-drive scaling and particle state-texture size shared by the fluid scenes.
namespace math {
constexpr float kMinAudioDrive = 0.2f;
constexpr float kMaxAudioDrive = 2.5f;
constexpr float kDriveCeiling = 1.5f;

float driven(float value, float audioDrive);
int stateSide(int count);
}  // namespace math

// The frame with its levels and bands scaled by the audio-drive control.
GeodeSoundFrame scaledFeatures(const GeodeSoundFrame& features, float audioDrive);

// Water: which dye texels are a catch well, and the well's radius and depth.
namespace water {
constexpr float kMinCatchRadius = 0.03f;
constexpr float kMaxCatchRadius = 0.3f;
constexpr float kRefCatchRadius = 0.12f;
constexpr float kDisplayBrightness = 1.0f;

bool isCatchWell(float r, float g, float b);
float catchWellRadius(float catchRadius);
float catchWellAmplitude(float speed, float catchRadius, float rippleStrength);
}  // namespace water

// Curl flow: beat drive, field amplitude, trail retention and particle brightness.
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
