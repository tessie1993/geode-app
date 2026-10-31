#pragma once
#include <algorithm>
#include <cmath>

#include "api/geode_api.h"

// The few readings the dedicated scenes (fluid, cymatics, acid, ...) take from the sound engine's
// frame, kept on the scale those scenes were tuned on: a band as its fast att envelope halved
// (typical 0.5, a strong hit 1, capped at 1.5), a hit as the onset envelope above a floor, the
// RMS, the centroid and the stereo field.
namespace geode::viz::live {

constexpr float kHitFloor = 0.06f;

inline float band(float relative) { return std::isfinite(relative) ? std::clamp(0.5f * relative, 0.0f, 1.5f) : 0.0f; }
inline float bass(const GeodeSoundFrame& f) { return band(f.bassAtt); }
inline float mid(const GeodeSoundFrame& f) { return band(f.midAtt); }
inline float treble(const GeodeSoundFrame& f) { return band(f.trebleAtt); }
inline float hit(const GeodeSoundFrame& f) { return f.onsetEnv >= kHitFloor ? std::clamp(f.onsetEnv, 0.0f, 1.0f) : 0.0f; }
inline float level(const GeodeSoundFrame& f) { return std::clamp(f.rms, 0.0f, 1.0f); }
inline float brightness(const GeodeSoundFrame& f) { return std::clamp(f.centroid, 0.0f, 1.0f); }
inline float width(const GeodeSoundFrame& f) { return std::clamp(f.stereoWidth, 0.0f, 1.0f); }
inline float pan(const GeodeSoundFrame& f) { return std::clamp(f.stereoPan, -1.0f, 1.0f); }

// Writes a band back on the same scale (the inverse of bass()/mid()/treble()), for the scenes that
// build a frame of their own: the idle frame, the audio-drive scaling.
inline void setBands(GeodeSoundFrame& f, float bass, float mid, float treble) {
    f.bass = f.bassAtt = 2.0f * std::max(bass, 0.0f);
    f.mid = f.midAtt = 2.0f * std::max(mid, 0.0f);
    f.treble = f.trebleAtt = 2.0f * std::max(treble, 0.0f);
}

// A note start: the engine's onset fires on exactly one frame, so the edge is the trigger itself.
class Edge {
public:
    void reset() {}
    bool step(const GeodeSoundFrame& f) const { return f.onset > 0.0f; }
};

// A slow walk through the song (position) and a count of big moments (the engine's hard cut).
class Traverse {
public:
    float position() const { return position_; }
    int sectionCount() const { return sectionCount_; }

    void reset() {
        position_ = 0.0f;
        sectionCount_ = 0;
    }

    void step(const GeodeSoundFrame& f, float dt) {
        const float rate = (level(f) - kIdleLevel) / kTraverseSeconds;
        position_ = std::clamp(position_ + rate * dt, 0.0f, 1.0f);
        if (f.hardCut > 0.0f) sectionCount_++;
    }

private:
    static constexpr float kIdleLevel = 0.12f;
    static constexpr float kTraverseSeconds = 150.0f;
    float position_ = 0.0f;
    int sectionCount_ = 0;
};

}  // namespace geode::viz::live
