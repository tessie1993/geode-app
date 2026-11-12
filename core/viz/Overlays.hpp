#pragma once
#include <GLES3/gl3.h>

#include <optional>

#include "api/geode_api.h"
#include "viz/Params.hpp"
#include "viz/fluid/FlowField.hpp"
#include "viz/scenes/ProgramLoader.hpp"

namespace geode::viz {

// The flow field shared by every scene.
class Overlays {
public:
    explicit Overlays(const ProgramLoader& loader) : loader_(loader) {}

    fluid::FlowField* flow() { return flow_ ? &*flow_ : nullptr; }

    // GL thread.
    void recreate();
    void resize(int width, int height);
    void release();
    void stepFlow(const GeodeSoundFrame& features, float dt, const SceneParams& p);

private:
    const ProgramLoader& loader_;
    std::optional<fluid::FlowField> flow_;
};

}  // namespace geode::viz
