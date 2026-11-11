#include "output/LoopPlan.hpp"

#include <algorithm>
#include <climits>
#include <cmath>

namespace geode::output {

namespace {
template <typename T>
T clampTo(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
}  // namespace

LoopSpec LoopSpec::of(int64_t loopMs, int64_t crossfadeMs, int fps) {
    LoopSpec s;
    s.loopMs = clampTo(loopMs, kMinLoopMs, kMaxLoopMs);
    s.crossfadeMs = clampTo(crossfadeMs, kMinCrossfadeMs, std::min(kMaxCrossfadeMs, s.loopMs / 2));
    s.fps = clampTo(fps, kMinFps, kMaxFps);
    return s;
}

bool LoopSpec::valid() const {
    return loopMs > 0 && crossfadeMs > 0 && crossfadeMs * 2 <= loopMs && fps >= kMinFps && fps <= kMaxFps;
}

int LoopSpec::loopFrames() const { return std::max(2, static_cast<int>(loopMs * fps / 1000)); }

int LoopSpec::crossfadeFrames() const {
    const int frames = static_cast<int>(crossfadeMs * fps / 1000);
    return clampTo(frames, 1, std::max(1, loopFrames() / 2));
}

int64_t LoopSpec::loopDurationUs() const { return fps > 0 ? loopFrames() * 1'000'000LL / fps : 0; }

int64_t LoopSpec::seamPreviewStartMs(int64_t leadMs) const { return std::max<int64_t>(0, loopMs - crossfadeMs - leadMs); }

LoopSpec LoopSpec::budgeted(int width, int height) const {
    const int64_t perFrame = static_cast<int64_t>(std::max(width, 1)) * std::max(height, 1) * kBytesPerPixel;
    // The budget over a 1x1 frame is 50 M frames, so the quotient always fits an int.
    const int affordable = static_cast<int>(std::max<int64_t>(1, kSeamStashBudgetBytes / perFrame));
    if (crossfadeFrames() <= affordable || fps <= 0) return *this;
    LoopSpec out = *this;
    // ceil(affordable * 1000 / fps): floor(ms * fps / 1000) is then exactly `affordable` for fps < 1000.
    out.crossfadeMs = std::max<int64_t>(1, (static_cast<int64_t>(affordable) * 1000 + fps - 1) / fps);
    return out;
}

float seamWeight(int tail, int seamFrames) {
    return seamFrames > 1 ? static_cast<float>(tail) / (static_cast<float>(seamFrames) - 1.0f) : 1.0f;
}

bool driftMoves(float hueTurns, float warmth, int stops) { return stops > 1 && (hueTurns != 0.0f || warmth != 0.0f); }

int driftStops(float hueTurns, float warmth, int stops, DriftStop* out, int capacity) {
    if (!driftMoves(hueTurns, warmth, stops)) {
        if (out && capacity > 0) out[0] = DriftStop{};
        return 1;
    }
    const float last = static_cast<float>(stops - 1);
    for (int i = 0; out && i < std::min(stops, capacity); ++i) {
        const float progress = static_cast<float>(i) / last;
        out[i] = DriftStop{i, hueTurns * progress, warmth * progress};
    }
    return stops;
}

void applyDrift(float colorShift, float temperature, float hueOffset, float warmthOffset, float* outColorShift,
                float* outTemperature) {
    const float hue = colorShift + hueOffset;
    const float fraction = hue - std::floor(hue);
    if (outColorShift) *outColorShift = clampTo(fraction, 0.0f, 1.0f);
    if (outTemperature) *outTemperature = clampTo(temperature + warmthOffset, -1.0f, 1.0f);
}

LoopExtendPlan LoopExtendPlan::of(int64_t loopDurationUs, int64_t totalDurationUs, int loops) {
    LoopExtendPlan p;
    p.loopUs_ = std::max<int64_t>(1, loopDurationUs);
    p.totalUs_ = std::max<int64_t>(1, totalDurationUs);
    const int repeats = static_cast<int>(std::max<int64_t>(1, (p.totalUs_ + p.loopUs_ - 1) / p.loopUs_));
    const int stops = std::min(std::max(loops, 1), repeats);
    const int each = repeats / stops;
    const int remainder = repeats % stops;
    int64_t startUs = 0;
    p.segments_.reserve(static_cast<size_t>(stops));
    p.writes_.reserve(static_cast<size_t>(repeats));
    for (int i = 0; i < stops; ++i) {
        const int count = each + (i < remainder ? 1 : 0);
        p.segments_.push_back({i, startUs, count});
        for (int r = 0; r < count; ++r) p.writes_.push_back({i, startUs + r * p.loopUs_});
        startUs += count * p.loopUs_;
    }
    return p;
}

int64_t LoopExtendPlan::seamPreviewStartMs(int64_t seamMs, int64_t leadMs) const {
    return clampTo<int64_t>(seamMs - leadMs, 0, totalUs_ / 1000);
}

int64_t LoopExtendPlan::estimatedBytes(int64_t loopBytes, int64_t soundtrackBytes) const {
    const int64_t body = repeats() * loopBytes + soundtrackBytes;
    return body + body / kSampleTableHeadroomDivisor;
}

int LoopExtendPlan::fittingBitRate(int64_t limitBytes, int64_t soundtrackBytes) const {
    const int64_t seconds = std::max<int64_t>(1, totalUs_ / 1'000'000);
    const int64_t room = std::max<int64_t>(0, limitBytes - soundtrackBytes);
    return static_cast<int>(std::min<int64_t>(room * 8 / seconds, INT_MAX));
}

}  // namespace geode::output
