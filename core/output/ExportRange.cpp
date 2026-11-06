#include "output/ExportRange.hpp"

#include <algorithm>

namespace geode::output {

bool clampExportRange(int64_t startMs, int64_t endMs, int64_t trackDurationMs, ExportRange* out) {
    if (trackDurationMs <= 0) return false;
    const int64_t start = std::clamp<int64_t>(startMs, 0, trackDurationMs);
    const int64_t end = std::clamp<int64_t>(endMs, 0, trackDurationMs);
    if (end - start < kMinExportRangeMs) return false;
    if (start == 0 && end >= trackDurationMs) return false;
    if (out) *out = {start, end - start};
    return true;
}

}  // namespace geode::output
