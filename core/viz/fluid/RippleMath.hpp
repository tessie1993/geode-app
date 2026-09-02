#pragma once
#include <functional>
#include <utility>
#include <vector>

#include "api/geode_api.h"
#include "viz/LiveSignal.hpp"

namespace geode::viz::fluid::ripple {

// Port of RippleMath.kt.
constexpr float kRefractionCap = 0.08f;
constexpr float kMaxHeight = 8.0f;
constexpr float kHeightDecayRatio = 0.35f;

struct StrokeDrop {
    float x;
    float y;
    float radius;
    float amplitude;
};

float heightDecayPerSubstep(float damping, float subDt);
float dropProfile(float dist, float radius, float amp);
float cflClampedDt(float c, float dt, float dx);
std::pair<float, float> refractionOffset(float hL, float hR, float hT, float hB, float strength);
float inkDissipation(float dissipation, float dt);
std::vector<StrokeDrop> strokeDrops(float x, float y, float dx, float dy, float dt, float radius, float strength);
std::pair<float, float> overlayDropPosition(int index, float aspect);

// Port of RippleOverlayDrops.kt: rings on the heard transient, sparkles on bright treble.
class OverlayDrops {
public:
    static constexpr int kBeatDrops = 2;
    static constexpr int kSparkleInterval = 6;
    static constexpr float kSparkleThreshold = 0.5f;

    using Queue = std::function<void(float x, float y, float radius, float amp)>;

    void reset();
    void tick(const GeodeFeatureFrame& features, float aspect, const Queue& queue);

private:
    int frame_ = 0;
    int dropIndex_ = 0;
    live::Edge hitEdge_;
};

}  // namespace geode::viz::fluid::ripple
