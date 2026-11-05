#include "signal/timeline/Cues.hpp"

#include <algorithm>
#include <cstddef>

namespace geode::signal::timeline {

CueTable CueTable::forSpan(const int64_t* startsMs, const int64_t* endsMs, int count, int64_t spanStartMs,
                           int64_t spanEndMs) {
    CueTable table;
    if (!startsMs || !endsMs || count <= 0) return table;
    const int64_t length = spanEndMs - spanStartMs;
    for (int i = 0; i < count; ++i) {
        if (endsMs[i] <= spanStartMs || startsMs[i] >= spanEndMs) continue;
        table.cues_.push_back({std::max<int64_t>(startsMs[i] - spanStartMs, 0),
                               std::min<int64_t>(endsMs[i] - spanStartMs, length), i});
    }
    return table;
}

int32_t CueTable::at(int64_t atMs) const {
    for (const Cue& c : cues_) {
        if (atMs >= c.startMs && atMs < c.endMs) return c.source;
    }
    return -1;
}

int32_t CueTable::atTriples(const int64_t* triples, int count, int64_t atMs) {
    if (!triples) return -1;
    for (int i = 0; i < count; ++i) {
        const int64_t* c = triples + static_cast<std::ptrdiff_t>(i) * 3;
        if (atMs >= c[0] && atMs < c[1]) return static_cast<int32_t>(c[2]);
    }
    return -1;
}

int64_t CueClock::itemMs(int64_t presentationUs) {
    if (hasOrigin == 0) {
        hasOrigin = 1;
        originUs = presentationUs;
    }
    return (presentationUs - originUs) / 1000;
}

}  // namespace geode::signal::timeline
