#pragma once
#include <GLES3/gl3.h>

#include <mutex>
#include <optional>
#include <vector>

#include "api/geode_api.h"
#include "viz/Params.hpp"
#include "viz/Scene.hpp"
#include "viz/fluid/FlowField.hpp"
#include "viz/fluid/RippleMath.hpp"
#include "viz/fluid/RippleSim.hpp"
#include "viz/scenes/ProgramLoader.hpp"

namespace geode::viz {

// Port of OverlayEffects.kt: the flow field and ripple overlay shared by every scene, plus the touch smears.
class Overlays {
public:
    explicit Overlays(const ProgramLoader& loader) : loader_(loader) {}

    fluid::FlowField* flow() { return flow_ ? &*flow_ : nullptr; }
    fluid::RippleSim* ripple() { return ripple_ ? &*ripple_ : nullptr; }

    // UI thread: one finger sample in 0..1 surface space; strength <= 0 is dropped.
    void queueTouchStroke(float nx, float ny, float ndx, float ndy, float dt, float strength, double nowSeconds);
    bool smearing(double nowSeconds) const;

    // GL thread.
    void recreate();
    void resize(int width, int height);
    void release();
    bool wantsFlow(const SceneParams& p, bool fluidActive) const;
    void stepFlow(const GeodeFeatureFrame& features, float dt, const SceneParams& p);
    bool rippleOverlayActive(const SceneParams& p, bool smearingNow, bool waterActive) const;
    void stepRippleOverlay(const GeodeFeatureFrame& features, const SceneParams& p, float dt);
    void drainTouchStrokes(Scene& scene);

private:
    static constexpr float kTouchRadius = 0.11f;
    static constexpr int kMaxTouchBacklog = 24;
    static constexpr double kTouchLingerSeconds = 2.5;
    static constexpr int kRippleOverlayRes = 256;

    struct TouchStroke {
        float nx;
        float ny;
        float ndx;
        float ndy;
        float dt;
        float strength;
    };

    const ProgramLoader& loader_;
    std::optional<fluid::FlowField> flow_;
    std::optional<fluid::RippleSim> ripple_;
    fluid::ripple::OverlayDrops rippleDrops_;
    mutable std::mutex strokeLock_;
    std::vector<TouchStroke> strokes_;
    std::vector<TouchStroke> drained_;
    double lastTouchSeconds_ = -1.0e9;
};

}  // namespace geode::viz
