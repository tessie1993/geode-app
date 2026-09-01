#include "analysis/FrameGrid.hpp"

namespace geode::analysis {

std::optional<int64_t> FrameGrid::latestCompleteFrame(int64_t writtenFrames) const {
    const int64_t candidate = floorDiv(writtenFrames - half_, branch_.hopFrames);
    if (candidate >= firstCompleteFrame()) return candidate;
    return std::nullopt;
}

int64_t FrameGrid::floorDiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

}  // namespace geode::analysis
