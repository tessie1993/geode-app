// The C ABI over a render's plan (geode_render_plan_api.h). Each call only unpacks and packs; the rules are
// core/output's. No exception leaves it.
#include "api/geode_render_plan_api.h"

#include <exception>
#include <string_view>
#include <vector>

#include "output/ExportRange.hpp"
#include "output/RenderBatch.hpp"

namespace out = geode::output;

extern "C" {

int geode_export_range_of(float start, float end, int64_t track_ms, int64_t* out_start_ms, int64_t* out_duration_ms) {
    out::ExportRange range;
    if (!out::rangeFromFractions(start, end, track_ms, &range)) return 0;
    if (out_start_ms) *out_start_ms = range.startMs;
    if (out_duration_ms) *out_duration_ms = range.durationMs;
    return 1;
}

void geode_export_range_capped(int64_t start_ms, int64_t duration_ms, int64_t cap_ms, int64_t* out_start_ms,
                               int64_t* out_duration_ms) {
    const out::ExportRange range = out::cappedRange({start_ms, duration_ms}, cap_ms);
    if (out_start_ms) *out_start_ms = range.startMs;
    if (out_duration_ms) *out_duration_ms = range.durationMs;
}

int64_t geode_export_range_min_ms(void) { return out::kMinRangeMs; }

int geode_render_batch_order(const char* const* uris, int count, int32_t* out_keep) {
    if (!uris || count <= 0 || !out_keep) return 0;
    try {
        std::vector<std::string_view> views;
        views.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) views.emplace_back(uris[i] ? uris[i] : "");
        const std::vector<int> keep = out::batch::firstOfEach(views);
        for (size_t i = 0; i < keep.size(); ++i) out_keep[i] = keep[i];
        return static_cast<int>(keep.size());
    } catch (const std::exception&) {
        return 0;
    }
}

float geode_render_batch_progress(int done, int total, float within_current) {
    return out::batch::progress(done, total, within_current);
}

}
