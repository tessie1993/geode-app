#include "viz/TouchField.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz {

void TouchField::submit(const float* xy, int n) {
    const int clamped = std::clamp(n, 0, kMaxPoints);
    std::lock_guard<std::mutex> guard(lock_);
    for (int i = 0; i < clamped * 2; i++) inbox_[i] = xy[i];
    inboxCount_ = clamped;
    inboxDirty_ = true;
}

void TouchField::reset() {
    {
        std::lock_guard<std::mutex> guard(lock_);
        inboxCount_ = 0;
        inboxDirty_ = true;
    }
    liveCount_ = 0;
    anchorSeeded_ = false;
    anchorStrength_ = 0.0f;
    anchorAge_ = 0.0f;
    spin_ = 0.0f;
    axisX_ = 0.0f;
    axisY_ = 0.0f;
    gesture_ = kGestureNone;
    count_ = 0;
    strength_.fill(0.0f);
    age_.fill(0.0f);
    points_.fill(0.0f);
}

void TouchField::step(float dt) {
    const float step = std::clamp(dt, 0.0f, 0.1f);
    int fresh = -1;
    {
        std::lock_guard<std::mutex> guard(lock_);
        if (inboxDirty_) {
            inboxDirty_ = false;
            for (int i = 0; i < inboxCount_ * 2; i++) scratch_[i] = inbox_[i];
            fresh = inboxCount_;
        }
    }
    if (fresh >= 0) adoptLive(fresh, step);
    decay(step);
    publish();
    updateAnchor(step);
    updateGesture(step);
}

float TouchField::spread() const {
    if (gesture_ != kGestureAxis && gesture_ != kGestureVortex) return 0.0f;
    return std::sqrt(axisX_ * axisX_ + axisY_ * axisY_);
}

void TouchField::adoptLive(int n, float dt) {
    for (int i = 0; i < n; i++) {
        const float nx = scratch_[i * 2];
        const float ny = scratch_[i * 2 + 1];
        if (!std::isfinite(nx) || !std::isfinite(ny)) continue;
        if (i < liveCount_ && dt > 0.0f) {
            prevX_[i] = liveX_[i];
            prevY_[i] = liveY_[i];
        } else {
            prevX_[i] = nx;
            prevY_[i] = ny;
        }
        liveX_[i] = std::clamp(nx, -1.0f, 1.0f);
        liveY_[i] = std::clamp(ny, -1.0f, 1.0f);
        strength_[i] = 1.0f;
        age_[i] = 0.0f;
    }
    liveCount_ = n;
}

void TouchField::decay(float dt) {
    const float k = std::exp(-dt / kReleaseTauSeconds);
    for (int i = liveCount_; i < kMaxPoints; i++) {
        if (strength_[i] <= 0.0f) continue;
        strength_[i] *= k;
        age_[i] += dt;
        if (strength_[i] < kSpentStrength) strength_[i] = 0.0f;
    }
    for (int i = 0; i < liveCount_; i++) age_[i] += dt;
}

void TouchField::publish() {
    int live = 0;
    for (int i = 0; i < kMaxPoints; i++) {
        const int base = i * kPointStride;
        if (strength_[i] <= 0.0f) {
            points_[base] = points_[base + 1] = points_[base + 2] = points_[base + 3] = 0.0f;
            continue;
        }
        points_[base] = liveX_[i];
        points_[base + 1] = liveY_[i];
        points_[base + 2] = strength_[i];
        points_[base + 3] = age_[i];
        live = i + 1;
    }
    count_ = live;
}

void TouchField::updateAnchor(float dt) {
    if (liveCount_ > 0) {
        const float tx = liveX_[0];
        const float ty = liveY_[0];
        if (!anchorSeeded_) {
            anchorX_ = tx;
            anchorY_ = ty;
            anchorSeeded_ = true;
        } else {
            const float k = 1.0f - std::exp(-dt / kAnchorTauSeconds);
            anchorX_ += (tx - anchorX_) * k;
            anchorY_ += (ty - anchorY_) * k;
        }
        anchorStrength_ = 1.0f;
        anchorAge_ = 0.0f;
    } else {
        anchorStrength_ *= std::exp(-dt / kReleaseTauSeconds);
        anchorAge_ += dt;
        if (anchorStrength_ < kSpentStrength) {
            anchorStrength_ = 0.0f;
            anchorSeeded_ = false;
        }
    }
}

void TouchField::updateGesture(float dt) {
    gesture_ = liveCount_ <= 0 ? kGestureNone : liveCount_ == 1 ? kGestureAnchor : liveCount_ == 2 ? kGestureAxis : kGestureVortex;
    if (liveCount_ >= 2) {
        axisX_ = liveX_[1] - liveX_[0];
        axisY_ = liveY_[1] - liveY_[0];
    } else {
        const float k = std::exp(-dt / kReleaseTauSeconds);
        axisX_ *= k;
        axisY_ *= k;
    }
    const float target = (liveCount_ >= 3 && dt > 0.0f) ? swirl(dt) : 0.0f;
    const float k = 1.0f - std::exp(-dt / kSpinTauSeconds);
    spin_ += (target - spin_) * k;
}

float TouchField::swirl(float dt) const {
    float cx = 0.0f;
    float cy = 0.0f;
    for (int i = 0; i < liveCount_; i++) {
        cx += liveX_[i];
        cy += liveY_[i];
    }
    cx /= liveCount_;
    cy /= liveCount_;
    float total = 0.0f;
    int counted = 0;
    for (int i = 0; i < liveCount_; i++) {
        const float rx = liveX_[i] - cx;
        const float ry = liveY_[i] - cy;
        const float radius = std::hypot(rx, ry);
        if (radius < 1e-3f) continue;
        const float vx = std::clamp((liveX_[i] - prevX_[i]) / dt, -kMaxSpeed, kMaxSpeed);
        const float vy = std::clamp((liveY_[i] - prevY_[i]) / dt, -kMaxSpeed, kMaxSpeed);
        total += (rx * vy - ry * vx) / (radius * radius);
        counted++;
    }
    return counted == 0 ? 0.0f : std::clamp(total / counted, -kMaxSpeed, kMaxSpeed);
}

}  // namespace geode::viz
