#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace geode::viz {

// MilkDrop's raw PCM (projectM's waveform input, and every scene's PcmPulse): the mono mix of exactly the
// samples this renderer's sound engine heard since the last frame. Renderer::writeAudio hands each block it
// writes into the engine here as well, so the raw tail comes from the same ring and the same each-sample-once
// cursor as the sound frame, on every surface that hears sound (live, cast, wallpaper, dream), with no
// second Kotlin ring and no push from the app (#360). The GL thread takes it once per frame; take() empties
// it, so a frame with no new samples (paused, starved, silent source gone) hands the scenes nothing:
// re-delivering the last block would hold every PcmPulse at its last peak and feed projectM the same
// samples again. Not locked: Renderer guards it with stateLock_.
class PcmHandoff {
public:
    explicit PcmHandoff(int capacity)
        : pending_(static_cast<size_t>(std::max(capacity, 0)), 0.0f), side_(static_cast<size_t>(std::max(capacity, 0)), 0.0f) {}

    // How many source frames make one sample for projectM: its buffers and bands are sized for 44.1-48 kHz,
    // so 88.2 and 96 kHz are halved and 176.4 and 192 kHz quartered. An unknown rate (0) passes through.
    static int decimationFor(int sampleRateHz) {
        const int ratio = sampleRateHz / 44100;
        return ratio >= 4 ? 4 : ratio >= 2 ? 2 : 1;
    }

    // Appends the mono mix (the mean of the channels, so mono passes through unchanged and stereo is
    // (L + R) / 2) of `frames` interleaved frames, averaged over decimationFor(sampleRateHz) frames; frames
    // short of one sample wait for the next write, so chunk sizes drop nothing. Several writes in one frame
    // (an export delivering in pieces) add up; only the newest capacity() samples are kept, as projectM
    // only wants the tail.
    void write(const float* interleaved, int frames, int channels, int sampleRateHz = 0) {
        if (!interleaved || frames <= 0 || channels <= 0 || capacity() == 0) return;
        const int factor = decimationFor(sampleRateHz);
        if (factor != factor_) {
            factor_ = factor;
            held_ = 0;
            heldSum_ = 0.0f;
            heldSide_ = 0.0f;
        }
        const int produced = (held_ + frames) / factor;
        const int n = std::min(produced, capacity());
        const int skip = produced - n;
        const int keep = std::min(count_, capacity() - n);
        if (keep < count_) {
            std::copy(pending_.begin() + (count_ - keep), pending_.begin() + count_, pending_.begin());
            std::copy(side_.begin() + (count_ - keep), side_.begin() + count_, side_.begin());
        }
        // The side signal, (L - R) / 2 of the first two channels: with the mono mix it gives stereo back
        // (L = mono + side, R = mono - side) for the one consumer that draws left against right, MilkDrop.
        const float sideScale = channels >= 2 ? 0.5f / static_cast<float>(factor) : 0.0f;
        const float scale = 1.0f / static_cast<float>(channels * factor);
        int made = 0;
        for (int i = 0; i < frames; ++i) {
            const float* frame = interleaved + static_cast<size_t>(i) * static_cast<size_t>(channels);
            for (int c = 0; c < channels; ++c) heldSum_ += frame[c];
            if (channels >= 2) heldSide_ += frame[0] - frame[1];
            if (++held_ < factor) continue;
            if (made >= skip) {
                pending_[static_cast<size_t>(keep + made - skip)] = heldSum_ * scale;
                side_[static_cast<size_t>(keep + made - skip)] = heldSide_ * sideScale;
            }
            ++made;
            held_ = 0;
            heldSum_ = 0.0f;
            heldSide_ = 0.0f;
        }
        count_ = keep + n;
    }

    // Copies the pending samples into `out` (room for capacity() samples) and marks them consumed;
    // `sideOut`, when given, gets their side signal (all zero for a mono source).
    int take(float* out, float* sideOut = nullptr) {
        const int n = count_;
        if (n > 0) std::copy(pending_.begin(), pending_.begin() + n, out);
        if (n > 0 && sideOut) std::copy(side_.begin(), side_.begin() + n, sideOut);
        count_ = 0;
        return n;
    }

    // A new epoch (seek, track or source change): samples of the old one never reach the next frame.
    void clear() {
        count_ = 0;
        held_ = 0;
        heldSum_ = 0.0f;
        heldSide_ = 0.0f;
    }

    int capacity() const { return static_cast<int>(pending_.size()); }

private:
    std::vector<float> pending_;
    std::vector<float> side_;
    int count_ = 0;
    // Decimation: the factor in use, and the frames (and their sum) still short of one sample.
    int factor_ = 1;
    int held_ = 0;
    float heldSum_ = 0.0f;
    float heldSide_ = 0.0f;
};

// When the raw PCM stops (paused, starved, the source gone), projectM keeps redrawing the last block it
// was given: its waveform freezes and its spectrum stays lit. MilkDrop itself draws a flat line then.
// tick() says, once per gap, that the engine should be handed silence: after kSeconds without samples,
// the same wait core/sound's engine gives a starved source.
class PcmStarve {
public:
    static constexpr float kSeconds = 0.25f;

    bool tick(bool gotSamples, float dt) {
        if (gotSamples) {
            quietS_ = 0.0f;
            flushed_ = false;
            return false;
        }
        if (flushed_) return false;
        quietS_ += std::max(dt, 0.0f);
        if (quietS_ < kSeconds) return false;
        flushed_ = true;
        return true;
    }

private:
    float quietS_ = 0.0f;
    // Starts true: an engine that was never fed holds silence already.
    bool flushed_ = true;
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
