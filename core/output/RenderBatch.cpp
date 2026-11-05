#include "output/RenderBatch.hpp"

#include <algorithm>
#include <unordered_set>

namespace geode::output {

namespace {

// A track ends saved, cancelled or failed; a lock or an empty plan of one track is that track failing.
RenderOutcome trackOutcome(RenderOutcome outcome) {
    return outcome == RenderOutcome::Saved || outcome == RenderOutcome::Cancelled ? outcome : RenderOutcome::Failed;
}

}  // namespace

int batchTracks(BatchScope scope, const std::string_view* keys, const int32_t* sources, int count, int32_t* out,
                int cap) {
    const auto source = static_cast<int32_t>(scope);
    if (!keys || !sources || count <= 0 || source < static_cast<int32_t>(BatchScope::CurrentTrack) ||
        source > static_cast<int32_t>(BatchScope::ChosenTracks)) {
        return 0;
    }
    std::unordered_set<std::string_view> seen;
    int n = 0;
    for (int i = 0; i < count; ++i) {
        if (sources[i] != source || !seen.insert(keys[i]).second) continue;
        if (out && n < cap) out[n] = i;
        ++n;
    }
    return n;
}

int batchNext(int total, int done, bool cancelRequested) {
    return !cancelRequested && done >= 0 && done < total ? done : -1;
}

int batchRecord(int32_t* codes, int total, int done, RenderOutcome outcome) {
    if (done < 0 || done >= total) return done;
    const RenderOutcome recorded = trackOutcome(outcome);
    const int to = recorded == RenderOutcome::Cancelled ? total : done + 1;
    if (codes) std::fill(codes + done, codes + to, static_cast<int32_t>(recorded));
    return to;
}

float batchProgress(int total, int done, float within) {
    if (total <= 0 || done >= total) return 1.0f;
    return (static_cast<float>(std::max(done, 0)) + std::clamp(within, 0.0f, 1.0f)) / static_cast<float>(total);
}

BatchSummary summarizeBatch(const int32_t* codes, int total, int done) {
    BatchSummary s;
    s.total = std::max(total, 0);
    s.done = codes ? std::clamp(done, 0, s.total) : 0;
    for (int i = 0; i < s.done; ++i) {
        switch (trackOutcome(static_cast<RenderOutcome>(codes[i]))) {
            case RenderOutcome::Saved:
                ++s.saved;
                s.lastSaved = i;
                break;
            case RenderOutcome::Cancelled:
                ++s.cancelled;
                break;
            default:
                ++s.failed;
                if (s.firstFailed < 0) s.firstFailed = i;
                break;
        }
    }
    s.finished = s.done >= s.total;
    s.current = s.finished ? -1 : s.done;
    s.report = s.lastSaved >= 0 ? RenderReport::Done : s.failed > 0 ? RenderReport::Failed : RenderReport::Clear;
    return s;
}

int batchFailed(const int32_t* codes, int done, int32_t* out, int cap) {
    if (!codes) return 0;
    int n = 0;
    for (int i = 0; i < done; ++i) {
        if (trackOutcome(static_cast<RenderOutcome>(codes[i])) != RenderOutcome::Failed) continue;
        if (out && n < cap) out[n] = i;
        ++n;
    }
    return n;
}

}  // namespace geode::output
