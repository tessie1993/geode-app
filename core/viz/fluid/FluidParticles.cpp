#include "viz/fluid/FluidParticles.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "util/Log.hpp"
#include "viz/fluid/FluidMath.hpp"

namespace geode::viz::fluid {

namespace {
constexpr const char* kTag = "FluidSim";
}

void Particles::create(int particleCount, const Formats& formats) {
    release();
    side_ = math::stateSide(particleCount);
    count_ = side_ * side_;
    const TexFormat stateFmt = formats.hasRgba32 ? formats.rgba32 : formats.rgba;
    state_.emplace(side_, side_, stateFmt, stateFmt);
    state_->create();
    if (!state_->ok()) {
        GEODE_LOGW(kTag, "particle MRT state FBO failed - particle layer disabled");
        release();
        return;
    }
    std::string error;
    const std::string vert = loader_.source("fluid_base_vert.glsl", &error);
    const GLuint seed = loader_.buildSource(vert, loader_.source("fluid_particle_seed_frag.glsl", &error), &error);
    const GLuint update = loader_.buildSource(vert, loader_.source("fluid_particle_update_frag.glsl", &error), &error);
    const GLuint render = loader_.build("fluid_particle_vert.glsl", "fluid_particle_frag.glsl", &error);
    if (seed == 0 || update == 0 || render == 0) {
        GEODE_LOGW(kTag, "particle shader rejected by driver: %s", error.c_str());
        if (seed != 0) glDeleteProgram(seed);
        if (update != 0) glDeleteProgram(update);
        if (render != 0) glDeleteProgram(render);
        release();
        return;
    }
    seed_ = UniformCache(seed);
    update_ = UniformCache(update);
    render_ = UniformCache(render);
    // The streak path is optional: a driver that rejects it leaves the points, never no particles.
    const GLuint streak = loader_.build("fluid_particle_streak_vert.glsl", "fluid_particle_streak_frag.glsl", &error);
    if (streak == 0) GEODE_LOGW(kTag, "particle streak shader rejected - points only: %s", error.c_str());
    streak_ = UniformCache(streak);
    quad_.create();

    std::vector<float> texels(static_cast<size_t>(count_) * 2);
    size_t k = 0;
    for (int y = 0; y < side_; ++y) {
        for (int x = 0; x < side_; ++x) {
            texels[k++] = (static_cast<float>(x) + 0.5f) / static_cast<float>(side_);
            texels[k++] = (static_cast<float>(y) + 0.5f) / static_cast<float>(side_);
        }
    }
    glGenVertexArrays(1, &pointsVao_);
    glGenBuffers(1, &pointsVbo_);
    glBindVertexArray(pointsVao_);
    glBindBuffer(GL_ARRAY_BUFFER, pointsVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(texels.size() * sizeof(float)), texels.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);
    if (streak != 0) {
        // Same texel buffer, read once per INSTANCE, plus a four-corner strip per vertex.
        static constexpr float kCorners[8] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
        glGenVertexArrays(1, &streakVao_);
        glGenBuffers(1, &cornerVbo_);
        glBindVertexArray(streakVao_);
        glBindBuffer(GL_ARRAY_BUFFER, pointsVbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glVertexAttribDivisor(0, 1);
        glBindBuffer(GL_ARRAY_BUFFER, cornerVbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(kCorners), kCorners, GL_STATIC_DRAW);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glBindVertexArray(0);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    seeded_ = false;
    available_ = true;
}

void Particles::setChoreography(const float* spawns, int spawnPoints, const float* catches, int catchPoints) {
    std::copy(spawns, spawns + spawnData_.size(), spawnData_.begin());
    std::copy(catches, catches + catchData_.size(), catchData_.begin());
    spawnCount_ = std::clamp(spawnPoints, 1, Choreography::kMaxSpawn);
    catchCount_ = std::clamp(catchPoints, 0, Choreography::kMaxCatch);
}

void Particles::step(float dt, GLuint velocityTex, float aspect, float flowScale, float timeSeconds) {
    if (!state_) return;
    DoubleMrt& st = *state_;
    glDisable(GL_BLEND);
    quad_.bind();
    if (!seeded_) {
        glUseProgram(seed_.program());
        glUniform1f(seed_.loc("uAspect"), aspect);
        glUniform4fv(seed_.loc("uSpawns"), Choreography::kMaxSpawn, spawnData_.data());
        glUniform1i(seed_.loc("uSpawnCount"), spawnCount_);
        glUniform1f(seed_.loc("uLife"), std::clamp(life, 1.0f, 30.0f));
        blitDiscarding(st.write());
        st.swap();
        seeded_ = true;
    }
    glUseProgram(update_.program());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, st.read().texA());
    glUniform1i(update_.loc("uState"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, st.read().texB());
    glUniform1i(update_.loc("uMeta"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, velocityTex);
    glUniform1i(update_.loc("uVelocityField"), 2);
    glUniform1f(update_.loc("uAspect"), aspect);
    glUniform1f(update_.loc("uDt"), dt);
    glUniform1f(update_.loc("uDrag"), std::clamp(drag, 0.02f, 1.0f));
    glUniform1f(update_.loc("uFlowScale"), flowScale);
    glUniform1f(update_.loc("uTime"), std::fmod(timeSeconds, 256.0f));
    glUniform1f(update_.loc("uLife"), std::clamp(life, 1.0f, 30.0f));
    glUniform4fv(update_.loc("uSpawns"), Choreography::kMaxSpawn, spawnData_.data());
    glUniform1i(update_.loc("uSpawnCount"), spawnCount_);
    glUniform4fv(update_.loc("uCatches"), Choreography::kMaxCatch, catchData_.data());
    glUniform1i(update_.loc("uCatchCount"), catchCount_);
    glUniform1f(update_.loc("uStir"), std::max(stir, 0.0f));
    glUniform1f(update_.loc("uStirFreq"), stirFreq);
    glUniform1f(update_.loc("uStirTime"), std::fmod(stirTime, 6.2831853f * 64.0f));
    blitDiscarding(st.write());
    st.swap();
    quad_.unbind();
}

void Particles::draw(float aspect, float pointScale, float hueBase, float hueSpan, float brightness, float shape, float glow, float timeSeconds) {
    if (!state_) return;
    DoubleMrt& st = *state_;
    const bool streaks = maxStretch > 1.0f && streak_.program() != 0;
    UniformCache& prog = streaks ? streak_ : render_;
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glUseProgram(prog.program());
    glBindVertexArray(streaks ? streakVao_ : pointsVao_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, st.read().texA());
    glUniform1i(prog.loc("uState"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, st.read().texB());
    glUniform1i(prog.loc("uMeta"), 1);
    glUniform1f(prog.loc("uAspect"), aspect);
    glUniform1f(prog.loc("uPointScale"), pointScale);
    glUniform1f(prog.loc("uHueBase"), hueBase);
    glUniform1f(prog.loc("uHueSpan"), hueSpan);
    glUniform1f(prog.loc("uBrightness"), brightness);
    glUniform1f(prog.loc("uShape"), shape);
    glUniform1f(prog.loc("uGlow"), glow);
    glUniform1f(prog.loc("uTime"), timeSeconds);
    if (streaks) {
        GLint vp[4] = {0, 0, 1, 1};
        glGetIntegerv(GL_VIEWPORT, vp);
        glUniform2f(prog.loc("uViewport"), static_cast<float>(std::max(vp[2], 1)), static_cast<float>(std::max(vp[3], 1)));
        glUniform1f(prog.loc("uStretchSeconds"), std::max(stretchSeconds, 0.0f));
        glUniform1f(prog.loc("uMaxStretch"), maxStretch);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, count_);
    } else {
        glDrawArrays(GL_POINTS, 0, count_);
    }
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

void Particles::release() {
    if (state_) state_->release();
    state_.reset();
    for (UniformCache* cache : {&update_, &seed_, &render_, &streak_}) {
        if (cache->program() != 0) glDeleteProgram(cache->program());
        *cache = UniformCache(0);
    }
    quad_.release();
    if (pointsVbo_ != 0) glDeleteBuffers(1, &pointsVbo_);
    if (pointsVao_ != 0) glDeleteVertexArrays(1, &pointsVao_);
    if (cornerVbo_ != 0) glDeleteBuffers(1, &cornerVbo_);
    if (streakVao_ != 0) glDeleteVertexArrays(1, &streakVao_);
    pointsVao_ = 0;
    pointsVbo_ = 0;
    streakVao_ = 0;
    cornerVbo_ = 0;
    seeded_ = false;
    available_ = false;
}

void Particles::blitDiscarding(DoubleMrt::Side& target) {
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo());
    target.discardContents();
    glViewport(0, 0, side_, side_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

}  // namespace geode::viz::fluid
