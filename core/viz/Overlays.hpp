#pragma once
#include <GLES3/gl3.h>

#include <optional>

#include "api/geode_api.h"
#include "viz/Params.hpp"
#include "viz/Scene.hpp"
#include "viz/fluid/FlowField.hpp"
#include "viz/fluid/RippleMath.hpp"
#include "viz/fluid/RippleSim.hpp"
#include "viz/scenes/ProgramLoader.hpp"

namespace geode::viz {

// The flow field and ripple overlay shared by every scene.
class Overlays {
public:
    explicit Overlays(const ProgramLoader& loader) : loader_(loader) {}

    fluid::FlowField* flow() { return flow_ ? &*flow_ : nullptr; }
    fluid::RippleSim* ripple() { return ripple_ ? &*ripple_ : nullptr; }

    // GL thread.
    void recreate();
    void resize(int width, int height);
    void release();
    void stepFlow(const GeodeSoundFrame& features, float dt, const SceneParams& p);
    // The overlay draws while Customize's Water ripples is on; switched off, its rings are dropped.
    bool rippleOverlayActive(const SceneParams& p, bool waterActive);
    // `dt` is wall time: a ring runs out and dies whether or not the music plays on.
    void stepRippleOverlay(const GeodeSoundFrame& features, const SceneParams& p, float dt, bool reducedMotion);
    // The fingers down now (y-up NDC) start rings: on the Water style's own surface, else on the overlay.
    void touchRipples(Scene& scene, const float* xyNdc, int fingers, float dt, bool overlayOn);

private:
    static constexpr int kRippleOverlayRes = 256;

    const ProgramLoader& loader_;
    std::optional<fluid::FlowField> flow_;
    std::optional<fluid::RippleSim> ripple_;
    fluid::ripple::OnsetDrops onsetDrops_;
    fluid::ripple::Wake wake_;
};

}  // namespace geode::viz
