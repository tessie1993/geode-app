#pragma once
#include <GLES3/gl3.h>

#include <optional>

#include "api/geode_api.h"
#include "viz/Params.hpp"
#include "viz/fluid/FlowField.hpp"
#include "viz/scenes/ProgramLoader.hpp"

namespace geode::viz {

// The flow field shared by every scene: Liquid mode rides it and Smear bends the picture by it. Its 14 programs
// and grids are built on its first use, not with the surface, so a surface that never pours or smears never
// builds them.
class Overlays {
public:
    explicit Overlays(const ProgramLoader& loader) : loader_(loader) {}

    // The field if it was built, else null; never builds it.
    fluid::FlowField* flow() { return flow_ ? &*flow_ : nullptr; }
    // The field, built now (at the last size) if this surface has not built it yet. Null before recreate().
    fluid::FlowField* useFlow();

    // GL thread.
    // A new context: the old field is let go, and the next useFlow() builds one on this context.
    void recreate();
    void resize(int width, int height);
    void release();
    void stepFlow(const GeodeSoundFrame& features, float dt, const SceneParams& p, bool live);

private:
    const ProgramLoader& loader_;
    std::optional<fluid::FlowField> flow_;
    bool wanted_ = false;  // a context is current for the field (recreate() ran, release() did not)
    int width_ = 0;
    int height_ = 0;
};

}  // namespace geode::viz
