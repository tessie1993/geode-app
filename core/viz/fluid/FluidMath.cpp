#include "viz/fluid/FluidMath.hpp"
#include "viz/LiveSignal.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz::fluid {

namespace math {

float driven(float value, float audioDrive) {
    const float d = std::clamp(audioDrive, kMinAudioDrive, kMaxAudioDrive);
    return std::clamp(value * d, 0.0f, std::max(value, kDriveCeiling));
}

int stateSide(int count) { return std::max(static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count)))), 2); }

}  // namespace math

GeodeSoundFrame scaledFeatures(const GeodeSoundFrame& features, float audioDrive) {
    const float d = std::clamp(audioDrive, math::kMinAudioDrive, math::kMaxAudioDrive);
    if (d == 1.0f) return features;
    GeodeSoundFrame out = features;
    for (float& band : out.spectrum) band = math::driven(band, d);
    out.rms = math::driven(features.rms, d);
    live::setBands(out, math::driven(live::bass(features), d), math::driven(live::mid(features), d), math::driven(live::treble(features), d));
    return out;
}

namespace water {

bool isCatchWell(float r, float g, float b) { return std::max({r, g, b}) <= 0.0f; }

float catchWellRadius(float catchRadius) { return std::clamp(catchRadius, kMinCatchRadius, kMaxCatchRadius); }

float catchWellAmplitude(float speed, float catchRadius, float rippleStrength) {
    const float r = catchWellRadius(catchRadius);
    const float spread = std::clamp(kRefCatchRadius / r, 0.4f, 2.5f);
    return -(0.06f + 0.5f * std::clamp(speed, 0.0f, 2.0f)) * spread * std::clamp(rippleStrength, 0.0f, 2.0f);
}

}  // namespace water

namespace curl {

float beatDrive(float beatEnvelope, float beatResponse) { return beatEnvelope * std::clamp(beatResponse, 0.0f, 2.0f); }

float fieldAmp(float audioDrive, float beatDrive) {
    return kBaseAmp * std::clamp(audioDrive, math::kMinAudioDrive, math::kMaxAudioDrive) * (1.0f + std::clamp(beatDrive, 0.0f, 2.0f) * kBeatAmp);
}

float retention(float trailLength, bool trails) {
    if (!trails) return kOffRetention;
    return kMinRetention + (1.0f - kMinRetention) * std::clamp(trailLength, 0.0f, 1.0f);
}

float fadeAlpha(bool trails, float trailLength, float dt) { return 1.0f - std::pow(retention(trailLength, trails) * 0.97f, dt * 60.0f); }

float warpDecay(float retentionValue, float dt) { return std::pow(std::clamp(retentionValue * 0.97f + 0.02f, 0.0f, 0.99f), dt * 60.0f); }

float particleBrightness(float beatEnvelope) { return kBaseBrightness + std::clamp(beatEnvelope, 0.0f, 1.0f) * kBeatBrightness; }

}  // namespace curl

}  // namespace geode::viz::fluid
