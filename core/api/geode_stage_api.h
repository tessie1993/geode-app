#ifndef GEODE_STAGE_API_H
#define GEODE_STAGE_API_H

#include "api/geode_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The native stage (core/stage): one surface's render thread, EGL context and vsync.
   The shell hands over a window, its size, what it sees and its pause; it reads back one state word and a few
   numbers. Native never calls up into the shell. Every call may come from any thread; only set_window waits. */

/* A handle is never reused, and every call on one that is gone (destroyed, or never made) does nothing and
   answers 0, from any thread. */
typedef struct geode_stage geode_stage;
struct ANativeWindow;

/* What the stage draws with, bound to a renderer by native code (never by the shell). Called on the stage thread
   only. surface_created, surface_changed, frame and trim_spares run with the context current; context_lost with
   none (forget every GL name, delete none); release is the last call, gl_current saying whether a context is
   current. frame returns event bits the shell reads with geode_stage_take_events. The stage owns `user` from
   create: after release it never touches it again. */
typedef struct geode_stage_client {
    void*    user;
    void     (*surface_created)(void* user);
    void     (*surface_changed)(void* user, int width, int height);
    unsigned (*frame)(void* user, float paced_fps, float cap_fps);
    void     (*trim_spares)(void* user);
    void     (*context_lost)(void* user);
    void     (*release)(void* user, int gl_current);
} geode_stage_client;

/* A view stage keeps its surface while hidden (the app's view); a surface stage pauses while its rule says
   stopped. */
#define GEODE_STAGE_KIND_VIEW                0
#define GEODE_STAGE_KIND_SURFACE             1

/* What the shell sees of the app's view: it moves while all four hold. */
#define GEODE_STAGE_VIEW_ATTACHED            (1u << 0)
#define GEODE_STAGE_VIEW_WINDOW_VISIBLE      (1u << 1)
#define GEODE_STAGE_VIEW_SHOWN               (1u << 2)
#define GEODE_STAGE_VIEW_OPAQUE              (1u << 3)   /* alpha above 0 */

/* The state word. The run mode (0 stopped, 1 still, 2 animated, 3 dark) is in bits
   GEODE_STAGE_STATE_MODE_SHIFT and up, two bits. */
#define GEODE_STAGE_STATE_WINDOW             (1u << 0)
#define GEODE_STAGE_STATE_CONTEXT            (1u << 1)
#define GEODE_STAGE_STATE_SURFACE            (1u << 2)
#define GEODE_STAGE_STATE_DRAWABLE           (1u << 3)
#define GEODE_STAGE_STATE_VSYNC              (1u << 4)
#define GEODE_STAGE_STATE_PAUSED             (1u << 5)
#define GEODE_STAGE_STATE_REFUSED            (1u << 6)   /* no ES 3 context on this window */
#define GEODE_STAGE_STATE_SURFACE_BAD        (1u << 7)   /* EGL refused this window; a new one is tried */
#define GEODE_STAGE_STATE_RELEASED           (1u << 8)
#define GEODE_STAGE_STATE_MODE_SHIFT         12

/* NULL when the client lacks a function or the thread could not start. cap_fps is the surface's own fps cap
   (Settings › Performance's, GEODE_LIVE_FPS_FOLLOW; 0 every vsync). */
GEODE_API geode_stage* geode_stage_create(int kind, const geode_stage_client* client, float cap_fps);
/* A window (from ANativeWindow_fromSurface; the stage takes its own reference, the caller keeps and releases
   its own) or NULL when the surface is destroyed. Returns at once, the stage applying it between frames: call it
   from surfaceCreated and, with NULL, from surfaceDestroyed. The EGL surface on the old window may outlive the call
   for a frame. The stage holds its own window reference, so that late destroy is safe, and
   GEODE_STAGE_STATE_SURFACE clears when it has happened. */
GEODE_API void     geode_stage_set_window(geode_stage*, struct ANativeWindow* window, int width, int height);
GEODE_API void     geode_stage_set_size(geode_stage*, int width, int height);
/* The app's view: GEODE_STAGE_VIEW_* bits. */
GEODE_API void     geode_stage_set_view(geode_stage*, unsigned view_flags);
/* The host's pause and resume (the view's onPause and onResume). */
GEODE_API void     geode_stage_pause(geode_stage*);
GEODE_API void     geode_stage_resume(geode_stage*);
/* Whether the shell's tilt sensor should run now: `wanted` (the user's setting) on a view somebody can see in
   a host that is not paused. Asked after every set_view, pause, resume and change of the setting. When the
   answer changes, the renderer's tilt session ends here, so the next reading starts a new one. */
