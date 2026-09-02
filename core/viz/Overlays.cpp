#include "viz/Overlays.hpp"

#include <algorithm>

namespace geode::viz {

void Overlays::queueTouchStroke(float nx, float ny, float ndx, float ndy, float dt, float strength, double nowSeconds) {
    if (strength <= 0.0f) return;
    std::lock_guard<std::mutex> lock(strokeLock_);
    if (strokes_.size() >= static_cast<size_t>(kMaxTouchBacklog)) return;
    lastTouchSeconds_ = nowSeconds;
    strokes_.push_back({nx, ny, ndx, ndy, dt, strength});
}

bool Overlays::smearing(double nowSeconds) const {
    std::lock_guard<std::mutex> lock(strokeLock_);
    return nowSeconds - lastTouchSeconds_ < kTouchLingerSeconds;
}

void Overlays::recreate() {
    release();
    flow_.emplace(loader_);
    flow_->create();
    ripple_.emplace(loader_);
    ripple_->create();
    ripple_->applyResolution(kRippleOverlayRes);
    rippleDrops_.reset();
}

void Overlays::resize(int width, int height) {
    if (flow_) flow_->resize(width, height);
    if (ripple_) ripple_->resize(width, height);
}

void Overlays::release() {
    if (flow_) flow_->release();
    flow_.reset();
    if (ripple_) ripple_->release();
    ripple_.reset();
}

bool Overlays::wantsFlow(const SceneParams& p, bool fluidActive) const {
    return p.flowEnabled && !fluidActive && flow_ && flow_->available();
}

void Overlays::stepFlow(const GeodeFeatureFrame& features, float dt, const SceneParams& p) {
    if (flow_) flow_->step(features, dt, p);
}

bool Overlays::rippleOverlayActive(const SceneParams& p, bool smearingNow, bool waterActive) const {
    return (p.rippleOverlayEnabled || smearingNow) && ripple_ && ripple_->available() && !waterActive;
}

void Overlays::stepRippleOverlay(const GeodeFeatureFrame& features, const SceneParams& p, float dt) {
    if (!ripple_) return;
    fluid::RippleSim& r = *ripple_;
    r.waveSpeed = 1.2f * std::clamp(p.waterWaveSpeed, 0.2f, 2.0f);
    r.damping = std::clamp(p.waterDamping, 0.9f, 0.999f);
    rippleDrops_.tick(features, r.aspect(), [&r](float x, float y, float radius, float amp) { r.queueDrop(x, y, radius, amp); });
    r.step(dt);
}

void Overlays::drainTouchStrokes(Scene& scene) {
    drained_.clear();
    {
        std::lock_guard<std::mutex> lock(strokeLock_);
        drained_.swap(strokes_);
    }
    if (drained_.empty()) return;
    const bool water = scene.isWater();
    fluid::RippleSim* r = ripple_ ? &*ripple_ : nullptr;
    const float aspect = r ? r->aspect() : 1.0f;
    for (const TouchStroke& st : drained_) {
        if (water) {
            scene.queueTouchStroke(st.nx * 2.0f - 1.0f, 1.0f - st.ny * 2.0f, st.ndx * 2.0f, -st.ndy * 2.0f, st.dt, st.strength);
        } else if (r && r->available()) {
            r->queueStroke((st.nx * 2.0f - 1.0f) * aspect, 1.0f - st.ny * 2.0f, st.ndx * 2.0f * aspect, -st.ndy * 2.0f, st.dt, kTouchRadius,
                           st.strength);
        }
    }
}

}  // namespace geode::viz
