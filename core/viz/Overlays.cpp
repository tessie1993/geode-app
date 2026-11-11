#include "viz/Overlays.hpp"

#include <algorithm>

#include "viz/VisualSafety.hpp"

namespace geode::viz {

void Overlays::recreate() {
    release();
    flow_.emplace(loader_);
    flow_->create();
    ripple_.emplace(loader_);
    ripple_->create();
    ripple_->applyResolution(kRippleOverlayRes);
    onsetDrops_.reset();
    wake_.reset();
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

void Overlays::stepFlow(const GeodeSoundFrame& features, float dt, const SceneParams& p) {
    if (flow_) flow_->step(features, dt, p);
}

bool Overlays::rippleOverlayActive(const SceneParams& p, bool waterActive) {
    const bool on = p.rippleOverlayEnabled && ripple_ && ripple_->available() && !waterActive;
    if (!on && ripple_) ripple_->clear();
    return on;
}

void Overlays::stepRippleOverlay(const GeodeSoundFrame& features, const SceneParams& p, float dt, bool reducedMotion) {
    if (!ripple_) return;
    fluid::RippleSim& r = *ripple_;
    r.waveSpeed = p.waterWaveSpeed;
    r.damping = p.waterDamping;
    r.gain = reducedMotion ? safety::kReducedMotionScale : 1.0f;
    const auto drop = onsetDrops_.step(features, r.aspect());
    if (drop.drop) r.queueDrop(drop.x, drop.y, drop.radius, drop.amplitude);
    r.step(dt);
}

void Overlays::touchRipples(Scene& scene, const float* xyNdc, int fingers, float dt, bool overlayOn) {
    struct Target {
        Scene* scene;
        fluid::RippleSim* overlay;
    };
    fluid::RippleSim* r = ripple_ ? &*ripple_ : nullptr;
    const float aspect = r ? r->aspect() : 1.0f;
    float xy[fluid::ripple::Wake::kMaxFingers * 2];
    const int n = std::clamp(fingers, 0, fluid::ripple::Wake::kMaxFingers);
    for (int i = 0; i < n; ++i) {
        xy[i * 2] = xyNdc[i * 2] * aspect;
        xy[i * 2 + 1] = xyNdc[i * 2 + 1];
    }
    Target target{scene.isWater() ? &scene : nullptr, overlayOn ? r : nullptr};
    wake_.step(xy, n, dt, &target, [](void* ctx, float x, float y, float amplitude) {
        const Target& t = *static_cast<const Target*>(ctx);
        if (t.scene) {
            t.scene->queueRipple(x, y, amplitude);
        } else if (t.overlay) {
            t.overlay->queueDrop(x, y, fluid::ripple::kTouchRadius, amplitude);
        }
    });
}

}  // namespace geode::viz
