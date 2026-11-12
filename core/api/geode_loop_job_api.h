#ifndef GEODE_LOOP_JOB_API_H
#define GEODE_LOOP_JOB_API_H

#include "api/geode_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One loop render (core/output/LoopRender): the encoder it settles on, each drift stop's frames, seam and
   presentation times, and the video-only scratch file each stop is encoded into. The shell fills the job in with
   the set calls, runs it on a worker thread, polls it from another and takes the files' paths when it is done;
   native never calls back.

   Threads: the set calls and run on one thread (run holds the GL context it makes). cancel, progress and state
   from any thread, during the run too. Everything else after run has returned. destroy once no call is in
   flight. NULL when out of memory; every call takes NULL. */
typedef struct geode_loop_job geode_loop_job;
GEODE_API geode_loop_job* geode_loop_job_create(void);
GEODE_API void            geode_loop_job_destroy(geode_loop_job*);

/* The frame, the bit rate asked for, the loop and its crossfade on the fps grid, how long the extended file will
   run (0: unknown; it caps the bit rate) and the directory the scratch files go in. */
GEODE_API void geode_loop_job_set_output(geode_loop_job*, const char* cache_dir, int width, int height, int bit_rate,
                                         int64_t loop_ms, int64_t crossfade_ms, int fps, int64_t long_form_ms);
/* The palette drift across the reel: `stops` loops, the last one hue_turns and warmth away from the first. */
GEODE_API void geode_loop_job_set_drift(geode_loop_job*, float hue_turns, float warmth, int stops);
/* A codec to try, in the order added (two at most): its MIME type and the top frame rate the device lists for it
   at this size (<= 0: it did not say). */
GEODE_API void geode_loop_job_add_codec(geode_loop_job*, const char* mime, int top_fps);
/* The track every stop hears: its descriptor, owned from here on (closed by destroy), and the span its
   AssetFileDescriptor declares (length < 0: none, the whole descriptor). */
GEODE_API void geode_loop_job_set_track(geode_loop_job*, int fd, int64_t offset, int64_t length);
/* The renderer's inputs, as geode_viz_create and geode_export_job_begin take them. The asset manager must
   outlive the run. milk_path NULL: no MilkDrop preset. */
GEODE_API void geode_loop_job_set_scene(geode_loop_job*, AAssetManager* assets, const char* milk_texture_dir,
                                        const char* scene_id, const char* milk_path, int entitled);
/* The look, each part in the frame the geode_viz setter of the same name takes; all copied. */
GEODE_API void geode_loop_job_set_params(geode_loop_job*, const float* values, int count);
GEODE_API void geode_loop_job_set_lfo(geode_loop_job*, int slot, const float* config, int count);
GEODE_API void geode_loop_job_set_adsr(geode_loop_job*, int slot, const float* config, int count);
GEODE_API void geode_loop_job_set_sound_tuning(geode_loop_job*, const float* tuning, int count);
GEODE_API void geode_loop_job_set_user_reactions(geode_loop_job*, const float* routes, int count, int has_user);
GEODE_API void geode_loop_job_set_motion(geode_loop_job*, int reduced_motion, const char* transition_id,
                                         int64_t transition_ms);
/* pixels NULL, or fewer than width * height of them (count): no image. */
GEODE_API void geode_loop_job_set_underlay(geode_loop_job*, const uint32_t* pixels, int count, int width, int height,
                                           int blend, float amount);
/* The wordmark's tile (tile_w * tile_h * 4 bytes, as geode_export_watermark_create takes it) and where it goes;
   placement 0 or rgba NULL: these frames carry none. */
GEODE_API void geode_loop_job_set_mark(geode_loop_job*, const uint8_t* rgba, int tile_w, int tile_h, int placement,
                                       int corner);

/* Renders every stop; returns when the job is rendered, cancelled or failed. A job runs once. */
GEODE_API void geode_loop_job_run(geode_loop_job*);
GEODE_API void geode_loop_job_cancel(geode_loop_job*);
/* 0..1 across the whole reel. */
GEODE_API float geode_loop_job_progress(geode_loop_job*);

#define GEODE_LOOP_JOB_IDLE      0
#define GEODE_LOOP_JOB_RUNNING   1
#define GEODE_LOOP_JOB_RENDERED  2
#define GEODE_LOOP_JOB_CANCELLED 3
#define GEODE_LOOP_JOB_FAILED    4
GEODE_API int geode_loop_job_state(geode_loop_job*);

/* Why a failed job failed; detail is the text the reason is shown with ("" when it has none). */
#define GEODE_LOOP_FAILURE_NONE             0
#define GEODE_LOOP_FAILURE_ENCODER_REJECTED 1 /* no codec took the frame at any rate */
#define GEODE_LOOP_FAILURE_SHADER_COMPILE   2 /* the seam pass did not build; detail is the log */
#define GEODE_LOOP_FAILURE_LOW_GPU_MEMORY   3 /* not one seam frame could be allocated */
#define GEODE_LOOP_FAILURE_NO_OUTPUT        4 /* the encoder never announced its format */
#define GEODE_LOOP_FAILURE_STALLED          5 /* the encoder never reached its end of stream */
#define GEODE_LOOP_FAILURE_ENCODER_FAILED   6 /* the encoder refused to open, start or hand out a buffer */
#define GEODE_LOOP_FAILURE_CACHE_WRITE      7 /* a stop's file could not be made; detail is the reason */
#define GEODE_LOOP_FAILURE_STOPPED          8 /* anything else; detail is the message, or "" */
GEODE_API int         geode_loop_job_failure(geode_loop_job*);
GEODE_API const char* geode_loop_job_detail(geode_loop_job*);

/* The reel a rendered job made: the frame rate the encoder took, the crossfade its stops kept, and one file per
   stop in order (NULL past the end). The paths live as long as the job. */
GEODE_API int         geode_loop_job_fps(geode_loop_job*);
GEODE_API int64_t     geode_loop_job_crossfade_ms(geode_loop_job*);
GEODE_API int         geode_loop_job_file_count(geode_loop_job*);
GEODE_API const char* geode_loop_job_file(geode_loop_job*, int index);

#ifdef __cplusplus
}
#endif

#endif
