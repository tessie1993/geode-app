#include "analysis/AnalysisBranch.hpp"

namespace geode::analysis {

static_assert(AnalysisBranch::transient().windowFrames % AnalysisBranch::transient().hopFrames == 0);
static_assert(AnalysisBranch::harmony().windowFrames == 8192);

}  // namespace geode::analysis
