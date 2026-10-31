#include "viz/ParticleLayer.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz {

bool ParticleLayer::create(const ShaderSource& assets, ProgramBinaryCache* cache, std::string* error) {
    release();
    const auto vert = assets.load("particle_layer_vert.glsl", error);
    const auto frag = assets.load("particle_layer_frag.glsl", error);
    if (!vert || !frag) return false;
    program_ = program::build(*vert, *frag, cache, error);
    if (program_ == 0) return false;
    uniforms_ = UniformCache(program_);
    // GLES needs a bound VAO to draw, even one with no attributes: positions come from gl_VertexID.
    glGenVertexArrays(1, &vao_);
    return true;
}

void ParticleLayer::release() {
    if (program_ != 0) glDeleteProgram(program_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
    program_ = 0;
    vao_ = 0;
}

void ParticleLayer::step(const SceneParams& p, float dt) {
    dt = std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.1f) : 0.0f;
    const float target = std::isfinite(p.particles) ? std::clamp(p.particles, 0.0f, 1.0f) : 0.0f;
    // The count eases (so a hit throws a burst that settles), the clock runs with the level: a
    // particle's age is music time, not wall time.
    level_ += (target - level_) * (1.0f - std::exp(-dt / (target > level_ ? 0.08f : 0.6f)));
    clock_ = std::fmod(clock_ + dt * (0.15f + 1.6f * level_) * (level_ > 0.01f ? 1.0f : 0.0f), 4096.0f);
    count_ = static_cast<int>(std::lround(std::sqrt(level_) * kMaxParticles));
    fall_ = std::isfinite(p.particleFall) ? std::clamp(p.particleFall, -1.0f, 1.0f) : 0.0f;
}

void ParticleLayer::draw(GLuint sceneTex, int width, int height, float ratio) {
    if (program_ == 0 || count_ <= 0) return;
    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex);
    glUniform1i(uniforms_.loc("uScene"), 0);
    glUniform1f(uniforms_.loc("uClock"), clock_);
    glUniform1f(uniforms_.loc("uFall"), fall_);
    glUniform1f(uniforms_.loc("uSize"), std::max(2.0f, static_cast<float>(std::min(width, height)) * 0.007f));
    glUniform1f(uniforms_.loc("uRatio"), ratio);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(vao_);
    glDrawArrays(GL_POINTS, 0, count_);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

}  // namespace geode::viz
