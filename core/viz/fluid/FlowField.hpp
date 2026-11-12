#pragma once
#include <GLES3/gl3.h>

#include <algorithm>
#include <vector>

#include "api/geode_api.h"
#include "viz/Params.hpp"
#include "viz/fluid/FluidEmitters.hpp"
#include "viz/fluid/FluidSim.hpp"

namespace geode::viz::fluid {

// A velocity-only fluid: the flow Liquid mode's liquid rides (viz/liquid). The sound stirs it through
// the emitters, and a finger pushes it or spins a whirlpool in it.
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
    // One finger's push at (clipX, clipY), clip units (-1..1, y up), velocity in clip units per second.
    void queueKick(float clipX, float clipY, float velX, float velY, float radius);
    // A whirlpool: kicks around a small ring, each along the tangent; `spin` signed, clip units/s.
    void queueVortex(float clipX, float clipY, float spin, float radius);
    // Liquid mode's grid (liquid::planFor): cells along the short side, and second-order advection.
    void setGrid(int cells, bool macCormack);
    // uv displacement per (velocity * second), per axis: the liquid's back-trace scale.
    float velScaleX() const { return sim_.rdx() / static_cast<float>(std::max(sim_.velocityGridWidth(), 1)); }
    float velScaleY() const { return sim_.rdx() / static_cast<float>(std::max(sim_.velocityGridHeight(), 1)); }
    void step(const GeodeSoundFrame& features, float dt, const SceneParams& p);
    // The fingers' kicks alone (Settings › Touch › Smear, viz/TouchSmear): no sound, no curl, `dissipation` per second.
    void stepTouch(float dt, float dissipation);
    void release() { sim_.release(); }

private:
    static constexpr int kGridRes = 64;
    static constexpr float kKickForce = 0.22f;
    static constexpr float kVelocityDissipation = 0.2f;

    FluidSim sim_;
    Emitters emitters_;
    std::vector<Splat> splats_;
};

}  // namespace geode::viz::fluid
