#pragma once
#include <cstdint>
#include <optional>

#include "analysis/AnalysisBranch.hpp"

namespace geode::analysis {

class FrameGrid {
public:
    explicit FrameGrid(AnalysisBranch branch) : branch_(branch), half_(branch.windowFrames / 2) {}

    const AnalysisBranch& branch() const { return branch_; }
    int64_t centerSample(int64_t index) const { return index * branch_.hopFrames; }
    int64_t firstSample(int64_t index) const { return centerSample(index) - half_; }
    int64_t endSample(int64_t index) const { return firstSample(index) + branch_.windowFrames; }
    int64_t frameAtOrBefore(int64_t sample) const { return floorDiv(sample, branch_.hopFrames); }
    bool hasFrameCenteredAt(int64_t sample) const { return sample >= 0 && sample % branch_.hopFrames == 0; }
    int64_t firstCompleteFrame() const { return (half_ + branch_.hopFrames - 1) / branch_.hopFrames; }
    std::optional<int64_t> latestCompleteFrame(int64_t writtenFrames) const;
    int64_t centerMicros(int64_t index, int sampleRateHz) const { return centerSample(index) * 1000000 / sampleRateHz; }

private:
    static int64_t floorDiv(int64_t a, int64_t b);

    AnalysisBranch branch_;
    int64_t half_;
};

}  // namespace geode::analysis
