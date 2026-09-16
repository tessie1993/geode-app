#include "viz/scenes/FluidSceneBase.hpp"

#include <algorithm>
#include <cmath>

#include "viz/fluid/FluidMath.hpp"
#include "viz/fluid/FluidQuality.hpp"

namespace geode::viz {

void FluidSceneBase::update(const GeodeFeatureFrame& features, float dt) {
    time_ = std::fmod(time_ + dt, timeWrapSeconds_);
    lastDt_ = dt;
    pending_ = features;
    hasPending_ = true;
    last_ = features;
    hasLast_ = true;
    featuresAgeSec_ = 0.0f;
    // Wave three: stepped from the real incoming frame, never the idle
    // synthesis in scaledFeatures()/idleFeatures() below.
    motionField_.step(features, dt);
}

GeodeFeatureFrame FluidSceneBase::scaledFeatures() {
    featuresAgeSec_ = std::min(featuresAgeSec_ + lastDt_, 1.0f);
    if (hasPending_) return fluid::scaledFeatures(pending_, params_.audioDrive);
    if (hasLast_ && featuresAgeSec_ < 0.25f) return fluid::scaledFeatures(last_, params_.audioDrive);
    return fluid::scaledFeatures(idleFeatures(lastDt_), params_.audioDrive);
}

// The Kotlin idle frame had 16 bands; the same values land at the same fractional positions of the 64.
void FluidSceneBase::fillIdleBands(float t, float amp) {
    for (int i = 0; i < GEODE_BAND_COUNT; ++i) {
        const float j = static_cast<float>(i * 16 / GEODE_BAND_COUNT);
        idleBands_[static_cast<size_t>(i)] = 0.1f + amp * std::sin(t * (0.5f + j * 0.13f));
    }
}

GeodeFeatureFrame FluidSceneBase::idleAudioFeatures(float bass, float mid, float treble, float rms) const {
    GeodeFeatureFrame f{};
    std::copy(idleBands_.begin(), idleBands_.end(), f.bands);
    f.rms = rms;
    f.bass = std::max(bass, 0.0f);
    f.mid = std::max(mid, 0.0f);
    f.treble = std::max(treble, 0.0f);
    f.stereoCorrelation = 1.0f;
    // The CPU-side emitters (FluidEmitters/RippleMath) read beatPhase/barPhase
    // straight off whatever frame scaledFeatures() returns, including this
    // idle one; zeroing them here would freeze every phase-locked oscillator
    // instead of settling it into a resting drift. Carry a slow steady phase
    // and neutral confidence instead. motionField_ itself never sees this
    // frame - it is stepped from the real one in update() - so this is only
    // for direct GeodeFeatureFrame readers downstream.
    constexpr float kIdleBeatHz = 1.8f;
    f.beatPhase = std::fmod(time_ * kIdleBeatHz, 1.0f);
    f.barPhase = std::fmod(time_ * kIdleBeatHz * 0.25f, 1.0f);
    f.pulseConfidence = 0.35f;
    f.tempoStability = 0.35f;
    f.harmonicity = 0.5f;
    f.novelty = 0.0f;
    f.sectionBoundary = 0.0f;
    for (float& c : f.chroma) c = 1.0f / static_cast<float>(GEODE_CHROMA_BINS);
    return f;
}

void FluidSceneBase::applyQualityTier() {
    const bool userChanged = params_.fluidQuality != lastUserQuality_;
    if (userChanged) {
        lastUserQuality_ = params_.fluidQuality;
        autoDowngrade_ = 0;
        monitor_.reset();
    }
    const int idx = fluid::quality::effectiveIndex(params_.fluidQuality, params_.fluidAutoQuality ? autoDowngrade_ : 0);
    if (idx == appliedTier_ && tierApplied()) return;
    appliedTier_ = idx;
    onApplyQualityTier(idx, userChanged);
}

void FluidSceneBase::autoQualityTick() {
    if (params_.fluidAutoQuality) {
        monitor_.setPacedFps(host_.pacedFps ? host_.pacedFps() : 0.0f);
        const int severity = monitor_.onFrame(lastDt_);
        if (severity > 0) {
            autoDowngrade_ += severity;
            monitor_.reset();
        }
    }
    applyQualityTier();
}

void FluidSceneBase::configureChoreography() {
    const SceneParams& p = params_;
    choreography_.path = std::clamp(p.fluidSpawnPath, 0, fluid::Choreography::kPathCount - 1);
    choreography_.spawnCount = std::clamp(p.fluidSpawnPoints, 1, fluid::Choreography::kMaxSpawn);
    choreography_.catchCount = std::clamp(p.fluidCatchPoints, 0, fluid::Choreography::kMaxCatch);
    choreography_.progressionAmount = std::clamp(p.fluidSpawnProgress, 0.0f, 1.0f);
    choreography_.speed = fluid::Choreography::sceneSpeed(p.speed);
}

void FluidSceneBase::applyChoreographyTo(fluid::Particles& particles) {
    const SceneParams& p = params_;
    particles.drag = std::clamp(p.fluidParticleDrag, 0.02f, 1.0f);
    particles.life = std::clamp(p.fluidParticleLife, 1.0f, 20.0f);
    choreography_.packSpawns(spawnPack_.data());
    choreography_.packCatches(catchPack_.data(), std::clamp(p.fluidCatchPull, 0.0f, 3.0f), std::clamp(p.fluidCatchRadius, 0.03f, 0.3f));
    particles.setChoreography(spawnPack_.data(), choreography_.spawnCount, catchPack_.data(), choreography_.catchCount);
}

void FluidSceneBase::saveFramebufferAndViewport() {
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo_);
    glGetIntegerv(GL_VIEWPORT, prevViewport_.data());
}

void FluidSceneBase::saveGlState() {
    saveFramebufferAndViewport();
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendFunc_[0]);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendFunc_[1]);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendFunc_[2]);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendFunc_[3]);
    blendWas_ = glIsEnabled(GL_BLEND) == GL_TRUE;
}

void FluidSceneBase::restoreFramebufferAndViewport() {
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo_));
    glViewport(prevViewport_[0], prevViewport_[1], prevViewport_[2], prevViewport_[3]);
}

void FluidSceneBase::restoreBlend() {
    if (blendWas_) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    glBlendFuncSeparate(static_cast<GLenum>(prevBlendFunc_[0]), static_cast<GLenum>(prevBlendFunc_[1]), static_cast<GLenum>(prevBlendFunc_[2]),
                        static_cast<GLenum>(prevBlendFunc_[3]));
}

}  // namespace geode::viz
