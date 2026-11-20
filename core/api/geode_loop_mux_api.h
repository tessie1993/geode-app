#ifndef GEODE_LOOP_MUX_API_H
#define GEODE_LOOP_MUX_API_H

#include "api/geode_api.h"
#include "api/geode_export_audio_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One loop extend's copy (core/output/LoopMux): the rendered loops' encoded video samples read back with
   AMediaExtractor and written into one MP4 once per repeat of the plan, each time shifted by whole loops, with the
   soundtrack's encoded clips interleaved by timestamp as the repeats go in; the progress, and what a cancel or a
   failure leaves. The shell fills it in, runs it on a worker thread and polls it from another; native never calls
   back. The file's row, its name, the size check and its fate are the shell's.

   Threads: the add calls and run on one thread (run blocks it). cancel, progress and state from any thread, during
   the run too. Everything else after run has returned. destroy once no call is in flight. NULL when out of memory;
   every call takes NULL. */
typedef struct geode_loop_mux geode_loop_mux;
GEODE_API geode_loop_mux* geode_loop_mux_create(void);
GEODE_API void            geode_loop_mux_destroy(geode_loop_mux*);

/* A rendered loop's file, in the order the repeat plan indexes the loops. Only its path is kept until run. */
GEODE_API void geode_loop_mux_add_loop(geode_loop_mux*, const char* path);

/* The next clip of the soundtrack: the finished transcode `job` (geode_export_audio_api.h, state done), whose samples
   and format are read out here, and the scratch file its samples sit in (opened here, closed by destroy). The job may
   be destroyed once this returns. The first clip fixes the track's format. */
#define GEODE_LOOP_MUX_CLIP_UNREADABLE 0 /* the job is not done, or the file cannot be opened */
#define GEODE_LOOP_MUX_CLIP_ADDED      1
#define GEODE_LOOP_MUX_CLIP_DIFFERS    (-1) /* another sample rate or channel count than the first clip: not added */
GEODE_API int geode_loop_mux_add_clip(geode_loop_mux*, const geode_aac_job* job, const char* scratch_path);

/* How long the soundtrack runs: the clips added so far, back to back (the running time of the long file). */
GEODE_API int64_t geode_loop_mux_total_us(geode_loop_mux*);

/* Copies the whole file into output_fd (borrowed: open for reading and writing until run returns; the muxer takes its
   own duplicate); loop_duration_us is how long each loop runs. Returns when it is done, cancelled or failed. A job
   runs once. */
GEODE_API void  geode_loop_mux_run(geode_loop_mux*, int output_fd, int64_t loop_duration_us);
GEODE_API void  geode_loop_mux_cancel(geode_loop_mux*);
/* 0..1 across the whole long-form build: the soundtrack's transcode takes its first share (the bar stands at its
   end before run), the copy the rest. */
GEODE_API float geode_loop_mux_progress(geode_loop_mux*);

#define GEODE_LOOP_MUX_IDLE      0
#define GEODE_LOOP_MUX_RUNNING   1
#define GEODE_LOOP_MUX_DONE      2
#define GEODE_LOOP_MUX_CANCELLED 3
#define GEODE_LOOP_MUX_FAILED    4
GEODE_API int geode_loop_mux_state(geode_loop_mux*);

/* Why a failed copy failed; detail is the text the reason is shown with ("" when it has none). */
#define GEODE_LOOP_MUX_FAILURE_NONE     0
#define GEODE_LOOP_MUX_FAILURE_UNUSABLE 1 /* the loops cannot make one file: detail is the words to show as they are */
#define GEODE_LOOP_MUX_FAILURE_READ     2 /* a loop or a soundtrack sample could not be read */
#define GEODE_LOOP_MUX_FAILURE_WRITE    3 /* the muxer would not open, start, take a sample or finish */
GEODE_API int         geode_loop_mux_failure(geode_loop_mux*);
GEODE_API const char* geode_loop_mux_detail(geode_loop_mux*);

#ifdef __cplusplus
}
#endif

#endif
