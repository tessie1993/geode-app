#ifndef GEODE_VIDEO_EXPORT_API_H
#define GEODE_VIDEO_EXPORT_API_H

#include "api/geode_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One video export (core/output/VideoExport): the bit rate, the encoder it settles on, every frame's draw, time
   and timestamp, the drain into the MP4, the transcoded audio interleaved by timestamp, the progress, and what a
   cancel or a failure leaves. The shell fills the job in with the set calls, runs it on a worker thread and polls
   it from another; native never calls back. The file's row, its name and its fate are the shell's.

   Threads: the set calls and run on one thread (run holds the GL context it makes). cancel, progress and state
   from any thread, during the run too. Everything else after run has returned. destroy once no call is in
   flight. NULL when out of memory; every call takes NULL. */
typedef struct geode_video_export geode_video_export;
GEODE_API geode_video_export* geode_video_export_create(void);
GEODE_API void                geode_video_export_destroy(geode_video_export*);

/* The file to write (borrowed: open for reading and writing until run returns; the muxer takes its own
   duplicate), the frame, the bit rate and frame rate asked for, and the app's cache directory. */
GEODE_API void geode_video_export_set_output(geode_video_export*, int output_fd, const char* cache_dir, int width,
                                             int height, int bit_rate, int fps);
/* A codec to try, in the order added (two at most): its MIME type and the top frame rate the device lists for it
   at this size (<= 0: it did not say). */
GEODE_API void geode_video_export_add_codec(geode_video_export*, const char* mime, int top_fps);
/* The track the renderer hears: its descriptor, owned from here on (closed by destroy), the span its
   AssetFileDescriptor declares (length < 0: none, the whole descriptor) and where the range starts in it. */
GEODE_API void geode_video_export_set_track(geode_video_export*, int fd, int64_t offset, int64_t length,
                                            int64_t range_start_ms);
/* The renderer's inputs, as geode_viz_create and geode_export_job_begin take them. The asset manager must outlive
   the run. */
GEODE_API void geode_video_export_set_scene(geode_video_export*, AAssetManager* assets,
                                            const char* scene_id, int entitled);
/* The pinned look, copied, as geode_viz_set_look takes it; its scene_id is ignored (set_scene's is drawn). */
GEODE_API void geode_video_export_set_look(geode_video_export*, const geode_viz_look* look);
/* pixels NULL, or fewer than width * height of them (count): no image. */
GEODE_API void geode_video_export_set_underlay(geode_video_export*, const uint32_t* pixels, int count, int width,
                                               int height, int blend, float amount);
/* The wordmark's tile (tile_w * tile_h * 4 bytes, as geode_export_watermark_create takes it) and where it goes;
   placement 0 or rgba NULL: these frames carry none. */
GEODE_API void geode_video_export_set_mark(geode_video_export*, const uint8_t* rgba, int tile_w, int tile_h,
                                           int placement, int corner);

/* The audio track, already encoded: the scratch file its samples sit in (opened here, closed by destroy) and
   `count` samples as {offset, size, pts_us, flags} quadruples, in timestamp order. 0 when the file cannot be
   opened. This is the seam the native transcoder will fill in place of the shell's. */
GEODE_API int  geode_video_export_set_audio(geode_video_export*, const char* scratch_path, const int64_t* samples,
                                            int count);
/* One key of the audio track's format as the encoder named it, typed as MediaFormat types it: 1 int32 and
   2 int64 (whole), 3 float (real), 4 string (text), 5 buffer (bytes, byte_count). */
#define GEODE_FORMAT_INT32  1
#define GEODE_FORMAT_INT64  2
#define GEODE_FORMAT_FLOAT  3
#define GEODE_FORMAT_STRING 4
#define GEODE_FORMAT_BUFFER 5
GEODE_API void geode_video_export_add_audio_key(geode_video_export*, const char* key, int type, int64_t whole,
                                                float real, const char* text, const uint8_t* bytes, int byte_count);

/* Renders the whole file; returns when it is rendered, cancelled or failed. A job runs once. */
GEODE_API void  geode_video_export_run(geode_video_export*);
GEODE_API void  geode_video_export_cancel(geode_video_export*);
/* 0..1 across the render, the audio's transcode before it included (where it ended, before run). */
GEODE_API float geode_video_export_progress(geode_video_export*);
/* The bar while the shell transcodes the audio, `done` 0..1 of that step. Pure. */
GEODE_API float geode_video_export_lead_in(float done);

#define GEODE_VIDEO_EXPORT_IDLE      0
#define GEODE_VIDEO_EXPORT_RUNNING   1
#define GEODE_VIDEO_EXPORT_RENDERED  2
#define GEODE_VIDEO_EXPORT_CANCELLED 3
#define GEODE_VIDEO_EXPORT_FAILED    4
GEODE_API int geode_video_export_state(geode_video_export*);

/* Why a failed job failed; detail is the text the reason is shown with ("" when it has none). */
#define GEODE_VIDEO_FAILURE_NONE             0
#define GEODE_VIDEO_FAILURE_ENCODER_REJECTED 1 /* no codec took the frame at any rate */
#define GEODE_VIDEO_FAILURE_NO_OUTPUT        2 /* the encoder never announced its format */
#define GEODE_VIDEO_FAILURE_STALLED          3 /* the encoder never reached its end of stream */
#define GEODE_VIDEO_FAILURE_ENCODER_FAILED   4 /* the encoder refused its surface, its start or its end of input */
#define GEODE_VIDEO_FAILURE_EMPTY_BUFFER     5 /* the encoder handed out a sample with no buffer */
#define GEODE_VIDEO_FAILURE_STOPPED          6 /* anything else; detail is the message, or "" */
GEODE_API int         geode_video_export_failure(geode_video_export*);
GEODE_API const char* geode_video_export_detail(geode_video_export*);

#ifdef __cplusplus
}
#endif

#endif