GEODE_API int      geode_stage_gyro_listens(geode_stage*, int wanted);
/* surfaceRedrawNeeded: one frame now, even while the vsync is stopped. */
GEODE_API void     geode_stage_redraw(geode_stage*);
GEODE_API void     geode_stage_set_cap(geode_stage*, float cap_fps);
GEODE_API void     geode_stage_set_live_fps(geode_stage*, int stored_fps, const float* refresh_rates, int count);
GEODE_API unsigned geode_stage_state(const geode_stage*);
/* The stage's frame budget in ns: one frame's length for the rate setting on the display mode now running. The swap
   interval, the frame-rate vote's pace and the work target follow this one number; 0 until the pacer has said. */
GEODE_API long long geode_stage_frame_budget_ns(const geode_stage*);
/* Android Frame Pacing (Swappy) is made from an Activity and paces the app's live stage: attach with the Activity's
   JNIEnv* and jobject in its onCreate and detach with them in its onDestroy. A stage with no Swappy is paced by
   Choreographer, one frame per vsync. */
GEODE_API void     geode_stage_pacing_attach(void* env, void* activity);
GEODE_API void     geode_stage_pacing_detach(void* env, void* activity);
/* A new file descriptor, readable while frame events wait and until geode_stage_take_events takes them, for the
   shell's main looper to sleep on; the caller owns and closes it. -1 when there is none. */
GEODE_API int      geode_stage_event_fd(const geode_stage*);
/* The frame events since the last call, OR-ed. */
GEODE_API unsigned geode_stage_take_events(geode_stage*);
/* Quits the stage: the client is released on the stage thread, with its context current where one can be.
   Waits at most 200 ms in all; a stage still busy then finishes on its own. The handle is gone either way. */
GEODE_API void     geode_stage_destroy(geode_stage*);

/* A live stage: one that owns its renderer (a geode_viz made here from the assets and the shader cache folder)
   and frees it at its release. This is the app's view.
   NULL when the renderer could not be made. The assets must outlive the stage. */
GEODE_API geode_stage* geode_stage_create_live(int kind, AAssetManager* assets, const char* cache_dir,
                                               float cap_fps);
/* The renderer for one any-thread geode_viz_* call (the ones geode_api.h marks "any thread", and the queries),
   or NULL once the stage released it. Every non-NULL answer is followed by geode_stage_viz_unlock on the same
   thread; the stage's release waits for it. */
GEODE_API geode_viz* geode_stage_viz_lock(geode_stage*);
GEODE_API void     geode_stage_viz_unlock(void);
/* The ring frames hear, or NULL. Never waits for a frame: one in flight keeps the ring it began with. The stage holds
   the ring until it hears another and that frame ends, so its owner may destroy it on return. */
GEODE_API void     geode_stage_set_sound(geode_stage*, const geode_pcm_ring* ring);

/* The user's look, as geode_viz_set_look takes it: only the parts that differ from the look set last reach the
   stage's renderer. */
GEODE_API void     geode_stage_set_look(geode_stage*, const geode_viz_look* look);
/* Builds a transition's program on the stage thread, once it has a context. */
GEODE_API void     geode_stage_warm_transition(geode_stage*, const char* id);

/* Record's tee into `session`'s encoder, opened on the stage thread. `tile` (premultiplied RGBA8, top row
   first, copied here) is the mark drawn over each copied frame when `placement` is not 0. When the tee cannot
   open (no context, the driver or the mark refused, the stage gone) the recorder is stopped as failed; a stage
   that is gone also tells the session its tee is closed. 1 when the stage took the ask. */
GEODE_API int      geode_stage_record_start(geode_stage*, geode_recorder* recorder, geode_record_session* session,
                                            const uint8_t* tile, int tile_width, int tile_height, int placement,
                                            int corner);
/* Closes the tee on the stage thread and tells the session; the stage's release does the same for a tee left
   open, so no recording waits on a stage that is gone. */
GEODE_API void     geode_stage_record_stop(geode_stage*);
/* 1 when no tee is open or waiting to open (always for a stage that is gone): the recorder and its session
   may be freed. */
GEODE_API int      geode_stage_record_closed(const geode_stage*);

/* What frames reported, kept until read. The texts answer their length and copy at most capacity - 1 bytes
   and a NUL. */
GEODE_API size_t   geode_stage_error(const geode_stage*, char* out, size_t capacity);
GEODE_API int      geode_stage_take_director_moments(geode_stage*);
GEODE_API int      geode_stage_liquid_mode(const geode_stage*);
/* The still a frame read after geode_viz_still_request (GEODE_FRAME_STILL_READY): its size (1 when one waits),
   then its RGBA8 bytes, top row first, handed out once (1 when copied; capacity below width * height * 4
   copies nothing and keeps it). */
GEODE_API int      geode_stage_still_size(geode_stage*, int* width, int* height);
GEODE_API int      geode_stage_still_read(geode_stage*, void* out, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
