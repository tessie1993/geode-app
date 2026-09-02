#pragma once
#include <algorithm>
#include <cmath>

#include "api/geode_api.h"

namespace geode::viz::live {

constexpr float kHitFloor = 0.06f;

inline float hit(const GeodeFeatureFrame& f) { return f.transient >= kHitFloor ? std::clamp(f.transient, 0.0f, 1.0f) : 0.0f; }
inline float level(const GeodeFeatureFrame& f) { return std::clamp(f.rms, 0.0f, 1.0f); }
inline float brightness(const GeodeFeatureFrame& f) { return std::clamp(f.centroid, 0.0f, 1.0f); }
inline float width(const GeodeFeatureFrame& f) { return std::clamp(f.stereoWidth, 0.0f, 1.0f); }
inline float pan(const GeodeFeatureFrame& f) { return std::clamp(f.stereoPan, -1.0f, 1.0f); }

inline float beatImpulse(const GeodeFeatureFrame& f) {
    if (f.beat <= 0.0f) return 0.0f;
    return f.beatStrength > 0.0f ? f.beatStrength : 1.0f;
}

inline float motionImpulse(const GeodeFeatureFrame& f) { return std::max(beatImpulse(f), f.transient * 0.5f); }

class Edge {
public:
    void reset() { armed_ = true; }
    bool step(const GeodeFeatureFrame& f) {
        const bool hot = f.transient >= kHitFloor;
        const bool fired = hot && armed_;
        armed_ = !hot;
        return fired;
    }

private:
    bool armed_ = true;
};

class Traverse {
public:
    float position() const { return position_; }
    int sectionCount() const { return sectionCount_; }
    void reset() {
        position_ = 0.0f;
        sectionCount_ = 0;
        brightnessMean_ = -1.0f;
        settleSeconds_ = 0.0f;
    }
    void step(const GeodeFeatureFrame& f, float dt) {
        const float drive = level(f);
        const float rate = (drive - kIdleLevel) / kTraverseSeconds;
        position_ = std::clamp(position_ + rate * dt, 0.0f, 1.0f);
        const float bright = brightness(f);
        if (brightnessMean_ < 0.0f) brightnessMean_ = bright;
        settleSeconds_ += dt;
        if (settleSeconds_ >= kSectionRefractorySeconds && std::fabs(bright - brightnessMean_) >= kSectionShift) {
            sectionCount_++;
            settleSeconds_ = 0.0f;
            brightnessMean_ = bright;
        }
        brightnessMean_ += (bright - brightnessMean_) * std::min(dt / kBrightnessTauSeconds, 1.0f);
    }

private:
    static constexpr float kIdleLevel = 0.12f;
    static constexpr float kTraverseSeconds = 150.0f;
    static constexpr float kBrightnessTauSeconds = 8.0f;
    static constexpr float kSectionShift = 0.14f;
    static constexpr float kSectionRefractorySeconds = 12.0f;

    float position_ = 0.0f;
    int sectionCount_ = 0;
    float brightnessMean_ = -1.0f;
    float settleSeconds_ = 0.0f;
};

}  // namespace geode::viz::live
