#include "viz/styles/fluid/FluidSceneBase.hpp"

#include <algorithm>
#include <cmath>

#include "viz/styles/fluid/FluidMath.hpp"
#include "viz/styles/fluid/FluidQuality.hpp"
#include "viz/styles/shared/LiveSignal.hpp"
#include "viz/gl/SceneTarget.hpp"

namespace geode::viz {

void FluidSceneBase::update(const GeodeSoundFrame& features, float dt) {
    time_ = std::fmod(time_ + dt, timeWrapSeconds_);
    lastDt_ = dt;
    pcmStrike_ = pcmPulse_.tick(dt);
    pending_ = features;
    hasPending_ = true;
    last_ = features;
    hasLast_ = true;
    featuresAgeSec_ = 0.0f;
}

GeodeSoundFrame FluidSceneBase::scaledFeatures() {
    featuresAgeSec_ = std::min(featuresAgeSec_ + lastDt_, 1.0f);
    if (hasPending_) return fluid::scaledFeatures(pending_, params_.audioDrive);
    if (hasLast_ && featuresAgeSec_ < 0.25f) return fluid::scaledFeatures(last_, params_.audioDrive);
    return fluid::scaledFeatures(idleFeatures(lastDt_), params_.audioDrive);
}

// The Kotlin idle frame had 16 bands; the same values land at the same fractional positions of the 64.
void FluidSceneBase::fillIdleBands(float t, float amp) {
    for (int i = 0; i < GEODE_SOUND_SPECTRUM_POINTS; ++i) {
        const float j = static_cast<float>(i * 16 / GEODE_SOUND_SPECTRUM_POINTS);
        idleBands_[static_cast<size_t>(i)] = 0.1f + amp * std::sin(t * (0.5f + j * 0.13f));
    }
}

GeodeSoundFrame FluidSceneBase::idleAudioFeatures(float bass, float mid, float treble, float rms) const {
    GeodeSoundFrame f{};
    std::copy(idleBands_.begin(), idleBands_.end(), f.spectrum);
    f.rms = rms;
    live::setBands(f, bass, mid, treble);
    return f;
}

void FluidSceneBase::applyQualityTier() {
    const bool userChanged = params_.fluidQuality != lastUserQuality_;
    if (userChanged) {
        lastUserQuality_ = params_.fluidQuality;
        autoDowngrade_.reset();
        monitor_.reset();
    }
    const int idx = fluid::quality::effectiveIndex(params_.fluidQuality, params_.fluidAutoQuality ? autoDowngrade_.steps : 0);
    if (idx == appliedTier_ && tierApplied()) return;
    appliedTier_ = idx;
    onApplyQualityTier(idx, userChanged);
}

void FluidSceneBase::autoQualityTick() {
    if (!fluid::quality::ownsAutoQuality(params_.fluidAutoQuality, host_.adaptive && host_.adaptive())) {
        // Adapt to frame time owns the one ratchet: the tier stays the user's. Off, or the param off, nothing here.
        autoDowngrade_.reset();
        monitor_.reset();
    } else if (host_.settling && host_.settling()) {
        // A switch's slow frames are not this scene's.
        monitor_.reset();
    } else {
        monitor_.setPacedFps(host_.pacedFps ? host_.pacedFps() : 0.0f);
        const int severity = monitor_.onFrame(lastDt_);
        if (autoDowngrade_.onFrame(severity, monitor_.keepingUp(), lastDt_)) monitor_.reset();
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
    SceneTarget::read(prevFbo_, prevViewport_.data());
}

// The blend state is not asked of the driver (five queries a frame, each a possible pipeline sync): it is known.
// Every pass and scene the renderer draws leaves blending off, and each one that blends turns it on with its own
// function first (TrailPass, the fluid look and particles, EdgeGlowPass, UiBlurPass, the physics layers, ...).
void FluidSceneBase::saveGlState() { saveFramebufferAndViewport(); }

void FluidSceneBase::restoreFramebufferAndViewport() {
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo_));
    glViewport(prevViewport_[0], prevViewport_[1], prevViewport_[2], prevViewport_[3]);
}

// Blending off, as the scene found it (saveGlState); the next pass that blends sets its own function.
void FluidSceneBase::restoreBlend() { glDisable(GL_BLEND); }

}  // namespace geode::viz
