#pragma once
#include <array>
#include <mutex>

namespace geode::viz {

// Port of TouchField.kt: latest-wins handover from the UI thread, stepped once per frame on the GL thread.
class TouchField {
public:
    static constexpr int kMaxPoints = 5;
    static constexpr int kPointStride = 4;
    static constexpr int kGestureNone = 0;
    static constexpr int kGestureAnchor = 1;
    static constexpr int kGestureAxis = 2;
    static constexpr int kGestureVortex = 3;
    static constexpr float kReleaseTauSeconds = 0.55f;
    static constexpr float kAnchorTauSeconds = 0.06f;

    const std::array<float, kMaxPoints * kPointStride>& points() const { return points_; }
    int count() const { return count_; }
    float anchorX() const { return anchorX_; }
    float anchorY() const { return anchorY_; }
    float anchorStrength() const { return anchorStrength_; }
    float anchorAge() const { return anchorAge_; }
    int gesture() const { return gesture_; }
    float axisX() const { return axisX_; }
    float axisY() const { return axisY_; }
    float spin() const { return spin_; }

    void submit(const float* xy, int n);
    void reset();
    void step(float dt);
    float spread() const;

private:
    static constexpr float kSpentStrength = 0.004f;
    static constexpr float kMaxSpeed = 8.0f;
    static constexpr float kSpinTauSeconds = 0.25f;

    void adoptLive(int n, float dt);
    void decay(float dt);
    void publish();
    void updateAnchor(float dt);
    void updateGesture(float dt);
    float swirl(float dt) const;

    std::array<float, kMaxPoints * kPointStride> points_{};
    int count_ = 0;
    float anchorX_ = 0.0f;
    float anchorY_ = 0.0f;
    float anchorStrength_ = 0.0f;
    float anchorAge_ = 0.0f;
    int gesture_ = kGestureNone;
    float axisX_ = 0.0f;
    float axisY_ = 0.0f;
    float spin_ = 0.0f;

    std::mutex lock_;
    std::array<float, kMaxPoints * 2> inbox_{};
    int inboxCount_ = 0;
    bool inboxDirty_ = false;

    std::array<float, kMaxPoints> liveX_{};
    std::array<float, kMaxPoints> liveY_{};
    std::array<float, kMaxPoints> strength_{};
    std::array<float, kMaxPoints> age_{};
    std::array<float, kMaxPoints> prevX_{};
    std::array<float, kMaxPoints> prevY_{};
    int liveCount_ = 0;
    bool anchorSeeded_ = false;
    std::array<float, kMaxPoints * 2> scratch_{};
};

}  // namespace geode::viz
