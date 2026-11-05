#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// Small whole-track helpers the offline analyser's second half shares (OfflineTrack, OfflineMelody,
// OfflineHarmony, OfflineEmotion). Header-only and inline, so each test can build any one file alone.
// Every smoother here is centred: offline, a value may be measured against the music on both sides of it,
// which is the point of the offline analyser (ADR 0011). Where live uses a one-pole follower of time
// constant τ, the offline twin is a centred mean over ±τ: the same reach, with no lag.
namespace geode::sound::offline::math {

// The mean over a centred window of ±half frames; at the edges, over what exists. Linear time.
inline std::vector<double> centredMean(const std::vector<double>& x, int half) {
    const int n = static_cast<int>(x.size());
    std::vector<double> prefix(x.size() + 1, 0.0), out(x.size(), 0.0);
    for (int i = 0; i < n; ++i) prefix[i + 1] = prefix[i] + x[i];
    for (int i = 0; i < n; ++i) {
        const int a = std::max(0, i - half), b = std::min(n, i + half + 1);
        out[i] = (prefix[b] - prefix[a]) / (b - a);
    }
    return out;
}

inline std::vector<float> centredMean(const std::vector<float>& x, int half) {
    const std::vector<double> m = centredMean(std::vector<double>(x.begin(), x.end()), half);
    return std::vector<float>(m.begin(), m.end());
}

// The mean of x over the frames where `use` holds, in a centred window of ±half; `fallback` where none do.
inline std::vector<double> centredMeanWhere(const std::vector<double>& x, const std::vector<uint8_t>& use, int half,
                                            double fallback = 0.0) {
    const int n = static_cast<int>(x.size());
    std::vector<double> sum(x.size() + 1, 0.0), count(x.size() + 1, 0.0), out(x.size(), fallback);
    for (int i = 0; i < n; ++i) {
        sum[i + 1] = sum[i] + (use[i] ? x[i] : 0.0);
        count[i + 1] = count[i] + (use[i] ? 1.0 : 0.0);
    }
    for (int i = 0; i < n; ++i) {
        const int a = std::max(0, i - half), b = std::min(n, i + half + 1);
        const double c = count[b] - count[a];
        if (c > 0.0) out[i] = (sum[b] - sum[a]) / c;
    }
    return out;
}

// Frames for a span in seconds at a frame rate, at least one.
inline int framesFor(double seconds, double frameRate) { return std::max(1, static_cast<int>(std::lround(seconds * frameRate))); }

// The analysis frame nearest a song time, clamped to the track.
inline int frameAt(double seconds, double frameRate, int frames) {
    return std::clamp(static_cast<int>(std::lround(seconds * frameRate)), 0, std::max(0, frames - 1));
}

// How many of the ascending event times are at or before t.
inline size_t countUpTo(const std::vector<double>& events, double t) {
    return static_cast<size_t>(std::upper_bound(events.begin(), events.end(), t) - events.begin());
}

// The index of the first event inside the frame's span (seconds - dt, seconds], or -1. A trigger fires on
// the one frame whose span holds its event, so every frame rate sees it once and at the same song time.
inline int firstIn(const std::vector<double>& events, double seconds, double dt) {
    const size_t before = countUpTo(events, seconds - dt), now = countUpTo(events, seconds);
    return now > before ? static_cast<int>(before) : -1;
}

// A count of events eased over a centred window of `width` seconds: each event adds `step` along a
// smoothstep from t - width/2 to t + width/2, so the clock is half way up on the event itself and never
// runs backwards. Wraps at `wrap`, the count and its easing together.
inline float easedCount(const std::vector<double>& events, double t, double width, double step, double wrap) {
    const double half = 0.5 * width;
    const size_t whole = countUpTo(events, t - half);
    double value = static_cast<double>(whole) * step;
    for (size_t k = whole; k < events.size() && events[k] < t + half; ++k) {
        const double x = std::clamp((t - events[k] + half) / width, 0.0, 1.0);
        value += step * x * x * (3.0 - 2.0 * x);
    }
    return static_cast<float>(std::fmod(value, wrap));
}

// A running integral sampled on the analysis grid, read between frames by linear interpolation: the
// band clocks, which advance by dt × a level at any frame rate.
inline float integralAt(const std::vector<double>& prefix, double seconds, double frameRate, double wrap) {
    if (prefix.empty()) return 0.0f;
    const double x = std::clamp(seconds * frameRate, 0.0, static_cast<double>(prefix.size() - 1));
    const size_t i = static_cast<size_t>(std::floor(x));
    const size_t j = std::min(i + 1, prefix.size() - 1);
    const double v = prefix[i] + (prefix[j] - prefix[i]) * (x - static_cast<double>(i));
    return static_cast<float>(std::fmod(v, wrap));
}

}  // namespace geode::sound::offline::math
