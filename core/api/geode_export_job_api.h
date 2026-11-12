#ifndef GEODE_EXPORT_JOB_API_H
#define GEODE_EXPORT_JOB_API_H

#include "api/geode_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An export's render over one renderer (core/output/ExportFrame): the prepare order, the pre-roll, and per frame
   the track's audio up to the frame's exact end, then the draw. The track is read natively (an AIFF through
   audio/ingest/AiffReader, anything else through AMediaExtractor and AMediaCodec, audio/ingest/android).

   create takes the track's descriptor, owned from here on (closed by destroy, or by a create that fails), and the
   span its AssetFileDescriptor declares (length < 0: none, the whole descriptor), the frame rate and where the
   range starts in the track. The renderer is borrowed: destroy the job before it. Every call on the thread
   holding the renderer's GL context. NULL when out of memory or with no renderer. */
typedef struct geode_export_job geode_export_job;
GEODE_API geode_export_job* geode_export_job_create(geode_viz* viz, int fd, int64_t offset, int64_t length, int fps,
                                                    int64_t range_start_ms);
GEODE_API void              geode_export_job_destroy(geode_export_job*);
/* The prepare order up to the look: offscreen pin, frame rate, surface, scene, MilkDrop preset (NULL or "": none),
   engine settings pinned for pin_duration_ms. The pinned hand mask (>= 0) the shell applies the look's reactions
   under, or -1 when the renderer cannot draw scene_id. */
GEODE_API int64_t           geode_export_job_begin(geode_export_job*, int width, int height, const char* scene_id,
                                                   const char* milk_path, int entitled, int64_t pin_duration_ms);
/* Once the look is applied: the pre-roll, its end and the range's decode. 0 when the track cannot be read or
   its audio was not heard (the render fails). */
GEODE_API int               geode_export_job_warm(geode_export_job*);
/* Frame n of the range into target_fbo (0: the default framebuffer). 0 when the decoder failed (the render
   fails); a track that has run out still draws. */
GEODE_API int               geode_export_job_frame(geode_export_job*, int64_t frame, uint32_t target_fbo);

#ifdef __cplusplus
}
#endif

#endif
