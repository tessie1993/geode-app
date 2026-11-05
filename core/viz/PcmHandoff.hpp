#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace geode::viz {

// The raw-PCM block the producer thread pushes (Renderer::pushPcm) and the GL thread takes once per
// frame. take() empties it, so a frame with no new push hands the scenes nothing: PlayerSession.latestPcm
// returns null while paused or starved, and re-delivering the last block would hold every PcmPulse at
// its last peak and feed projectM the same samples again. Not locked: Renderer guards it with stateLock_.
class PcmHandoff {
public:
    explicit PcmHandoff(int capacity) : pending_(static_cast<size_t>(std::max(capacity, 0)), 0.0f) {}

    // Keeps the newest `capacity` samples of the block, replacing any block not taken yet.
    void push(const float* samples, int count) {
        const int n = std::min(count, capacity());
        if (!samples || n <= 0) return;
        std::copy(samples + (count - n), samples + count, pending_.begin());
        count_ = n;
    }

    // Copies the pending block into `out` (room for capacity() samples) and marks it consumed.
    int take(float* out) {
        const int n = count_;
        if (n > 0) std::copy(pending_.begin(), pending_.begin() + n, out);
        count_ = 0;
        return n;
    }

    int capacity() const { return static_cast<int>(pending_.size()); }

private:
    std::vector<float> pending_;
    int count_ = 0;
};

// The peak of the newest PCM block, decaying per second.
class PcmPulse {
public:
    explicit PcmPulse(float decayPerSecond = 4.0f, float ceiling = 1.5f) : decay_(decayPerSecond), ceiling_(ceiling) {}

    void accept(const float* samples, int count) {
        float peak = 0.0f;
        for (int i = 0; i < count; ++i) {
            const float s = samples[i];
            if (std::isfinite(s)) peak = std::max(peak, std::fabs(s));
        }
        if (peak > level_) level_ = std::min(peak, ceiling_);
    }

    float tick(float dt) {
        const float out = level_;
        level_ = std::max(level_ - dt * decay_, 0.0f);
        return out;
    }

private:
    float decay_;
    float ceiling_;
    float level_ = 0.0f;
};

}  // namespace geode::viz
