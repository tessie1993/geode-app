#include "viz/styles/fluid/FluidSceneBase.hpp"

#include <algorithm>
#include <cmath>

#include "viz/fields/FieldSet.hpp"
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
    if (userChanged) lastUserQuality_ = params_.fluidQuality;
    // The tier is the user's: nothing lowers it for frame time.
    const int idx = std::clamp(params_.fluidQuality, 0, fluid::quality::kTierCount - 1);
    if (idx == appliedTier_ && tierApplied()) return;
    appliedTier_ = idx;
    onApplyQualityTier(idx, userChanged);
}

void FluidSceneBase::configureChoreography() {
    const SceneParams& p = params_;
    choreography_.path = std::clamp(p.fluidSpawnPath, 0, fluid::Choreography::kPathCount - 1);
    choreography_.spawnCount = std::clamp(p.fluidSpawnPoints, 1, fluid::Choreography::kMaxSpawn);
    choreography_.catchCount = std::clamp(p.fluidCatchPoints, 0, fluid::Choreography::kMaxCatch);
    choreography_.progressionAmount = std::clamp(p.fluidSpawnProgress, 0.0f, 1.0f);
    choreography_.speed = fluid::Choreography::sceneSpeed(p.speed);
}

void FluidSceneBase::applyChoreographyTo(fluid::Particles& particles, float aspect) {
    const SceneParams& p = params_;
    particles.drag = std::clamp(p.fluidParticleDrag, 0.02f, 1.0f);
    particles.life = std::clamp(p.fluidParticleLife, 1.0f, 20.0f);
    choreography_.packSpawns(spawnPack_.data());
    choreography_.packCatches(catchPack_.data(), std::clamp(p.fluidCatchPull, 0.0f, 3.0f), std::clamp(p.fluidCatchRadius, 0.03f, 0.3f));
    int catches = std::clamp(choreography_.catchCount, 0, fluid::Choreography::kMaxCatch);
    const fields::FieldSet* live = host_.fieldSet ? host_.fieldSet() : nullptr;
    for (int i = 0; live != nullptr && i < fields::kMaxFields && catches < fluid::Choreography::kMaxCatch; ++i) {
        const fields::View v = live->view(i);
        if (!v.alive || v.kind != fields::Kind::Attract || !(v.strength > 0.0f)) continue;
        // Sim space: x runs -aspect..aspect, y -1..1.
        float* o = catchPack_.data() + catches * 4;
        o[0] = (v.x * 2.0f - 1.0f) * aspect;
        o[1] = v.y * 2.0f - 1.0f;
        o[2] = std::clamp(v.strength, 0.0f, 3.0f);
        o[3] = std::clamp(p.fluidCatchRadius, 0.03f, 0.3f);
        ++catches;
    }
    particles.setChoreography(spawnPack_.data(), choreography_.spawnCount, catchPack_.data(), catches);
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
