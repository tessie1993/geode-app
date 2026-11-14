#pragma once
#include <GLES3/gl3.h>

#include <optional>
#include <string>

#include "viz/Framebuffer.hpp"
#include "viz/Program.hpp"
#include "viz/fluid/FluidBuffers.hpp"
#include "viz/fluid/FluidParticles.hpp"
#include "viz/fluid/FluidSim.hpp"
#include "viz/physics/PhysicsPlan.hpp"
#include "viz/scenes/ProgramLoader.hpp"

namespace geode::viz::physics {

// The physics layers' GL side (PhysicsPlan.hpp has the why and the maths). One per renderer, so live, the
// wallpaper, the dream and an export all draw them the same way, after Trails, Liquid and the dive and before the
// composite. Each frame, with a plan that asks for anything:
//
//   1. the fluid (a fluid::FluidSim at the plan's low-res grids, the solver every fluid style runs) takes its
//      ink and its stir from the finished picture (physics_seed_frag, FluidSim::applyField), then steps;
//   2. the particles (fluid::Particles) ride its velocity, or a zero field and their own curl drift with the fluid
//      off, then step;
//   3. physics_layer_frag lays the fluid behind or over the picture into this pass's own target, and the
//      particles are drawn over that with a screen blend (physics_particle_vert/frag), each coloured from the
//      picture under it.
//
// The composite then shows that target in place of the picture. A plan that asks for nothing (both mixes 0, every
// look's default) draws nothing and returns the picture itself, so the frame is the one it was; the grids,
// particles and target are made only once a layer is on and freed kIdleFrames after both are off. Live, the
// programs, the fluid and the particles are built one per frame, so turning a layer on never stalls a frame for
// all of them; an export builds them whole. Everything steps on the frame's music clock, so silence holds it.
class PhysicsLayers {
public:
    struct Frame {
        GLuint styleTex = 0;  // the finished picture the layers lay over
        int width = 1;        // the scene target's size, which this pass's own target matches
        int height = 1;
        float dt = 0.0f;      // the frame's step on the music clock: 0 holds every layer
        float time = 0.0f;    // the picture's clock (the particles' respawn hash)
        GLuint zeroTex = 0;   // samples as zero: no velocity and no ink with the fluid off
        GLuint quadVao = 0;   // the renderer's attribute-less VAO for fade_vert.glsl
        bool blocking = false;  // an export: build everything on the first frame that asks
    };

    explicit PhysicsLayers(const ProgramLoader& loader) : loader_(loader) {}
    ~PhysicsLayers() { release(); }
    PhysicsLayers(const PhysicsLayers&) = delete;
    PhysicsLayers& operator=(const PhysicsLayers&) = delete;

    // One frame: the texture the composite shows, `f.styleTex` itself when nothing is drawn.
    GLuint run(const Plan& plan, const Frame& f);
    // This frame's layers over another picture (a transition's outgoing style), without stepping anything, into a
    // second target made for the transition: the fluid and the particles hold still across the crossfade instead
    // of fading out with the old style. `pictureTex` itself when run() drew nothing this frame.
    GLuint over(GLuint pictureTex);
    // A part was built this frame (live only): the frame's time says nothing about the phone (the renderer settling).
    bool building() const { return building_; }
    // Frees the grids, the particles and the targets and keeps the programs (a memory trim, a hidden surface).
    void releaseTargets();
    // Every GL name: a new context, or the renderer going away.
    void release();

private:
    bool ensurePrograms();
    bool ensureFluid(const Plan& plan, int width, int height, bool blocking);
    bool ensureParticles(const Plan& plan, float aspect, bool blocking);
    // Live builds take one part a frame; an export builds every part at once.
    bool mayBuild(bool blocking) const { return blocking || !building_; }
    void stepFluid(const Plan& plan, const Frame& f);
    void stepParticles(const Plan& plan, const Frame& f, float aspect, bool withFluid);
    // The fluid under or over `pictureTex`, then the particles, into `target`.
    void draw(GLuint pictureTex, Framebuffer& target);

    const ProgramLoader& loader_;
    std::optional<fluid::FluidSim> fluid_;
    std::optional<fluid::Particles> particles_;
    fluid::Formats formats_{};
    bool formatsProbed_ = false;
    // A part this GPU refused is not asked for again until a new context or a release.
    bool programsRefused_ = false;
    bool fluidRefused_ = false;
    bool particlesRefused_ = false;
    UniformCache seed_{0};
    UniformCache layer_{0};
    UniformCache mote_{0};
    Framebuffer target_{"physics"};
    Framebuffer outgoing_{"physicsOut"};
    // Each part's idle window (PhysicsPlan Keep): the fluid and the particles go on and off on their own.
    Keep fluidKeep_;
    Keep particleKeep_;
    Clock clock_;
    int particleSide_ = 0;
    float spawnAspect_ = 0.0f;
    bool building_ = false;
    // over() was asked this frame; the second target is freed the frame after a transition ends.
    bool outgoingAsked_ = false;
    // What run() drew this frame, for over().
    bool drew_ = false;
    bool fluidShown_ = false;
    bool particlesShown_ = false;
    Plan drawn_;
    Frame frame_;
};

}  // namespace geode::viz::physics
