#pragma once
#include <GLES3/gl3.h>

#include <vector>

#include "api/geode_api.h"
#include "viz/Params.hpp"
#include "viz/fluid/FluidEmitters.hpp"
#include "viz/fluid/FluidSim.hpp"

namespace geode::viz::fluid {

// Port of FlowField.kt: a velocity-only fluid whose field warps the fragment styles and the composite.
class FlowField {
public:
    explicit FlowField(const ProgramLoader& loader) : sim_(loader, true) {
        emitters_.beatPattern = Emitters::kPatternRing;
        emitters_.beatSplats = 2;
        emitters_.stirrers = 2;
        emitters_.sparkle = false;
    }

    bool available() const { return sim_.available(); }
    GLuint velocityTex() const { return sim_.velocityTex(); }
    float flowScale() const { return sim_.flowScale(); }
    float aspect() const { return sim_.aspect(); }

    void create();
    void resize(int w, int h) { sim_.resize(w, h); }
    void queueKick(float clipX, float clipY, float velX, float velY, float radius);
    void step(const GeodeFeatureFrame& features, float dt, const SceneParams& p);
    void release() { sim_.release(); }

private:
    static constexpr int kGridRes = 64;
    static constexpr float kKickForce = 0.22f;

    FluidSim sim_;
    Emitters emitters_;
    std::vector<Splat> splats_;
};

}  // namespace geode::viz::fluid
