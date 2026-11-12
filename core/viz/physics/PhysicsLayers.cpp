#include "viz/physics/PhysicsLayers.hpp"

#include <algorithm>
#include <array>

#include "util/Log.hpp"
#include "viz/fluid/FluidChoreography.hpp"
#include "viz/fluid/FluidMath.hpp"

namespace geode::viz::physics {

namespace {

constexpr const char* kTag = "PhysicsLayers";
// fluid::Particles::setChoreography copies a whole spawn table: ours fills it.
static_assert(kSpawnPoints == fluid::Choreography::kMaxSpawn, "the physics layers' births fill the particles' spawn table");
// The particles' inertia toward the flow (the per-1/60 s factor fluid_particle_update_frag reads), their life in
// seconds and the curl drift's spatial frequency.
constexpr float kDrag = 0.35f;
constexpr float kLife = 5.0f;
constexpr float kStirFreq = 1.4f;

// The sprite's base size: about 2 px on a 720-line target, 4 on a 1440-line one and up.
float pointScale(int height) { return std::clamp(static_cast<float>(height) / 360.0f, 1.5f, 4.0f); }

void bindTexture(UniformCache& program, const char* name, GLuint tex, int unit) {
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(program.loc(name), unit);
}

}  // namespace

GLuint PhysicsLayers::run(const Plan& plan, const Frame& f) {
    building_ = false;
    drew_ = false;
    fluidShown_ = false;
    particlesShown_ = false;
    // Each part outlives its layer going off by the idle window, then goes: nothing is held while both are off.
    const bool keepFluid = fluidKeep_.step(plan.fluid);
    const bool keepParticles = particleKeep_.step(plan.particles);
    if (!keepFluid && fluid_) fluid_.reset();
    if (!keepParticles && particles_) {
        particles_.reset();
        particleSide_ = 0;
        spawnAspect_ = 0.0f;
    }
    if (!keepFluid && !keepParticles) target_.release();
    // The second target lives only while a transition asks for it.
    if (!outgoingAsked_) outgoing_.release();
    outgoingAsked_ = false;
    if (!plan.any() || f.styleTex == 0) return f.styleTex;
    if (seed_.program() == 0) {
        if (!ensurePrograms()) return f.styleTex;
        building_ = true;
    }
    const int w = std::max(f.width, 1);
    const int h = std::max(f.height, 1);
    const float aspect = static_cast<float>(w) / static_cast<float>(h);
    const bool fluidReady = plan.fluid && ensureFluid(plan, w, h, f.blocking);
    const bool particlesReady = plan.particles && ensureParticles(plan, aspect, f.blocking);
    if (!fluidReady && !particlesReady) return f.styleTex;
    if (!target_.ensure(w, h)) return f.styleTex;
    clock_.step(f.dt);
    if (fluidReady) stepFluid(plan, f);
    if (particlesReady) stepParticles(plan, f, aspect, fluidReady);
    drawn_ = plan;
    frame_ = f;
    fluidShown_ = fluidReady;
    particlesShown_ = particlesReady;
    drew_ = true;
    draw(f.styleTex, target_);
    return target_.tex();
}

GLuint PhysicsLayers::over(GLuint pictureTex) {
    if (!drew_ || pictureTex == 0) return pictureTex;
    outgoingAsked_ = true;
    if (!outgoing_.ensure(target_.width(), target_.height())) return pictureTex;
    draw(pictureTex, outgoing_);
    return outgoing_.tex();
}

bool PhysicsLayers::ensurePrograms() {
    if (programsRefused_) return false;
    std::string error;
    const std::string fluidVert = loader_.source("fluid_base_vert.glsl", &error);
    const GLuint seed = loader_.buildSource(fluidVert, loader_.source("physics_seed_frag.glsl", &error), &error);
    const GLuint layer = loader_.build("fade_vert.glsl", "physics_layer_frag.glsl", &error);
    const GLuint mote = loader_.build("physics_particle_vert.glsl", "physics_particle_frag.glsl", &error);
    if (seed == 0 || layer == 0 || mote == 0) {
        // The picture shows without them: a GPU that refuses a pass loses the layers, never the style.
        GEODE_LOGW(kTag, "physics layers off on this GPU: %s", error.c_str());
        for (const GLuint program : {seed, layer, mote}) {
            if (program != 0) glDeleteProgram(program);
        }
        programsRefused_ = true;
        return false;
    }
    seed_ = UniformCache(seed);
    layer_ = UniformCache(layer);
    mote_ = UniformCache(mote);
    return true;
}

bool PhysicsLayers::ensureFluid(const Plan& plan, int width, int height, bool blocking) {
    if (fluidRefused_) return false;
    if (!fluid_) {
        if (!mayBuild(blocking)) return false;
        building_ = true;
        fluid_.emplace(loader_);
        fluid_->simRes = plan.simRes;
        fluid_->dyeRes = plan.dyeRes;
        fluid_->create();
    }
    // A new tier's grids keep what the fluid holds (FluidSim::allocGrids preserves); a new size re-allocates too.
    if (fluid_->available()) {
        fluid_->applyResolution(plan.simRes, plan.dyeRes);
        fluid_->resize(width, height);
    }
    if (!fluid_->available() || fluid_->dyeTex() == 0) {
        GEODE_LOGW(kTag, "physics fluid layer off: no renderable half-float grids on this GPU");
        fluid_.reset();
        fluidRefused_ = true;
        return false;
    }
    return true;
}

bool PhysicsLayers::ensureParticles(const Plan& plan, float aspect, bool blocking) {
    if (particlesRefused_) return false;
    if (!particles_ || particleSide_ != plan.particleSide) {
        // A new population waits its turn; the one there keeps drawing meanwhile.
        if (!mayBuild(blocking)) return particles_.has_value();
        building_ = true;
        if (!formatsProbed_) {
            formats_ = fluid::probeFormats();
            formatsProbed_ = true;
        }
        if (!particles_) particles_.emplace(loader_);
        if (formats_.ok) particles_->create(plan.particleSide * plan.particleSide, formats_);
        if (!formats_.ok || !particles_->available()) {
            GEODE_LOGW(kTag, "physics particle layer off: no particle state on this GPU");
            particles_.reset();
            particlesRefused_ = true;
            return false;
        }
        particleSide_ = plan.particleSide;
        spawnAspect_ = 0.0f;
    }
    if (aspect != spawnAspect_) {
        const std::array<float, kSpawnPoints * 4> births = spawns(aspect);
        const std::array<float, fluid::Choreography::kMaxCatch * 4> noCatches{};
        particles_->setChoreography(births.data(), kSpawnPoints, noCatches.data(), 0);
        spawnAspect_ = aspect;
    }
    return true;
}

void PhysicsLayers::stepFluid(const Plan& plan, const Frame& f) {
    // The whole frame on the music clock, in the solver's stable steps (FluidSim::step); none in silence.
    const float simDt = fluid::math::frameSimDt(f.dt);
    if (!(simDt > 0.0f)) return;
    fluid::FluidSim& sim = *fluid_;
    sim.densityDissipation = kDyeDissipation;
    sim.velocityDissipation = kVelocityDissipation;
    sim.curlStrength = curlStrength(plan.force);
    sim.pressureIterations = plan.pressureIterations;
    sim.macCormack = false;
    sim.timeSeconds = f.time;
    // The picture stirs the velocity along its contours, then pours its colour into the ink (physics_seed_frag).
    sim.applyField(fluid::FluidSim::Field::Velocity, seed_, [&](UniformCache& u) {
        bindTexture(u, "uStyle", f.styleTex, 1);
        glUniform1i(u.loc("uMode"), 0);
        glUniform1f(u.loc("uSwirl"), swirlGain(plan.force));
        glUniform1f(u.loc("uDt"), simDt);
        glUniform1f(u.loc("uWeight"), 0.0f);
    });
    sim.applyField(fluid::FluidSim::Field::Dye, seed_, [&](UniformCache& u) {
        bindTexture(u, "uStyle", f.styleTex, 1);
        glUniform1i(u.loc("uMode"), 1);
        glUniform1f(u.loc("uSwirl"), 0.0f);
        glUniform1f(u.loc("uDt"), simDt);
        glUniform1f(u.loc("uWeight"), dyeWeight(simDt));
    });
    sim.step(simDt);
}

void PhysicsLayers::stepParticles(const Plan& plan, const Frame& f, float aspect, bool withFluid) {
    const float dt = fluid::math::frameSimDt(f.dt);
    if (!(dt > 0.0f)) return;
    fluid::Particles& parts = *particles_;
    parts.drag = kDrag;
    parts.life = kLife;
    parts.maxStretch = 0.0f;
    // With the fluid off the field is zero, so the curl drift is all that moves them; with it on, the drift
    // fills in only where the fluid is calm (fluid_particle_update_frag).
    parts.stir = driftSpeed(plan.force);
    parts.stirFreq = kStirFreq;
    parts.stirTime = clock_.stirTime;
    parts.step(dt, withFluid ? fluid_->velocityTex() : f.zeroTex, aspect, withFluid ? fluid_->flowScale() : 0.0f, f.time);
}

void PhysicsLayers::draw(GLuint pictureTex, Framebuffer& target) {
    const Frame& f = frame_;
    const GLuint ink = fluidShown_ ? fluid_->dyeTex() : f.zeroTex;
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo());
    glViewport(0, 0, target.width(), target.height());
    glDisable(GL_BLEND);
    // Back: the fluid behind or over the picture (PhysicsPlan layer()).
    glUseProgram(layer_.program());
    bindTexture(layer_, "uStyle", pictureTex, 0);
    bindTexture(layer_, "uDye", ink, 1);
    glUniform1f(layer_.loc("uFluid"), fluidShown_ ? drawn_.fluidMix : 0.0f);
    glUniform1f(layer_.loc("uOrder"), drawn_.order);
    glBindVertexArray(f.quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (particlesShown_) {
        // Front: the particles, screened on (PhysicsPlan front()), coloured from the active style's picture so a
        // transition's two sides carry the same particles.
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
        glUseProgram(mote_.program());
        bindTexture(mote_, "uState", particles_->stateTex(), 0);
        bindTexture(mote_, "uMeta", particles_->metaTex(), 1);
        bindTexture(mote_, "uStyle", f.styleTex, 2);
        bindTexture(mote_, "uDye", ink, 3);
        glUniform1f(mote_.loc("uAspect"), static_cast<float>(target.width()) / static_cast<float>(std::max(target.height(), 1)));
        glUniform1f(mote_.loc("uPointScale"), pointScale(target.height()));
        glUniform1f(mote_.loc("uMix"), drawn_.particleMix);
        glBindVertexArray(particles_->pointsVao());
        glDrawArrays(GL_POINTS, 0, particles_->count());
        glDisable(GL_BLEND);
    }
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}

void PhysicsLayers::releaseTargets() {
    fluid_.reset();
    particles_.reset();
    particleSide_ = 0;
    spawnAspect_ = 0.0f;
    target_.release();
    outgoing_.release();
    drew_ = false;
    fluidShown_ = false;
    particlesShown_ = false;
}

void PhysicsLayers::release() {
    releaseTargets();
    for (UniformCache* program : {&seed_, &layer_, &mote_}) {
        if (program->program() != 0) glDeleteProgram(program->program());
        *program = UniformCache(0);
    }
    formatsProbed_ = false;
    programsRefused_ = false;
    fluidRefused_ = false;
    particlesRefused_ = false;
}

}  // namespace geode::viz::physics
