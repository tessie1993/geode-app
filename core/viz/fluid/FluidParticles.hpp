#pragma once
#include <GLES3/gl3.h>

#include <array>
#include <optional>

#include "viz/Program.hpp"
#include "viz/Quad.hpp"
#include "viz/fluid/FluidBuffers.hpp"
#include "viz/fluid/FluidChoreography.hpp"
#include "viz/scenes/ProgramLoader.hpp"

namespace geode::viz::fluid {

// Port of FluidParticles.kt: a square state texture of particles advected by a velocity field.
class Particles {
public:
    explicit Particles(const ProgramLoader& loader) : loader_(loader) {
        spawnData_[2] = 1.0f;
        spawnData_[3] = 0.35f;
    }
    ~Particles() { release(); }
    Particles(const Particles&) = delete;
    Particles& operator=(const Particles&) = delete;

    float drag = 0.5f;
    float life = 6.0f;

    bool available() const { return available_; }

    void create(int particleCount, const Formats& formats);
    void setChoreography(const float* spawns, int spawnPoints, const float* catches, int catchPoints);
    void step(float dt, GLuint velocityTex, float aspect, float flowScale, float timeSeconds = 0.0f);
    void draw(float aspect, float pointScale, float hueBase, float hueSpan, float brightness, float shape = 0.0f, float glow = 0.85f,
              float timeSeconds = 0.0f);
    void invalidateSeed() { seeded_ = false; }
    void release();

private:
    void blitDiscarding(DoubleMrt::Side& target);

    const ProgramLoader& loader_;
    int side_ = 0;
    int count_ = 0;
    std::optional<DoubleMrt> state_;
    UniformCache update_{0};
    UniformCache seed_{0};
    UniformCache render_{0};
    FullscreenTriangle quad_;
    GLuint pointsVao_ = 0;
    GLuint pointsVbo_ = 0;
    bool seeded_ = false;
    std::array<float, Choreography::kMaxSpawn * 4> spawnData_{};
    std::array<float, Choreography::kMaxCatch * 4> catchData_{};
    int spawnCount_ = 1;
    int catchCount_ = 0;
    bool available_ = false;
};

}  // namespace geode::viz::fluid
