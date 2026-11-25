#pragma once
#include <algorithm>
#include <array>

namespace geode::viz::fluid::quality {

// The fluid engine's whole cost ladder: a tier says how big the grids are and which of the
// expensive refinements it can afford, so a low-end phone gets the same solver with the
// refinements off rather than a different one.
//
// Ultra and High were raised when the splats went into one batched pass: up to sixteen splats used
// to be sixteen full-grid passes on the dye AND on the velocity, and that budget now pays for finer
// grids and for MacCormack's second dye pass instead.
struct Tier {
    const char* label;
    int simRes;          // velocity / pressure / temperature grid, short side
    int dyeRes;          // dye grid, short side
    int particleSide;    // the fluid styles' tracer state texture side (count = side^2)
    int iterations;      // default pressure budget, in full-grid passes
    bool macCormack;     // second-order advection: one extra pass per advected field
    float maxStretch;    // tracer motion-blur streak ceiling; 0 draws plain points
    int curlParticleSide;  // Curl Flow's population, which is ALL that style draws
};

constexpr std::array<Tier, 5> kTiers = {{
    {"Ultra", 288, 1152, 1024, 28, true, 4.0f, 640},
    {"High", 224, 896, 768, 24, true, 3.0f, 512},
    {"Medium", 128, 512, 512, 20, true, 2.5f, 320},
    {"Low", 96, 384, 320, 16, false, 0.0f, 224},
    {"Min", 64, 256, 160, 12, false, 0.0f, 160},
}};

constexpr int kTierCount = static_cast<int>(kTiers.size());

inline const Tier& tier(int index) { return kTiers[static_cast<size_t>(std::clamp(index, 0, kTierCount - 1))]; }

inline int effectiveIndex(int userIndex, int autoDowngradeSteps) {
    return std::min(std::clamp(userIndex, 0, kTierCount - 1) + std::max(autoDowngradeSteps, 0), kTierCount - 1);
}

// The fluid style's own frame-time watch for "Lower the quality when the phone struggles" (its fluidAutoQuality
// param, which the user can turn off): a sustained deficit against the surface's paced rate asks for relief.
class FrameMonitor {
public:
    // What onFrame answers for a sustained deficit: a mild one (one rung of relief), or frames more than
    // 35 % under the target (two rungs).
    static constexpr int kMildRelief = 1;
    static constexpr int kSevereRelief = 2;
    // A frame this many paced periods long is a stall (a GC pause, a program binary load, a framebuffer
    // reallocation): the pacer caps a frame's dt, so it would read as one slow sample. A stall that stands alone is
    // left out of the window; a run of them is the phone struggling and counts.
    static constexpr float kStallPeriods = 3.0f;

    explicit FrameMonitor(float targetFps = 50.0f, float sustainSeconds = 2.5f, int windowSize = 30)
        : targetFps_(targetFps),
          sustainSeconds_(sustainSeconds),
          windowSize_(std::clamp(windowSize, 1, kMaxWindow)) {}

    // The rate the surface is paced at, 0 when free-running.
    void setPacedFps(float fps) { pacedFps_ = fps; }

    float averageFps() const {
        if (count_ < windowSize_ / 2) return 0.0f;
        float sum = 0.0f;
        const int n = std::min(count_, windowSize_);
        for (int i = 0; i < n; i++) sum += samples_[static_cast<size_t>(i)];
        const float meanDt = sum / static_cast<float>(n);
        return meanDt > 1e-6f ? 1.0f / meanDt : 0.0f;
    }

    // A full window at or above the effective target: the headroom a step back up waits for.
    bool keepingUp() const { return count_ >= windowSize_ && averageFps() >= effectiveTargetFps(); }

    int onFrame(float dtSeconds) {
        const float target = effectiveTargetFps();
        if (target != lastTargetFps_) {
            lastTargetFps_ = target;
            deficitSeconds_ = 0.0f;
        }
        if (!(dtSeconds > 1e-6f)) return 0;
        const bool stall = pacedFps_ > 0.0f && dtSeconds >= kStallPeriods / pacedFps_;
        const bool aloneStall = stall && !afterStall_;
        afterStall_ = stall;
        if (aloneStall) return 0;
        samples_[static_cast<size_t>(index_)] = dtSeconds;
        index_ = (index_ + 1) % windowSize_;
        count_ = std::min(count_ + 1, windowSize_);
        const float avg = averageFps();
        if (avg <= 0.0f) return 0;
        if (avg < target) deficitSeconds_ += dtSeconds; else deficitSeconds_ = 0.0f;
        if (deficitSeconds_ < sustainSeconds_) return 0;
        const float severity = (target - avg) / target;
        return severity > 0.35f ? kSevereRelief : kMildRelief;
    }

