// The C ABI over a render's plan (geode_render_plan_api.h). Each call only unpacks and packs; the rules are
// core/output's. Nothing here throws.
#include "api/geode_render_plan_api.h"

#include "output/ExportRange.hpp"

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

}
