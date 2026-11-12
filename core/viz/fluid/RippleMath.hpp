#pragma once
#include <functional>
#include <utility>
#include <vector>

#include "api/geode_api.h"
#include "viz/LiveSignal.hpp"

namespace geode::viz::fluid::ripple {

// Ripple maths: height limits and decay, the CFL step, ink dissipation, finger strokes as drops.
constexpr float kMaxHeight = 8.0f;
constexpr float kHeightDecayRatio = 0.35f;

struct StrokeDrop {
    float x;
    float y;
    float radius;
    float amplitude;
};

float heightDecayPerSubstep(float damping, float subDt);
float cflClampedDt(float c, float dt, float dx);
float inkDissipation(float dissipation, float dt);
std::vector<StrokeDrop> strokeDrops(float x, float y, float dx, float dy, float dt, float radius, float strength);
std::pair<float, float> overlayDropPosition(int index, float aspect);

// The ripple overlay's drops: sparkles on bright treble. (Its rings on every heard hit were a hidden hit
// reaction and are gone: what moves on a hit is the reaction matrix alone.)
class OverlayDrops {
public:
    static constexpr int kSparkleInterval = 6;
    static constexpr float kSparkleThreshold = 0.5f;

    using Queue = std::function<void(float x, float y, float radius, float amp)>;

    void reset();
    void tick(const GeodeSoundFrame& features, float aspect, const Queue& queue);

private:
    int frame_ = 0;
    int dropIndex_ = 0;
};

}  // namespace geode::viz::fluid::ripple