    void reset() {
        samples_.fill(0.0f);
        count_ = 0;
        index_ = 0;
        deficitSeconds_ = 0.0f;
        afterStall_ = false;
    }

private:
    static constexpr int kMaxWindow = 64;
    static constexpr float kKeepingUpFraction = 0.85f;
    // The paced rate the default target (50) was tuned against.
    static constexpr float kTargetTunedAtFps = 60.0f;

    float effectiveTargetFps() const {
        if (pacedFps_ <= 0.0f) return targetFps_;
        // Above 60 the target scales with the paced rate; at 60 and under it is the old target, or a share of a
        // lower paced rate, so a 30 fps surface drawing 30 is keeping up.
        if (pacedFps_ > kTargetTunedAtFps) return targetFps_ * pacedFps_ / kTargetTunedAtFps;
        return std::min(targetFps_, pacedFps_ * kKeepingUpFraction);
    }

    float targetFps_;
    float sustainSeconds_;
    int windowSize_;
    std::array<float, kMaxWindow> samples_{};
    int count_ = 0;
    int index_ = 0;
    float deficitSeconds_ = 0.0f;
    bool afterStall_ = false;
    float lastTargetFps_ = 0.0f;
    float pacedFps_ = 0.0f;
};

// With "Adapt to frame time" on (frame::FrameGovernor), the governor is the one thing that gives quality up for
// frame time; a fluid scene's own auto quality would be a second ratchet on the same signal.
inline bool ownsAutoQuality(bool autoQualityParam, bool adaptOn) { return autoQualityParam && !adaptOn; }

// "Lower the quality when the phone struggles": the rungs a fluid scene has stepped down. It used to
// only ever step down, so one janky moment cost the scene its quality for as long as it stayed
// resident, which in a long session is hours. Now a long run of kept-up frames steps back up one
// rung; a step up that a deficit undoes within the probe doubles the next wait, so a style too heavy
// for the phone settles instead of flapping.
struct AutoDowngrade {
    static constexpr float kRelaxSeconds = 45.0f;
    static constexpr float kProbeSeconds = 15.0f;
    static constexpr float kRelaxMaxSeconds = 720.0f;

    int steps = 0;
    float keptUpSeconds = 0.0f;
    float relaxSeconds = kRelaxSeconds;
    float sinceRelaxSeconds = kProbeSeconds;  // at kProbeSeconds: no probe in flight

    void reset() { *this = AutoDowngrade{}; }

    // One frame: `relief` rungs the monitor asks for (0 = none), `keepingUp` whether its window holds the
    // target. True when the rung changed (or a deficit was answered at the bottom), so the caller restarts its
    // monitor. `maxSteps` is the ladder's length: the fluid tiers' by default, frame::FrameGovernor's for
    // Settings › Performance.
    bool onFrame(int relief, bool keepingUp, float dt, int maxSteps = kTierCount - 1) {
        if (sinceRelaxSeconds < kProbeSeconds) {
            sinceRelaxSeconds += dt;
            if (sinceRelaxSeconds >= kProbeSeconds) relaxSeconds = kRelaxSeconds;
        }
        if (relief > 0) {
            if (sinceRelaxSeconds < kProbeSeconds) {
                relaxSeconds = std::min(relaxSeconds * 2.0f, kRelaxMaxSeconds);
                sinceRelaxSeconds = kProbeSeconds;
            }
            keptUpSeconds = 0.0f;
            steps = std::min(steps + relief, std::max(maxSteps, 0));
            return true;
        }
        if (steps == 0 || !keepingUp) {
            keptUpSeconds = 0.0f;
            return false;
        }
        keptUpSeconds += dt;
        if (keptUpSeconds < relaxSeconds) return false;
        keptUpSeconds = 0.0f;
        steps--;
        sinceRelaxSeconds = 0.0f;
        return true;
    }
};

}  // namespace geode::viz::fluid::quality
