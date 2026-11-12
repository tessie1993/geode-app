#ifndef GEODE_RENDER_PLAN_API_H
#define GEODE_RENDER_PLAN_API_H

#include "api/geode_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What a render covers, decided before it starts (core/output). Pure: every call is host-tested
   (core/tests/render_plan_test.cpp) and safe on any thread. */

/* The part of a track between two slider positions (fractions of track_ms, output/ExportRange). 1 with the part's
   start and length, or 0 when the render covers the whole track: the length unknown, the part under
   geode_export_range_min_ms, or the whole track anyway. */
GEODE_API int     geode_export_range_of(float start, float end, int64_t track_ms, int64_t* out_start_ms,
                                        int64_t* out_duration_ms);
/* The free tier's cap on a range (duration_ms 0: the whole track); cap_ms <= 0 is none. The capped range, whose
   duration is 0 when it is still the whole track. */
GEODE_API void    geode_export_range_capped(int64_t start_ms, int64_t duration_ms, int64_t cap_ms,
                                            int64_t* out_start_ms, int64_t* out_duration_ms);
GEODE_API int64_t geode_export_range_min_ms(void);

#ifdef __cplusplus
}
#endif

#endif
