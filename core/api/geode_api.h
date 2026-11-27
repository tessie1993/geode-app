#pragma once
#include <stddef.h>
#include <stdint.h>
#include <android/asset_manager.h>

#include "sound/SoundFrame.h"
#ifdef __cplusplus
extern "C" {
#endif
#define GEODE_API __attribute__((visibility("default")))

typedef struct geode_viz      geode_viz;
typedef struct geode_dsp      geode_dsp;
typedef struct geode_tags     geode_tags;
typedef struct geode_pcm_ring geode_pcm_ring;   /* the audio ring (declared again with its calls below) */

/* Native visualizer: one renderer per GL surface. Setters may be called from any thread and are latched
 * for the next frame; the calls under "GL thread" need the surface's context current. */
#define GEODE_LFO_SLOTS 3
#define GEODE_LFO_CONFIG_FLOATS 11  /* enabled, source, target, wave, rate_seconds, depth, polarity, curve, beats (0 = free; a shorter array of 8 stays free),
                                       knob (the macro knob's id a MACRO target turns; 0 none), moment (-1 free, else the Moment that restarts it) */
#define GEODE_ADSR_SLOTS 2
#define GEODE_REACT_ROUTES 256 /* the matrix's slots: the style's routes, the user's and the Event Responder's compiled responses */
#define GEODE_REACT_ROUTE_FLOATS 8  /* enabled, source, target, mode, amount, curve, attack_seconds, release_seconds */
#define GEODE_ADSR_CONFIG_FLOATS 12 /* enabled, attack, decay, sustain, release, amount, band, gate_threshold,
                                       sustain_track, retrigger, knob (as the LFO's), moment (-1 a note start, else the
                                       Moment that fires it); followed by the target ordinals */

GEODE_API geode_viz*  geode_viz_create(AAssetManager* assets, const char* cache_dir);
GEODE_API void        geode_viz_destroy(geode_viz*);
GEODE_API int         geode_viz_param_count(void);
GEODE_API const char* geode_viz_param_name(int index);   /* NULL past the end */
/* Every scene parameter in geode_viz_param_name order; ints and flags travel as floats. */
GEODE_API void        geode_viz_set_params(geode_viz*, const float* values, int count);
GEODE_API void        geode_viz_set_reduced_motion(geode_viz*, int on);
GEODE_API void        geode_viz_set_transition(geode_viz*, const char* id, int64_t duration_ms);
GEODE_API void        geode_viz_begin_param_morph(geode_viz*, float seconds);
GEODE_API void        geode_viz_set_touch(geode_viz*, const float* xy_ndc, int points);   /* 0 points = all lifted */
/* Settings › Gestures & controls › Touch › Smear (core/viz/input/TouchSmear): on, the fingers touch play holds bend the
 * picture through the flow field, at strength 0.2..2 (clamped; a broken value is 1). Live renderers only. Any thread. */
GEODE_API void        geode_viz_set_touch_smear(geode_viz*, int on, float strength);
/* GLSL for the fluid style's force and dye injection; "" restores the built-in splat. */
GEODE_API void        geode_viz_set_fluid_injection(geode_viz*, const char* force_source, const char* dye_source);
/* Section changes and drops the auto-director committed a look at since the last call; handed out once. */
GEODE_API int         geode_viz_take_director_style_moments(geode_viz*);
/* The auto-director (core/viz/director/Director): the music's feeling deciding behaviour (its axes, camera moves,
 * motion dials) over every style; never a colour, never a route. on is the user's switch as set (1/0), which
 * an export directs by (geode_viz_pin_engine_settings);
 * amount 0 off .. 1 full is what live surfaces drive with now, eased in and out by each renderer; seed is the
 * play's, so a new seed is a new play (the renderer forgets the song so far) and an export pinned with it
 * makes the same film. */
GEODE_API void        geode_director_set(int on, float amount, long long seed);
/* What live surfaces drive with (core/viz/director/Director, director::liveAmount): 0 or 1 from the user's switch (on),
 * the entitlement (entitled: Pro or the trial, 1/0), the song's position and whether the app's own player is
 * playing. Free gets the first minute of a song while the player plays; the mic and other apps' audio get none.
 * Pass the result to geode_director_set. */
GEODE_API float       geode_director_live_amount(int on, int entitled, long long position_ms, int playing);
/* The director's reach past its look (core/viz/director/DirectorReach), process-wide like geode_director_set and pinned
 * with it by an export. level: 0 the look alone, 1 + it shuffles Customize categories at a new section or a
 * drop, 2 + its own slow modulators on sliders; anything else is 0. Nothing happens while the director is off. */
GEODE_API void        geode_director_reach_set(int level);
/* Customize's keep set, which the reach never moves: one control key or "#CATEGORY" per line (the lock set
 * geode_params_randomize takes). Null or "" keeps nothing. */
GEODE_API void        geode_director_keep_set(const char* keys);
/* The Auto Director's settings (core/viz/director/DirectorTuning): every number it tunes itself by, as a table native
 * lists. The shell stores the text native gives it and draws the rows as given. set: process-wide like
 * geode_director_set and pinned with it by an export (null or "" is every default). count: the rows; label,
 * help: row i's, static ("" outside the table); range: writes min, max, step (0 continuous) and 1 for a
 * switch (an on/off row) or 0 into out, returns 0 outside the table; value: row i in a saved text; value_text:
 * as the page shows it; with: the saved text with row i set to value, held to its range. The returned
 * texts are valid until this thread's next tuning call. */
GEODE_API void        geode_director_tuning_set(const char* text);
GEODE_API int         geode_director_tuning_count(void);
GEODE_API const char* geode_director_tuning_label(int index);
GEODE_API const char* geode_director_tuning_help(int index);
GEODE_API int         geode_director_tuning_range(int index, float* out4);
GEODE_API float       geode_director_tuning_value(const char* text, int index);
GEODE_API const char* geode_director_tuning_value_text(const char* text, int index);
GEODE_API const char* geode_director_tuning_with(const char* text, int index, float value);
/* Auto's pick at a director moment or a timer tick (core/viz/director/DirectorStyle, core/viz/director/AutoDraw): ids is the
 * shell's pool, one style id a line in its order (a saved look by its style's id);
 * on_screen the entry showing (-1 none). With director_moment, the style the moment's director asks for: a
 * returning section's family stepped on by its returns, else the next style along the catalogue; -1 to
 * keep the style on screen. Without one (or with no intent waiting) Auto's random draw, as geode_auto_pick. */
GEODE_API int         geode_auto_pick_among(const char* ids, int on_screen, int director_moment);
/* Customize › Advanced › Layers (core/viz/LayerState, core/viz/LayerPick): a second style over the main one,
 * process-wide like geode_director_set, so live and export both draw it. Native keeps the
 * layer; the shell only saves the line edit hands back ("layer|1|<on 0/1>|<SOURCE>|<style id>|<knob id>|<blend>|
 * <mix>") and hands it to set at the next start. Where its style comes from (GEODE_LAYER_SOURCE_USER, the user's
 * pick or one native picks to suit the style on screen; _DIRECTOR, Auto Director's pick at its style moments;
 * _MACRO, a knob's turn) is turned into a style by each renderer.
 * set: the saved line (any other text is the default layer, off); every renderer draws it from its next frame,
 *   and an export pins the layer it started with (geode_viz_set_offscreen).
 * edit: one field of the layer changed, `field` GEODE_LAYER_ON..GEODE_LAYER_STYLE: ON 0/1, SOURCE a
 *   GEODE_LAYER_SOURCE_*, KNOB a place in the KNOBS list, BLEND a place in the BLENDS list (Screen,
 *   GEODE_LAYER_BLEND_DEFAULT, for a new layer), MIX 0..1, STYLE `style_id` ("" for native's pick). Returns the
 *   line to save, "" when nothing changed.
 * values: what Customize › Layers draws over active_style, out[GEODE_LAYER_VALUES] by GEODE_LAYER_*: ON, SOURCE,
 *   KNOB, BLEND and MIX as edit takes them; FAMILY the style picker's family, a place in the FAMILIES list (`family`
 *   as the page holds it, or -1 for the one holding the style drawn now); STYLE the picked style's place in that
 *   family's STYLES (-1 none); PICKED 1 when the user picked a style. Returns the count written, 0 when it cannot.
 * list: a GEODE_LAYER_LIST_* list over active_style, each entry followed by '\n': the sources' labels, the blends'
 *   labels, the Styles page families with a style other than active_style (by name), `family`'s styles but
 *   active_style (by id; `family` as values takes it), the knobs on screen (by name).
 * shown: the style the layer draws now over active_style, "" for none.
 * The returned texts are valid until this thread's next layer call. */
#define GEODE_LAYER_ON 0
#define GEODE_LAYER_SOURCE 1
#define GEODE_LAYER_KNOB 2
#define GEODE_LAYER_BLEND 3
#define GEODE_LAYER_MIX 4
#define GEODE_LAYER_STYLE 5
#define GEODE_LAYER_FAMILY 6
#define GEODE_LAYER_PICKED 7
#define GEODE_LAYER_VALUES 8
#define GEODE_LAYER_SOURCE_USER 0
#define GEODE_LAYER_SOURCE_DIRECTOR 1
#define GEODE_LAYER_SOURCE_MACRO 2
#define GEODE_LAYER_LIST_SOURCES 0
#define GEODE_LAYER_LIST_BLENDS 1
#define GEODE_LAYER_LIST_FAMILIES 2
#define GEODE_LAYER_LIST_STYLES 3
#define GEODE_LAYER_LIST_KNOBS 4
#define GEODE_LAYER_BLEND_DEFAULT 1 /* Screen, the blend a new layer starts on (core/viz/modulation/VisualSafety) */
GEODE_API void        geode_layer_set(const char* layer);
GEODE_API const char* geode_layer_edit(int field, float value, const char* style_id);
GEODE_API int         geode_layer_values(const char* active_style, int family, float* out, int capacity);
GEODE_API const char* geode_layer_list(int list, const char* active_style, int family);
GEODE_API const char* geode_layer_shown(const char* active_style);
/* The renderer's own sound engine: interleaved PCM in play order, every sample once, written on the render
 * thread before geode_viz_render; the next render steps it with that frame's dt. */
GEODE_API void        geode_viz_write_audio(geode_viz*, const float* interleaved, int frames, int channels, int sample_rate);
GEODE_API void        geode_viz_set_sound_tuning(geode_viz*, const float* saved, int count);  /* a saved tuning, resolved as geode_sound_tuning_resolve */
/* Customize › Advanced › Shader code (core/viz/glsl/ShaderOverrides): the user's fragment source for a style, process-wide
 * like geode_director_set, so live and export both draw it. A null or empty source clears it
 * (the built-in style again). Every renderer applies it at scene build and to its resident scenes on the next frame. */
GEODE_API void        geode_shader_override_set(const char* style_id, const char* fragment_source);
/* The style's override; returns its full length, 0 when it draws the built-in style. */
GEODE_API size_t      geode_shader_override(const char* style_id, char* out, size_t capacity);
/* Customize › Music, "the hand wins per moment" (core/viz Reactions.hpp hand::, ReactionDna.hpp). The hand mask
   is bit i for moment i (GEODE_MUSIC_MOMENTS, in the page's order) plus GEODE_HAND_UNCLAIMED for every source
   no moment claims; GEODE_HAND_ALL is a whole list of the user's (the route editor, a list saved before the
   mask). The routes themselves, and every read and edit of them, are api/geode_modulation_api.h's.
   geode_hand_set is process-wide like geode_director_set: every renderer's director keeps off those moments.
   has_user 0 (no list of the user's: the DNA plays) makes the mask count as 0, whatever its bits. */
#define GEODE_MUSIC_MOMENTS 10
#define GEODE_MUSIC_ACTIONS 6
#define GEODE_HAND_UNCLAIMED 0x40000000u
#define GEODE_HAND_ALL 0x400003FFu
GEODE_API void        geode_hand_set(unsigned mask, int has_user);
/* Modulation targets (core/viz/modulation/ModTargets, #642): bit per Music action whose target reaches `style_id`, so the
   shell hides a chip the style ignores. */
GEODE_API unsigned    geode_music_actions_for(const char* style_id);
/* The phone's tilt for the camera's "Tilt phone to look" (viz/input/GyroLook): one game rotation vector reading
   (quaternion x, y, z, w) on a screen at display_rotation (Surface.ROTATION_*, 0..3), latched for the next
   geode_viz_frame. The first reading after an off (or on a new handle) starts a session, whose first frame
   recentres and takes it as the baseline; the tilt from it is the -1..1 look per axis the camera reads only
   while camGyro is on. geode_viz_gyro_off: the sensor stopped; the next frame recentres the look to (0,0),
   once. Any thread. */
GEODE_API void        geode_viz_gyro_push(geode_viz*, float x, float y, float z, float w, int display_rotation);
GEODE_API void        geode_viz_gyro_off(geode_viz*);
/* The style a live surface draws (any thread, latched): each geode_viz_frame asks the renderer for the latest
   request until the renderer can draw it, and then not again. Also what the reaction matrix deals the DNA
   of. An export, which never calls geode_viz_frame, sets its scene with geode_viz_set_scene. */
GEODE_API void        geode_viz_request_scene(geode_viz*, const char* scene_id);
/* The user's look as the shell hands it, whole (core/viz/looks/LookSnapshot). lfos, adsrs and reactions are the saved
   texts geode_viz_set_lfos, _set_adsrs and _set_reactions read (NULL lfos or adsrs: none saved; NULL reactions:
   the style's own routes); tuning is the saved sound tuning, none for the engine's defaults. */
typedef struct geode_viz_look {
    const char*  scene_id;
    const float* params;
    int          param_count;
    const char*  lfos;
    const char*  adsrs;
    const float* tuning;
    int          tuning_count;
    const char*  reactions;
    int          reduced_motion;
    const char*  transition_id;
    long long    transition_ms;
} geode_viz_look;
/* Any thread. Only the parts that differ from the look set last on this renderer reach it (all of them on a new
   renderer), so a transition or a scene set again restarts nothing. The scene is a request, as above. */
GEODE_API void        geode_viz_set_look(geode_viz*, const geode_viz_look* look);
/* The look a renderer starts on before one is set: "<scene id>\n<transition id>\n<transition ms>\n", then the
   shell's visual defaults a line each: attack, decay, style timer s, playlist interval s, preset morph max s.
   Static. geode_viz_set_look holds transition_ms to 300..5000 (viz/looks/LookSnapshot). */
GEODE_API const char* geode_viz_look_defaults(void);
/* Settings › Performance for a live surface (any thread, latched; the next geode_viz_frame applies it when it
   changed, and again on a new surface): scales 0..1, post_fx 0 or 1. Never set, the renderer keeps full
   quality (an export). */
GEODE_API void        geode_viz_set_quality(geode_viz*, float render_scale, float march_scale, int post_fx);
/* One whole live frame (core/viz/frame/LiveFrame), on the GL thread with the context current, into
   framebuffer 0; the shell swaps after it. In order: paced_fps (the rate this surface's pacer draws at,
   0 = free-running) and cap_fps (the surface's own cap; the same as paced_fps where
   only a fixed rate paces it, Cast's) to the renderer; the latched quality; the latched gyro; the requested scene; `ring` heard (geode_viz_feed_ring, NULL
   hears nothing); geode_viz_render at CLOCK_MONOTONIC.
   Answers the GEODE_FRAME_* events; the shell acts on a bit only when it is set. 0 for a null handle. */
#define GEODE_FRAME_LIQUID_CHANGED  0x4  /* Liquid mode changed: geode_viz_liquid_mode */
#define GEODE_FRAME_ERROR_CHANGED   0x8  /* the error text changed, or this is the handle's first frame: geode_viz_last_error */
#define GEODE_FRAME_DIRECTOR_MOMENT 0x20 /* director moments wait: geode_viz_take_director_style_moments */
#define GEODE_FRAME_STILL_READY     0x40 /* the still asked for is ready: geode_viz_still_size / _read */
GEODE_API int         geode_viz_frame(geode_viz*, const geode_pcm_ring* ring, float paced_fps, float cap_fps);
/* What Liquid mode ran when a frame last said GEODE_FRAME_LIQUID_CHANGED: 0 off, 1 full (liquid::Mode). Any thread. */
GEODE_API int         geode_viz_liquid_mode(geode_viz*);
GEODE_API void        geode_viz_set_offscreen(geode_viz*, int on);
/* Export: step every frame by exactly 1/fps instead of measuring the time between frames (#377); 0 = live. */
GEODE_API void        geode_viz_set_frame_rate(geode_viz*, int fps);
/* Frosted glass under the app's sheet (core/viz/passes/UiBlurPass): blurs the finished frame inside one rounded
 * rect, after the composite. The rect is normalised to the viewport with y DOWN (a view's left/top/right/
 * bottom in pixels over the visual's width/height); strength 0..1 mixes the blur in, 0 turns it off and
 * costs nothing; corner_px is the corner radius in surface pixels. Any thread, latched for the next frame.
 * An offscreen (export) renderer never draws it. */
GEODE_API void        geode_viz_set_ui_blur_region(geode_viz*, float left, float top, float right, float bottom,
                                                   float strength, float corner_px);
/* An export's engine settings (signal::SettingsLatch, #350), once at prepare: from now on this renderer never
 * reads geode_macro_bank_set, geode_hand_set or geode_director_set again, so live
 * edits, the player's next track and its free taste never reach the render. The macro bank
 * and the hand mask are copied as they are now; the director is the user's switch and the play's seed, full for the
 * whole export when entitled (Pro or the trial, 1/0) or when duration_ms fits the free taste, else off
 * (director::forExport). Returns the pinned hand mask,
 * the one the export's reactions (geode_viz_set_reactions) then play under; 0 for a null handle. */
GEODE_API unsigned    geode_viz_pin_engine_settings(geode_viz*, int entitled, long long duration_ms);
GEODE_API int         geode_viz_knows(geode_viz*, const char* scene_id);
GEODE_API size_t      geode_viz_scene_ids(geode_viz*, char* out, size_t capacity);   /* newline-joined; returns the full length */
GEODE_API const char* geode_viz_last_error(geode_viz*);   /* "" when clean */
/* Full-frame RGBA8 underlay blended UNDER/INTO the scene, before the composite's own post-FX:
 * blend 0 = screen (replaces the scene where it is black), 1 = multiply, 2 = add; amount 0..1 is how
 * much of that blend result replaces the plain scene colour. `pixels` is Android's Bitmap.getPixels
 * ARGB_8888 layout (0xAARRGGBB per int), row-major, width*height entries; NULL or a 0x0 size clears it. */
GEODE_API void        geode_viz_set_underlay_rgba(geode_viz*, const uint32_t* pixels, int width, int height, int blend, float amount);
/* GL thread. */
GEODE_API void        geode_viz_surface_created(geode_viz*);
GEODE_API void        geode_viz_surface_changed(geode_viz*, int width, int height);
GEODE_API int         geode_viz_set_scene(geode_viz*, const char* scene_id);   /* 0 = native cannot draw it */
GEODE_API void        geode_viz_warm_transition(geode_viz*, const char* id);
/* Forgets the drawn scene so the next frame cuts instead of transitioning from it. */
GEODE_API void        geode_viz_cut(geode_viz*);
/* Export (Studio 2D): the styles this segment switches to. `count` timed cues (render ms since the
 * segment began, style id) and a beat cut stepping through
 * `cut_count` style ids every `every_beats` beats of the renderer's own sound engine. Both empty
 * clears it. `locked_ids` is the entitlement: the `locked_count` styles this user may not draw (none
 * for Pro); cues and cut ids naming one are dropped. Strings are copied. */
GEODE_API void        geode_viz_set_style_schedule(geode_viz*, const double* at_ms, const char* const* scene_ids,
                                                   int count,
                                                   const char* const* cut_ids, int cut_count, int every_beats,
                                                   const char* const* locked_ids, int locked_count);
GEODE_API void        geode_viz_render(geode_viz*, double time_seconds, uint32_t target_fbo);
/* Record's tee (core/viz/frame/RecordTap), render thread with the stage's context current. From the next
 * geode_viz_render on, each frame the recorder's clock keeps is copied, after the composite and the edge
 * glow and before the UI blur, into `native_window` (the video encoder's input surface as an ANativeWindow,
 * width x height), stamped with the clock's PTS; and every block geode_viz_feed_ring hands the sound engine
 * goes to the recorder too. `watermark` (a geode_export_watermark_create pass built on this thread, or
 * NULL) is drawn over each copied frame. 1 when the surface opened, 0 when the driver refused it. The
 * recorder and the pass must outlive the tee; the window must until geode_viz_record_stop, which is safe
 * with no context current (the surface went first) and when nothing records. */
typedef struct geode_recorder geode_recorder;
typedef struct geode_export_pass geode_export_pass;
GEODE_API int         geode_viz_record_start(geode_viz*, const geode_recorder* recorder, void* native_window, int width,
                                             int height, geode_export_pass* watermark);
GEODE_API void        geode_viz_record_stop(geode_viz*);
/* Cast's tee (core/viz/frame/CastTee): the stream to a TV or a browser carries what the stage draws, not a second
 * render. A renderer marked as the cast's source (the app's stage only; any thread, kept until changed) copies
 * its finished visual, at the same point as Record's tee, into the running cast's tee at most once per cast frame.
 * The cast thread opens the tee with its own EGL context current (width x height, the stream's frame and fps;
 * NULL when the driver lacks what it needs, and the cast then renders on its own), and each frame slot calls
 * geode_cast_tee_draw, which blits the stage's newest copy into framebuffer 0 and returns 1, or returns 0 when
 * the stage has drawn nothing for a quarter of a second (the app in the background): the cast's own renderer
 * draws that frame. Close on the cast thread, its context still current. */
typedef struct geode_cast_tee geode_cast_tee;
GEODE_API void            geode_viz_set_cast_source(geode_viz*, int on);
GEODE_API geode_cast_tee* geode_cast_tee_open(int width, int height, int fps);
GEODE_API int             geode_cast_tee_draw(geode_cast_tee*, int64_t now_ns, int width, int height);
GEODE_API void            geode_cast_tee_close(geode_cast_tee*);
/* 1 while a new stream should leave this frame slot empty for the stage's first copy instead of drawing on its own
 * renderer: the shell says the stage draws (GEODE_STAGE_DRAWS), that renderer is not made yet and the tee opened
 * since_open_ns ago, under 3 s. Pure (viz/frame/CastTeeRule). */
GEODE_API int             geode_cast_tee_await_stage(int stage_draws, int fallback_made, int64_t since_open_ns);
/* Shake to screenshot's picture: the next frame's finished visual (after the composite and the edge glow,
 * before the UI blur, as Record's tee sees it) is read back once, rows top first, RGBA8. Any thread.
 * geode_viz_still_size gives its width and height (1 once it is ready, 0 before); geode_viz_still_read copies
 * it into `out` (at least width*height*4 bytes) and lets it go. GL thread for both. */
GEODE_API void        geode_viz_still_request(geode_viz*);
GEODE_API int         geode_viz_still_size(geode_viz*, int* width, int* height);
GEODE_API int         geode_viz_still_read(geode_viz*, void* out, size_t capacity);
/* Tap along (core/viz/input/TapAlong): a tap's touch-down at uptime_ns (CLOCK_MONOTONIC, MotionEvent's clock) against
 * the beat this renderer's sound engine heard on its last frame. 1 on the tap that completes kTaps on the beat in
 * a row. Read only: the beat tracking never hears the taps. Any thread. */
GEODE_API int         geode_viz_tap_along(geode_viz*, int64_t uptime_ns);
/* Shake to screenshot's detector (core/viz/input/Shake): one per listener. geode_shake_sample takes one
 * TYPE_ACCELEROMETER event (its timestamp in ns, x, y, z in m/s²) and is 1 on the sample that completes a
 * shake. geode_shake_reset forgets the motion (the sensor was off). One thread per handle. */
typedef struct geode_shake geode_shake;
GEODE_API geode_shake* geode_shake_create(void);
GEODE_API void         geode_shake_destroy(geode_shake*);
GEODE_API void         geode_shake_reset(geode_shake*);
GEODE_API int          geode_shake_sample(geode_shake*, int64_t time_ns, float x, float y, float z);
/* Offline (export) render: prepare pins the renderer for it, any thread: the offscreen tier
 * (geode_viz_set_offscreen(1)), cut transitions, and fixed fluid quality on every later set_params. */
GEODE_API void        geode_viz_prepare_offscreen(geode_viz*);
GEODE_API void        geode_viz_release_scenes(geode_viz*);
/* The surface is about to stop drawing (the host pauses it): sheds this renderer's spare
 * scenes now, exactly as a TRIM_MEMORY_UI_HIDDEN trim does (geode_memory_trim), because that signal is only read
 * by a frame and a paused surface draws none. What is drawn, the layer, the born and the requested style stay.
 * GL thread, while the context is still current; without one it does nothing. */
GEODE_API void        geode_viz_trim_spares(geode_viz*);

/* Playback DSP: gain -> 10-band equalizer -> loudness makeup -> crossfeed on float
 * interleaved PCM, one chain per input format, switched on the Media3 AudioProcessor contract (audio/dsp/DspProcessor):
 * configure stages the next format's chain, and the old one keeps processing the draining tail until flush
 * makes the staged one live. Setters may be called from any thread and reach every chain; configure, flush
 * and process run on the playback thread, and process never allocates, blocks or logs. */
#define GEODE_DSP_BANDS 10
#define GEODE_DSP_MIN_MILLIBELS (-1500)
#define GEODE_DSP_MAX_MILLIBELS 1500
/* The sound effects a range is asked for (audio/dsp/FxRanges): a band level in millibels, the bass boost in
 * permille, the loudness makeup in millibels. */
#define GEODE_DSP_FX_BAND 0
#define GEODE_DSP_FX_BASS_BOOST 1
#define GEODE_DSP_FX_LOUDNESS 2

GEODE_API geode_dsp* geode_dsp_create(void);
GEODE_API void       geode_dsp_destroy(geode_dsp*);
/* Stages the chain for the format queued after the next flush; 0 when it cannot run it (not 1 or 2
 * channels), and that format then passes through untouched. */
GEODE_API int        geode_dsp_configure(geode_dsp*, int sample_rate, int channels);
/* 1 when the chain runs a format: `encoding` 16-bit or float (numbered as Android's ENCODING_PCM_*, see
 * GEODE_PCM_S16), 1 or 2 channels. 0 for any other; the shell then steps aside and the track passes through. */
GEODE_API int        geode_dsp_runs(int encoding, int channels);
/* The shell's configure: geode_dsp_configure for a format geode_dsp_runs takes, else pass-through staged so the
 * next flush never leaves the old format's chain live. 1 when the chain runs it; 0 (and for a NULL dsp) when the
 * shell's processor steps aside. */
GEODE_API int        geode_dsp_configure_input(geode_dsp*, int encoding, int sample_rate, int channels);
GEODE_API void       geode_dsp_flush(geode_dsp*);                              /* commits, then clears filter state */
GEODE_API float      geode_dsp_band_center_hz(int band);
/* The built-in equalizer curves (audio/dsp/EqPresets). The settings store a curve by its index, so the order
 * is saved data. _levels writes GEODE_DSP_BANDS millibel levels, low band first, and returns that count; 0,
 * writing nothing, for no such curve or a capacity below GEODE_DSP_BANDS. */
GEODE_API int        geode_dsp_eq_preset_count(void);
GEODE_API int        geode_dsp_eq_preset_levels(int preset, int32_t* out, int capacity);
/* Pure, any thread. A control's range and a value held to it (0 for a control that is not one); and the band
 * levels a session starts on: the saved curve's when `preset` names one, else the `stored_count` saved levels
 * (NULL for none), a band with neither at 0, each held to the band range. _restore_bands writes
 * GEODE_DSP_BANDS levels and returns that count; 0, writing nothing, for a capacity below it. */
GEODE_API int        geode_dsp_band_count(void);
GEODE_API int        geode_dsp_fx_min(int control);
GEODE_API int        geode_dsp_fx_max(int control);
GEODE_API int        geode_dsp_fx_clamp(int control, int value);
GEODE_API int        geode_dsp_restore_bands(int preset, const int32_t* stored, int stored_count, int32_t* out, int capacity);
/* The same from the text the settings save the levels as: millibels, comma separated, low band first. An entry
 * that is not a whole number in int range is dropped and the rest move up; NULL is nothing saved.
 * _encode_bands writes that text for `count` levels (NUL-terminated, cut to fit) and returns the length the
 * whole text needs, so a short buffer asks again with room. */
GEODE_API int        geode_dsp_restore_bands_text(int preset, const char* csv, int32_t* out, int capacity);
GEODE_API int        geode_dsp_encode_bands(const int32_t* levels, int count, char* out, int capacity);
/* The equalizer's labels, written as _encode_bands writes its text: band `band`'s centre ("125 Hz", "1 kHz";
 * "0 Hz" for no such band) and a level in millibels as dB to a tenth ("+3.5 dB", "-12 dB", "0 dB"). */
GEODE_API int        geode_dsp_band_label(int band, char* out, int capacity);
GEODE_API int        geode_dsp_db_label(int millibels, char* out, int capacity);
GEODE_API void       geode_dsp_set_enabled(geode_dsp*, int enabled);          /* the equalizer and bass boost */
GEODE_API void       geode_dsp_set_band(geode_dsp*, int band, int millibels);
GEODE_API void       geode_dsp_set_bass_boost(geode_dsp*, int permille);      /* 0..1000 */
GEODE_API void       geode_dsp_set_loudness_mb(geode_dsp*, int millibels);    /* 0..1000 makeup gain */
GEODE_API void       geode_dsp_set_crossfeed(geode_dsp*, int enabled);

/* Settings › Playback's speed and pitch rows (audio/dsp/PlaybackControls). Pure, any thread. */
GEODE_API float      geode_playback_pitch_ratio(float semitones);   /* the rate a semitone shift plays at */
/* The nearest multiple of step (a half rounds up); a step not above 0, or a non-finite value, is left alone. */
GEODE_API float      geode_playback_snap(float value, float step);
/* The saved options that have a range. control: 0 fade ms, 1 crossfade ms, 2 repeat mode, 3 speed, 4 pitch
 * semitones, 5 ReplayGain mode, 6 ReplayGain preamp dB (GEODE_PLAYBACK_CONTROLS of them). range writes
 * min, max, step (0: none) and default into out4 and returns 1, or 0 for a control that is not one. clamp
 * holds a value to the range, a NaN to the default; crossfade_ms is a slider position in seconds as ms. */
#define GEODE_PLAYBACK_CONTROLS 7
GEODE_API int        geode_playback_range(int control, float* out4);
GEODE_API float      geode_playback_clamp(int control, float value);
GEODE_API int        geode_playback_crossfade_ms(float seconds);

/* What a track that will not play leads to: 0 skip to the next, 1 stop (nothing follows), 2 stop (3 failures
 * in a row, checked first). */
GEODE_API int        geode_playback_error_action(int consecutive_failures, int has_next);

/* The A-B loop (audio/dsp/PlaybackControls), -1 for a point not set. cycle: A-B pressed at at_ms, out2 = the new
 * start and end (none set marks A; A alone and more than 1 s past it marks B, else moves A; both set clears).
 * seek: the ms to seek to at position_ms (A once B is reached), or -1. Pure, any thread. */
GEODE_API void       geode_ab_loop_cycle(int64_t start_ms, int64_t end_ms, int64_t at_ms, int64_t* out2);
GEODE_API int64_t    geode_ab_loop_seek(int64_t start_ms, int64_t end_ms, int64_t position_ms);

/* Which track positions are remembered (audio/dsp/TrackBookmark). Pure, any thread. Verdict: 0 ignore (a track
 * under 20 minutes, or of unknown length), 1 clear (within 10 s of either end), 2 save this position. */
GEODE_API int        geode_bookmark_verdict(int64_t position_ms, int64_t duration_ms);
/* 1 when a track that just started at current_position_ms is still within 10 s of its start, so it may jump to its bookmark. */
GEODE_API int        geode_bookmark_resumes_from(int64_t current_position_ms);
/* At most this many tracks keep a bookmark; past it the shell drops the one saved longest ago. */
#define GEODE_BOOKMARK_MAX 200

/* Playback gain (audio/dsp/GainStage): ReplayGain, the UI fades, the sleep fade and the track crossfade, as
 * ramps linear in dB counted in samples, before the chain (#483). The shell only says targets and lengths;
 * the curves, the dB maths and when each applies are here. "Silence" is GEODE_DSP_SILENCE_DB or below (-inf
 * included); a frame there is multiplied by exactly 0. Setters from any thread, never blocking the audio. */
#define GEODE_DSP_SILENCE_DB (-80.0f)
/* The UI fade (pause, resume, skip): from wherever it is to db over ms; ms 0 jumps. */
GEODE_API void       geode_dsp_ramp_to(geode_dsp*, float db, int ms);
/* The shell's UI fade: to full level (0 dB) when audible, else to silence, over ms. */
GEODE_API void       geode_dsp_ramp_audible(geode_dsp*, int audible, int ms);
/* The sleep timer's time left in ms (< 0: cancelled or done). Its last 3 s ramp to silence, ending as the
 * timer ends; a cancel comes back up in 50 ms. Returns the ms until it next needs to hear the time left. */
GEODE_API int64_t    geode_dsp_sleep_fade(geode_dsp*, int64_t remaining_ms);
/* Settings › Playback › Crossfade: half fades out the tail of a track something follows, half fades in the
 * track that followed on. A skip, seek or pick starts at full level. */
GEODE_API void       geode_dsp_set_crossfade(geode_dsp*, int ms);
GEODE_API void       geode_dsp_set_follows_on(geode_dsp*, int follows_on);  /* a next item, or repeat one */
/* The player's queue as it stands: something follows on when there is a next item or repeat_mode is
 * GEODE_REPEAT_ONE (numbered as Media3's Player.REPEAT_MODE_*). */
#define GEODE_REPEAT_ONE 1
GEODE_API void       geode_dsp_set_up_next(geode_dsp*, int has_next, int repeat_mode);
GEODE_API void       geode_dsp_set_speed(geode_dsp*, float speed);          /* what Sonic plays at, upstream */
/* ReplayGain. mode: 0 off, 1 track, 2 album; clip_guard keeps the tagged peak below full scale. */
GEODE_API void       geode_dsp_set_replaygain(geode_dsp*, int mode, float preamp_db, int clip_guard);
/* One track's tags as the file carries them, by stream name (its item's URI), read ahead of its first sample.
 * mask: the GEODE_TAG_*_GAIN/PEAK bits of the values present (0: read, untagged), as geode_tags_replaygain
 * returns them. The 32 newest streams are kept. */
GEODE_API void       geode_dsp_set_stream_tags(geode_dsp*, const char* stream, int mask, float track_gain_db,
                                               float track_peak, float album_gain_db, float album_peak);
/* Playback thread, right after geode_dsp_flush: the stream whose first sample comes next (NULL or "" when
 * not known), the media position of that sample and the item's duration (<= 0 unknown). Its ReplayGain gain
 * starts on that sample; while its tags are still loading the last gain is kept, never 0 dB (#351). */
GEODE_API void       geode_dsp_begin_stream(geode_dsp*, const char* stream, int64_t position_us, int64_t duration_us);
/* Playback thread: the stream's last sample has been queued (Media3's end of stream). */
GEODE_API void       geode_dsp_end_stream(geode_dsp*);
/* In place, RT-safe. `channels` is the format the block was queued in: a block that is not the live chain's
 * passes through untouched, and frames are clamped to capacity_floats / channels. */
GEODE_API void       geode_dsp_process(geode_dsp*, float* interleaved, size_t capacity_floats, size_t frames, int channels);
/* The same on 16-bit PCM in place (the sink's encoding): the int16 <-> float conversion is native too. A block
 * the chain does not run is left byte for byte as it came. */
GEODE_API void       geode_dsp_process_pcm16(geode_dsp*, int16_t* interleaved, size_t capacity_samples, size_t frames, int channels);
/* The shell's raw block, in place: `bytes` (held to capacity_bytes) of `encoding` (GEODE_PCM_S16 or
 * GEODE_PCM_FLOAT, Media3's C.ENCODING_PCM_* as queued) at `channels`. Runs its whole frames through one of the
 * two calls above and returns their bytes, the block the shell passes on (a trailing part frame is dropped).
 * Any other encoding, or no channels, returns all of `bytes` untouched; a NULL dsp or pcm runs nothing but
 * still answers (capacity then unused). 0 for bytes <= 0. */
GEODE_API int        geode_dsp_process_bytes(geode_dsp*, void* pcm, size_t capacity_bytes, int bytes, int encoding, int channels);
/* The shell's queued block: copies `in_bytes` of `in` (held to out_capacity) into `out`, then runs it there as
 * geode_dsp_process_bytes does and returns the bytes to pass on. NULL in or out copies nothing and returns 0. */
GEODE_API int        geode_dsp_process_into(geode_dsp*, const void* in, int in_bytes, void* out, size_t out_capacity,
                                            int encoding, int channels);
/* Bit-perfect USB (#353): whether the platform takes the next AudioTrack bit-perfect, from any thread. From the
 * next flush (Media3 flushes whenever it builds an AudioTrack) until a flush with it off, process passes every
 * block through untouched, whatever gain, ramp, equalizer or crossfeed is set. _live reports the
 * latched state (tests). */
GEODE_API void       geode_dsp_set_bit_perfect(geode_dsp*, int on);
GEODE_API int        geode_dsp_bit_perfect_live(const geode_dsp*);

/* Tag I/O through TagLib. Both calls take a file descriptor they own: it is closed before they return, so
 * the caller detaches it first. Text crosses as UTF-8. */
typedef enum GeodeTagText {
    GEODE_TAG_TITLE = 0,
    GEODE_TAG_ARTIST,
    GEODE_TAG_ALBUM,
    GEODE_TAG_ALBUM_ARTIST,
    GEODE_TAG_GENRE,
    GEODE_TAG_COMMENT,
    GEODE_TAG_TEXT_COUNT
} GeodeTagText;
#define GEODE_TAG_TRACK_GAIN 1
#define GEODE_TAG_TRACK_PEAK 2
#define GEODE_TAG_ALBUM_GAIN 4
#define GEODE_TAG_ALBUM_PEAK 8

GEODE_API geode_tags* geode_tags_read(int fd);   /* NULL when TagLib cannot parse the file */
GEODE_API void        geode_tags_destroy(geode_tags*);
GEODE_API const char* geode_tags_text(const geode_tags*, GeodeTagText);   /* "" when absent */
GEODE_API int         geode_tags_year(const geode_tags*);
GEODE_API int         geode_tags_track(const geode_tags*);
GEODE_API int         geode_tags_duration_ms(const geode_tags*);
GEODE_API size_t      geode_tags_art_bytes(const geode_tags*);   /* every embedded picture together */
/* Fills the four ReplayGain values and returns the GEODE_TAG_*_GAIN/PEAK mask of the ones the file carries. */
GEODE_API int         geode_tags_replaygain(const geode_tags*, float* track_gain_db, float* track_peak,
                                            float* album_gain_db, float* album_peak);
/* texts holds GEODE_TAG_TEXT_COUNT UTF-8 strings in GeodeTagText order (NULL, or a short array's missing
 * slot, leaves that field unchanged; "" clears it). 1 = saved, 0 = failed. */
GEODE_API int         geode_tags_write(int fd, const char* const* texts, int year, int track);

/* Voice "play X" and Android Auto's search over the library (core/library/Search), best match first. `rows`
 * holds 3 UTF-8 strings per track, title, artist, album, `track_count` tracks in library order. `query` is the spoken query; `focus`, `artist`,
 * `album` and `title` are MediaStore's EXTRA_MEDIA_FOCUS / _ARTIST / _ALBUM / _TITLE (NULL = absent).
 * Writes up to `max_out` matching track indices to `out` and returns how many match in all, so a count
 * above `max_out` means the buffer was short. Nothing asked plays the whole library; 0 = no match. */
GEODE_API int         geode_library_search(const char* const* rows, int track_count, const char* query,
                                           const char* focus, const char* artist, const char* album,
                                           const char* title, int* out, int max_out);

/* The library index (core/library/Search, SearchIndex): the Library screen's order and search. `rows` holds
 * 3 UTF-8 strings per track (title, artist, album); `durations_ms` and `added_sec` one value per track (NULL =
 * zeros). Built once per library change, read from any thread, freed with _destroy (NULL is fine).
 * _order writes every row in `sort` order (0 title, 1 artist, 2 album, 3 duration, 4 newest first) and returns
 * the row count. _search writes three counts (tracks, albums, artists) and then those rows, best first, an
 * album or artist as its first row; it returns the ints in all, so a value above `max_out` means the buffer
 * was short. A blank query finds nothing. */
typedef struct geode_library_index geode_library_index;
GEODE_API geode_library_index* geode_library_index_create(const char* const* rows, const long long* durations_ms,
                                                          const long long* added_sec, int track_count);
GEODE_API void        geode_library_index_destroy(geode_library_index*);
GEODE_API int         geode_library_index_order(const geode_library_index*, int sort, int* out, int max_out);
GEODE_API int         geode_library_index_search(const geode_library_index*, const char* query, int* out, int max_out);

/* The global search sheet (core/library/Search, searchSheet): tracks, playlists and presets for one query.
 * `tracks` holds GEODE_SEARCH_SHEET_FIELDS UTF-8 strings per track: title, artist, album, one more field searched
 * after them (a folder, a genre), and the key every copy of one song shares (its URI). `preferred` (NULL = none)
 * holds one byte per track, nonzero for the copy shown when copies meet. `playlists` and `presets` are names.
 * Writes three counts (tracks, playlists, presets) and then those rows, best first, at most
 * GEODE_SEARCH_SHEET_TRACKS tracks; returns the ints in all, so a value above `max_out` means the buffer was
 * short. A blank query, or a missing array with a count, finds nothing (three zero counts). */
#define GEODE_SEARCH_SHEET_FIELDS 5
#define GEODE_SEARCH_SHEET_TRACKS 30
GEODE_API int         geode_search_sheet(const char* const* tracks, const unsigned char* preferred, int track_count,
                                         const char* const* playlists, int playlist_count,
                                         const char* const* presets, int preset_count, const char* query,
                                         int* out, int max_out);

/* Lyrics (core/library/Lyrics). _parse reads `len` bytes of UTF-8 .lrc or plain lyrics (a NUL among them is text,
 * not the end) into the wire form: "S" (timed, sorted by time) or "P" (plain) and a newline, then one "<timeMs>\t<text>\n" per line, timeMs -1 on a plain line; "" = no
 * lyrics. The text goes out as geode::api::textOut does (the return is the length needed). _index_at takes the
 * ascending times of the lines and returns the line sung at `position_ms`: -1 before the first, with no lines,
 * or when plain (first time -1). */
GEODE_API int         geode_lyrics_parse(const char* text, int len, char* out, int capacity);
GEODE_API int         geode_lyrics_index_at(const long long* times_ms, int count, long long position_ms);

/* A playlist file the user picked (core/library/Playlist): M3U, PLS or XSPF as UTF-8 bytes, `file_name` its name
 * (a hint for the format, and the playlist's name when the file has none). Text goes into `out`, NUL-terminated;
 * the return is the length the whole text needs. Record 0 is "O", name or "E", reason; then one record per entry:
 * location, title, duration in ms (-1 unknown), file name inside the location with its case kept. Fields are split
 * by U+001F, records by U+001E. A playlist with a DOCTYPE is refused. Pure, any thread. */
GEODE_API int         geode_playlist_parse(const char* file_name, const unsigned char* data, int len, char* out,
                                           int capacity);

/* The play history (core/library/History). The shell holds history.json's text as the state, hands it to each call
 * and keeps what comes back; it does the file, the lock, the quarantine and the write queue. Text out is valid until
 * the calling thread's next call of the same function; NULL arguments read as "". Pure, any thread.
 * corrupt: 1 when the text is not blank and does not read (the shell moves the file aside and starts empty).
 * record_play: the state once `uri` starts playing at now_ms (made if new, count up, title set, artist unless
 * blank, stamped after every play held). add_listen: the state with delta_ms (ignored at 0 or less, and for a uri
 * not held) added to the track's listened time. file_text: the text to write (the 200 most recent and the 200 most
 * played). recent: at most `limit` entries, newest first, as History's wire: uri, last-played ms, play count,
 * title, artist and listened ms split by U+001F, each entry ended by U+001E. */
GEODE_API int         geode_history_corrupt(const char* text);
GEODE_API const char* geode_history_record_play(const char* state, const char* uri, const char* title,
                                                const char* artist, long long now_ms);
GEODE_API const char* geode_history_add_listen(const char* state, const char* uri, long long delta_ms);
GEODE_API const char* geode_history_file_text(const char* state);
GEODE_API const char* geode_history_recent(const char* state, int limit);

/* Smart playlists (core/library/SmartPlaylist). The shell lists smart-playlists/ and reads and writes the files'
 * text. A playlist crosses as match_all (1 or 0), limit and `rules`, 3 UTF-8 strings per rule: the field and op
 * tokens the files hold (TITLE ... FAVOURITE, CONTAINS ... AT_MOST) and the value; a rule with an unknown token is
 * dropped. Text out is valid until the calling thread's next call of the same function; NULL texts read as "".
 * smart_list: the playlists among `count` file texts, sorted by lower-case name (ties in the order given), as
 * SmartPlaylist's wire: per playlist its name, match_all ("1" or "0"), limit, then each rule's field token, op token
 * and value, split by U+001F, each playlist ended by U+001E.
 * smart_save_text: the text to write for a playlist named `name`, NULL for a blank name (nothing is saved).
 * smart_resolve: the rows of `tracks` the playlist picks, in order (every track when no rule with a value is left,
 * then the first `limit` when limit > 0). `tracks` holds 5 UTF-8 strings per track: uri, title, artist, album,
 * folder; durations_ms and added_sec one value per track (NULL = zeros); `history` the play history's state (the
 * play counts, see geode_history_*); `favourites` the favourite uris; now_ms the wall clock. Writes up to max_out rows
 * to out and returns how many are picked. */
GEODE_API const char* geode_smart_list(const char* const* texts, int count);
GEODE_API const char* geode_smart_save_text(const char* name, int match_all, int limit, const char* const* rules,
                                            int rule_count);
GEODE_API int         geode_smart_resolve(int match_all, int limit, const char* const* rules, int rule_count,
                                          const char* const* tracks, const long long* durations_ms,
                                          const long long* added_sec, int track_count, const char* history,
                                          const char* const* favourites, int favourite_count, long long now_ms,
                                          int* out, int max_out);

/* The user's ordinary playlists (core/library/PlaylistRules). The shell lists music-playlists/, reads and writes the
 * files' text and acts on what comes back; a playlist's file is its name's geode_file_safe_name stem plus ".json".
 * Text out is valid until the calling thread's next call of the same function; NULL texts read as "". Fields are
 * split by U+001F and records ended by U+001E.
 * list: the playlists among `count` file texts, sorted by lower-case name (ties in the order given): per playlist
 * its name, then its track uris.
 * apply: one edit, `kind` a GEODE_MUSIC_PLAYLIST_* below: create (name, tracks: each track once under the first free
 * name from the trimmed name; a blank name writes nothing), import (name is the base, tracks as given, under the
 * first free name), add (track), remove (track), move (from, to) and rename (name to new_name). A free name is one
 * whose stem no listed playlist has. `texts` are every playlist file's text, `named` the text of the file `name`
 * maps to, NULL when there is none (a file that cannot be read is ""): an add, remove or move never writes over a
 * file that does not read. The answer is one record: write ("1" or "0"), ok ("0" when a rename is refused), the
 * name whose file to write `json` to, the name whose file to delete once it is written ("" for none), and json.
 * migration: the one-time renames of files saved under an older name rule, given every entry of the folder
 * (`texts[i]` the text of a .json file, "" for anything else): per rename its from and to file names, in order.
 * import_base: the name an imported file's playlist starts from: its own name, else the file's name, else
 * `untitled`, each kept as it is when not blank. */
#define GEODE_MUSIC_PLAYLIST_CREATE 0
#define GEODE_MUSIC_PLAYLIST_IMPORT 1
#define GEODE_MUSIC_PLAYLIST_ADD 2
#define GEODE_MUSIC_PLAYLIST_REMOVE 3
#define GEODE_MUSIC_PLAYLIST_MOVE 4
#define GEODE_MUSIC_PLAYLIST_RENAME 5
GEODE_API const char* geode_music_playlist_list(const char* const* texts, int count);
GEODE_API const char* geode_music_playlist_apply(int kind, const char* name, const char* new_name, const char* track,
                                                 const char* const* tracks, int track_count, int from, int to,
                                                 const char* const* texts, int text_count, const char* named);
GEODE_API const char* geode_music_playlist_migration(const char* const* file_names, const char* const* texts,
                                                     int count);
GEODE_API const char* geode_music_playlist_import_base(const char* parsed_name, const char* file_name,
                                                       const char* untitled);

/* The saved play queue, session.json (core/library/SessionDoc). The shell reads and writes the file's text. Text out
 * is valid until the calling thread's next call of the same function; NULL texts read as "".
 * session_read: the session the text holds as one record of index, position ms, then each track's uri, title and
 * artist, split by U+001F and ended by U+001E; "" when the text is no session (the shell moves such a file aside):
 * not an object, a version above 1, no track with a uri. Index and position come clamped.
 * session_write: the text to write for `count` tracks (3 UTF-8 strings each: uri, title, artist), the index and the
 * position (both clamped); "" for no tracks, when the shell deletes the file instead.
 * session_position_write: the small text session_position.json is written as when only the position moved; the
 * queue is not written again. session_read's position_text is that file's text (NULL: none): it moves the
 * session's track and position (newer than the queue's); unreadable text changes nothing. */
GEODE_API const char* geode_session_read(const char* text, const char* position_text);
GEODE_API const char* geode_session_position_write(int index, long long position_ms);
GEODE_API const char* geode_session_write(const char* const* tracks, int count, int index, long long position_ms);

/* The app's own track library and the rules around it (core/library/TrackRules, TrackLibraryDoc). The shell reads
 * and writes library.json and hands its text in; every call takes a whole batch and answers once. A track crosses
 * as GEODE_TRACK_FIELDS UTF-8 strings: uri, title, artist, duration ms, album, genre, year, track number, comment,
 * file name, size in bytes (numbers as decimal text). An answer is U+001E-ended records of U+001F-split fields.
 * An edit answers a record of changed ("1" or "0"), corrupt input ("1": the old file did not read as a library and
 * is set aside) and the json to write when changed, then one record per track of the library after the edit.
 * _apply_fresh takes 3 strings per re-read title (uri, title, artist) and keeps each track's other info.
 * _tag_fallbacks: per file 9 strings (title, artist, album, genre, year, track number as the reader gave them, NULL
 * for none; uri; display name; last path segment, NULL for none) and its size; a track record per file.
 * _walk_actions: per entry its name and MIME type (NULL for none) and is_dir; out[i] is 0 skip, 1 descend, 2 keep.
 * _title_refresh: 2 strings per track (title, artist); out[i] is 1 when the title should be read again.
 * _device_rows: per row 4 strings (title, artist, album, path; NULL for none) and its duration; rows shorter than
 * min_seconds are left out; `fallbacks` are the 3 names for a missing title, artist, album. A record per kept row:
 * its index, title, artist, album, folder; and a last record holding the count left out.
 * _resolve_entries: playlist entries' base names matched to the library's file names and uris (`library` holds 2
 * strings per track: file name, uri); three records: the uris, the missing indices, the ambiguous indices. */
#define GEODE_TRACK_FIELDS 11
GEODE_API const char* geode_track_library_read(const char* text);
GEODE_API const char* geode_track_library_add(const char* text, const char* const* tracks, int count);
GEODE_API const char* geode_track_library_save_info(const char* text, const char* uri, const char* title,
                                                    const char* artist, const char* album, const char* genre, int year,
                                                    int track_no, const char* comment);
GEODE_API const char* geode_track_library_apply_fresh(const char* text, const char* const* fresh, int count);
GEODE_API const char* geode_track_tag_fallbacks(const char* const* files, const long long* sizes, int count,
                                                const char* untitled);
GEODE_API int         geode_track_walk_actions(const char* const* names, const char* const* mimes, const int* is_dir,
                                               int count, int parent_depth, int* out);
GEODE_API int         geode_track_title_refresh(const char* const* title_artist, int count, int* out);
GEODE_API const char* geode_track_device_rows(const char* const* rows, const long long* durations_ms, int count,
                                              int min_seconds, const char* const* fallbacks);
GEODE_API const char* geode_track_resolve_entries(const char* const* base_names, int entry_count,
                                                  const char* const* library, int track_count);
/* The backup's rules (core/backup/Backup). The shell reads and writes the zip, files and SharedPreferences, and hashes
 * with the platform's SHA-256; each call is one plan or one answer, never one file. Lists cross as arrays of UTF-8
 * strings in both directions, read in order: a count is decimal text, a flag is "1" or "0", a digest is lowercase hex,
 * a preference value is its type ("s" string, "i" int, "l" long, "f" float, "b" boolean, "t" string set, "u" anything
 * else) then its text (a float as its double text, a boolean as "1" or "0") then its item count and items (a set's).
 * An answer starts with the problem number (core/backup Problem: 0 none) and a message for the log; what follows is
 * meant only when the problem is 0 unless said. The answer is valid until the next backup call on the same thread;
 * `*out_count` is its length. A malformed argument list answers problem 6.
 *   _layout: the manifest's archive path, the byte cap on a manifest, the byte cap on a backup file, the entry cap,
 *     the section count, then per section its id, its prefs-file count and (file, archive path) each, its root
 *     count and roots (filesDir-relative dirs, then files). No problem or message.
 *   _plan_export: prefs = snapshot count, then per file its name, entry count and per entry key, type, text, item
 *     count, items; files = (section, path) pairs. Answers the byte cap on the files of the zip, then the
 *     sources in zip order: count, per source its path, "1" when the next string is its content (else the shell opens filesDir/path) and the content.
 *   _finish_export: per written entry its path, size and digest. Answers the manifest text, to be written last.
 *   _plan_unpack: the zip's entry count, "1" when manifest.json exists, "1" when reading it passed the cap, its text,
 *     then per entry its name and "1" for a directory. Answers the failure to raise after extracting (problem,
 *     message), the staged prefs paths to read for _finish_unpack, then the items to extract: count, per item its
 *     entry index, path, promised size, promised digest and byte budget.
 *   _check_staging: the canonical staging directory, the path separator, then per item its entry name and canonical
 *     target. Answers the first target outside staging as an unsafe path.
 *   _check_extracted: an item's path, promised size, promised digest and budget as the plan gave them, then the
 *     bytes read, their digest and "1" when the read was cut at the budget.
 *   _finish_unpack: the manifest text, then (path, text) per staged prefs file. Answers the backup's creation time
 *     (ms), its app version code, the section ids to offer, then the files migration rewrote: count, (path, text).
 *   _plan_restore: "1" when something is staged, the staged section ids (count first), the requested ids (count
 *     first), the staged prefs as (path, text) pairs (count first), then per prefs file the phone holds its name,
 *     key count and keys. Answers per section to restore its id, its targets, carry-over subtrees, directories to
 *     create and its prefs (count first each; per prefs file its name, staged path, keys to remove, and the values
 *     to put, each as key, type, text, item count, items). */
GEODE_API const char* const* geode_backup_layout(int* out_count);
GEODE_API const char* const* geode_backup_plan_export(const char* const* prefs, int prefs_len, const char* const* files,
                                                      int files_len, int* out_count);
GEODE_API const char* const* geode_backup_finish_export(const char* const* entries, int entries_len, int app_version_code,
                                                        long long created_at_ms, int* out_count);
GEODE_API const char* const* geode_backup_plan_unpack(const char* const* request, int request_len, int* out_count);
GEODE_API const char* const* geode_backup_check_staging(const char* const* request, int request_len, int* out_count);
GEODE_API const char* const* geode_backup_check_extracted(const char* const* item, int item_len, int* out_count);
GEODE_API const char* const* geode_backup_finish_unpack(const char* const* request, int request_len, int* out_count);
GEODE_API const char* const* geode_backup_plan_restore(const char* const* request, int request_len, int* out_count);
/* beatrealms-backup-YYYYMMDD.zip; valid until the next call on the same thread. */
GEODE_API const char* geode_backup_file_name(int year, int month, int day);

/* Scene and style catalogue (core/viz/styles/catalogue/Catalogue), one question per call; the shell keeps the answers. A list
 * is its items each followed by '\n'. _style_ids and _picker_names are static; every other string is valid
 * until the next catalogue call on the same thread. NULL arguments read as "". Display labels and the Pro
 * rule are not here.
 * _style_ids: the styles with a trait, in the order the picker lists them ("" for an unknown trait).
 * _picker_names / _picker_ids: the Styles page's families in order, then one family's styles.
 * _resolve: the style a saved id opens on (a retired id's replacement; any other id is itself).
 * _preset_style: the style a saved look of this one is filed under (Fluid's and Cymatics' looks share one).
 * _scope_of: the name of the scope a Customize control (its Customize key) has; "UNIVERSAL" if none.
 * _reaches: 1 when a control of a scope, by that name, is shown for the style (catalogue::shows; an unknown name shows everywhere).
 * _override: the value a style holds a control at, NaN when the control is the user's.
 * _value: a size the shell's controls quote, by name: "fluidTiers" (the Fluid quality tiers), "touchPoints"
 *   (the fingers geode_viz_set_touch takes), "touchRotationPerDegree"; NaN for any other name. */
#define GEODE_STYLE_ANY 0
#define GEODE_STYLE_SHADER 1
#define GEODE_STYLE_FAMILY_FOLLOWER 2 /* in a shader family and not its opener */
#define GEODE_STYLE_AUTO_DRAW 3       /* what Auto's random step draws from */
#define GEODE_STYLE_FLUID 4
#define GEODE_STYLE_CYMATICS 5
GEODE_API const char* geode_catalogue_style_ids(int trait);
/* 1 when the style has the trait; 0 for an unknown trait or id. */
GEODE_API int         geode_catalogue_style_has(int trait, const char* style_id);
GEODE_API const char* geode_catalogue_picker_names(void);
GEODE_API const char* geode_catalogue_picker_ids(const char* family);
GEODE_API const char* geode_catalogue_resolve(const char* style_id);
GEODE_API const char* geode_catalogue_preset_style(const char* style_id);
GEODE_API const char* geode_catalogue_scope_of(const char* param_key);
GEODE_API int         geode_catalogue_reaches(const char* scope_name, const char* style_id);
/* 1 when the style holds the control at a value of its own (geode_catalogue_override then gives it), 0 when it is the user's. */
GEODE_API int         geode_catalogue_has_override(const char* style_id, const char* param_key);
GEODE_API float       geode_catalogue_override(const char* style_id, const char* param_key);
GEODE_API float       geode_catalogue_value(const char* name);

/* The catalogue's presets section (core/viz/styles/catalogue/CataloguePresets, #648): the bundled looks, each a named set of
 * param values by #346's param name, and every bundled preset (name, style id, look), as one versioned
 * '|'-separated line table (see CataloguePresets.hpp for the rows). Static: the pointer stays valid for the process. */
GEODE_API const char* geode_catalogue_presets_text(void);

/* Customize randomizer and Reset tab (core/viz/panel/ParamRandomizer) over SceneParams
 * in the geode_viz_param_name wire order: geode_viz_param_count floats, ints and flags as floats. Pure: no viz
 * handle; the same arguments always give the same result. Controls are named by their Customize keys, tabs
 * by their CustomizeTab names (a tab the engine does not know rolls and resets nothing). in and out may alias.
 * style_id NULL or empty = any style; tab NULL = every tab; locked = newline-joined control names, NULL = none.
 * Returns 1 when the roll changed something, 0 when out is a copy of in. */
GEODE_API int         geode_params_randomize(const char* style_id, const char* tab, const char* locked, uint64_t seed,
                                             const float* in, float* out);
GEODE_API void        geode_params_reset_tab(const char* tab, const float* in, float* out);
/* A frame read from a saved look or a shared link, made safe (viz/params/Params SceneParams::sanitized): a non-finite
 * value or a retired slot reads as the row's default, an int is held to its range and rounds, a flag is
 * value > 0.5, a float is held to its range and a circular one is left to wrap. Same wire; in and out may alias. */
GEODE_API void        geode_params_sanitize(const float* in, float* out);
/* A look's frame `in` (a preset, a template, Reset all) as it replaces the live frame `current`: every slot whose
 * meta row has kInput (viz/params/ParamTable.inc: an input preference such as Tilt phone to look, not part of a look) keeps
 * current's value, every other slot is in's (viz/params/Params SceneParams::keepInputs). Same wire; out may alias either. */
GEODE_API void        geode_params_keep_inputs(const float* current, const float* in, float* out);
/* The scene params object of a preset, shared link, template look or saved live look (viz/params/ParamsJson), the
 * text the shell's org.json wrote and read. write_json: the n-float geode_viz_set_params frame (n at least
 * geode_viz_param_count) and the two palette slot ids (NULL as "") as compact JSON, valid until the calling
 * thread's next write_json; NULL for a short frame. read_json: the object at `path` (nested keys, newline-separated,
 * NULL or "" for the top) of `document`, the text org.json parsed, when that is standard JSON, else `fallback`, the
 * object as org.json printed it (either may be NULL), read as org.json's opt* read it and sanitized
 * (geode_params_sanitize) into out_frame, and the ids into *id_a and *id_b ("" when missing; each may be NULL),
 * valid until the calling thread's next read_json; 1, or 0 with nothing written when neither holds an object or
 * the frame is short. Pure, any thread. */
GEODE_API const char* geode_params_write_json(const float* frame, int n, const char* id_a, const char* id_b);
GEODE_API int         geode_params_read_json(const char* document, const char* path, const char* fallback,
                                             float* out_frame, int n, const char** id_a, const char** id_b);
/* The scene params schema (core/viz/params/ParamTable.inc): one row per geode_viz_param_name slot, giving the field's
 * name, kind, default, control range and whether a fade glides it, as a versioned '|'-separated line table:
 *   params|<version>|<count>
 *   param|<slot>|<name>|<FLOAT|INT|BOOL|CIRCULAR>|<default>|<min>|<max>|<FADE|HOLD>
 * slots 0..count-1 in order; ints and bools as numbers, like the wire. Then palette|<index>|<name> per built-in
 * palette and choice|<name>|<index>|<label> per picker label (Params.hpp). Static: the pointer stays valid for the
 * process. The shell does not read it (it asks geode_params_get and _set by name); its JVM tests read the
 * checked-in copy, core/tests/params_schema.txt. */
GEODE_API const char* geode_params_schema_text(void);
/* The text a Customize control shows for `value` (viz/panel/ParamText): "35 %", "1.8×", "2.5 s" or "35 ms",
 * "110 Hz", "90°", and "Default" at `def` (NaN for a control with none). `unit` is 0 percent, 1 times,
 * 2 seconds, 3 hertz, 4 turn degrees, 5 plain; any other value takes the control's own, by its `key` and then
 * its range lo..hi. A range below zero signs a percentage. UTF-8 into `out`, NUL-terminated; returns the
 * length the whole text needs. Pure, any thread. */
GEODE_API int         geode_param_text(const char* key, int unit, float value, float lo, float hi, float def,
                                       char* out, int capacity);
/* The hue base and span a palette slot draws (viz/params/Params paletteBase, paletteRange): the override when it is
 * >= 0, else built-in palette `palette`, clamped into the table. The same for slot 2. Pure, any thread. */
GEODE_API float       geode_params_palette_base(int palette, float base_override);
GEODE_API float       geode_params_palette_range(int palette, float range_override);
/* The look's own palette in a geode_viz_set_params frame (viz/params/ParamsPalette): its hue overrides and your colours'
 * stops. uses_custom_palette: 1 when an override or the first stop is set (a saved palette's id means something only
 * then). palette_stops: how many stops are set from the first, into out (room for geode_saved_palette_max_stops()). clear_custom_palette: both overrides
 * and every stop unset. apply_stops: cleared, then the first `count` (at most geode_saved_palette_max_stops()) of `stops`. apply_gradient: no
 * stops, base and span held to 0..1 as the overrides. Same wire in and out; in and out may alias. Pure, any thread. */
GEODE_API int         geode_params_uses_custom_palette(const float* frame);
GEODE_API int         geode_params_palette_stops(const float* frame, int* out);
GEODE_API void        geode_params_clear_custom_palette(const float* in, float* out);
GEODE_API void        geode_params_apply_stops(const float* in, const int* stops, int count, float* out);
GEODE_API void        geode_params_apply_gradient(const float* in, float base, float span, float* out);
/* The look files (viz/looks/LookFiles): the shell reads and writes the bytes, every rule is here. Text out is valid until
 * the calling thread's next call of the same function. Pure, any thread.
 * file_safe_name: a name as a file stem (A-Z a-z 0-9 - _ space kept, anything else '_', and a changed or blank name
 * gets '-' and four SHA-256 bytes in hex); file_safe_chars the same without the hash (a preset folder). looks_order: the indices of `names` in list order into out (count ints),
 * caseless or by code point, ties kept; returns count.
 * Saved palettes (files/palettes/<id>.json): read gives the file's text as this build writes it, or NULL when it is
 * no palette; sanitize the same, the id, name, hues and colours made safe; create a new palette's text (a hue
 * gradient, or your colours when count > 0); migrate the text the file should hold under its current id; id_for a
 * name's id (its file stem); string its "id" or "name"; hues writes base and span to out[2]; stops writes its colours
 * to out (at most max_stops) and returns how many; apply makes it the params frame's own palette (1, or 0 and a copy
 * when the text is no palette); kept_id the saved palette id a look keeps once deleted_id is deleted. */
GEODE_API const char* geode_file_safe_name(const char* name);
GEODE_API const char* geode_file_safe_chars(const char* name);
GEODE_API int         geode_looks_order(const char* const* names, int count, int caseless, int* out);
GEODE_API const char* geode_saved_palette_read(const char* text);
GEODE_API const char* geode_saved_palette_sanitize(const char* text);
GEODE_API const char* geode_saved_palette_create(const char* name, float base, float span, const int* stops, int count);
GEODE_API const char* geode_saved_palette_migrate(const char* text);
GEODE_API const char* geode_saved_palette_id_for(const char* name);
GEODE_API const char* geode_saved_palette_string(const char* text, const char* key);
GEODE_API int         geode_saved_palette_hues(const char* text, float* out);
GEODE_API int         geode_saved_palette_stops(const char* text, int* out);
GEODE_API int         geode_saved_palette_max_stops(void);
GEODE_API int         geode_saved_palette_apply(const char* text, const float* in, float* out);
GEODE_API const char* geode_saved_palette_kept_id(const char* look_id, const char* deleted_id);
/* The saved palette id a look keeps on a params frame native wrote (a roll, a reset): `look_id` while the frame
 * still draws its own palette, "" once it does not. Thread-local text. */
GEODE_API const char* geode_saved_palette_frame_id(const char* look_id, const float* frame);
/* Presets (files/presets/..., a geode://preset link's body, the saved live look; viz/looks/LookFiles). read: the text as
 * org.json read it, the params into out_frame (n at least geode_viz_param_count), out_text[4] = name, scene id (a
 * retired style resolved), custom shader (NULL when absent), saved palette id, and out_envelope[2] =
 * attack and decay held to the sliders' ranges; 1, or 0 with nothing written when it is no preset. The strings stay
 * valid until the calling thread's next read. write: the file text org.json wrote (toString(2)); custom_shader
 * NULL to leave it out; NULL for a short frame. A look on a style this build does not have is no preset (0). */
GEODE_API int         geode_preset_read(const char* text, float* out_frame, int n, const char** out_text, float* out_envelope);
GEODE_API const char* geode_preset_write(const char* name, const char* scene_id, float attack, float decay,
                                         const char* custom_shader, const float* frame, int n,
                                         const char* custom_palette_id);
/* Preset names (viz/looks/LookFiles). is_built_in: 1 when the name holds the built-ins' " · ". clean_name: the separator
 * as " - ", trimmed, `fallback` when nothing is left; folder_name: trimmed. rename_problem and folder_name_problem:
 * 0 fine, 1 blank, 2 taken (another of the `count` names, case ignored); `current` NULL for a new folder.
 * copy_name: a duplicate's free "<name> copy" ("... 2" on, case ignored); import_name: cleaned (`fallback` when
 * blank) and free in `taken` exactly. replace_target: the user preset a save under `raw` overwrites, or NULL.
 * built_in_clash: 1 when `raw` names a built-in, case ignored. matches_scene: 1 when a built-in of style `preset`
 * is made for style `active`. Text out valid until the calling thread's next call of the same function. Pure. */
GEODE_API int         geode_preset_is_built_in(const char* name);
GEODE_API const char* geode_preset_clean_name(const char* raw, const char* fallback);
GEODE_API const char* geode_preset_folder_name(const char* raw);
GEODE_API int         geode_preset_rename_problem(const char* raw, const char* current, const char* const* taken, int count);
GEODE_API int         geode_preset_folder_name_problem(const char* raw, const char* current, const char* const* folders,
                                                       int count);
GEODE_API const char* geode_preset_copy_name(const char* name, const char* const* taken, int count);
GEODE_API const char* geode_preset_import_name(const char* raw, const char* fallback, const char* const* taken, int count);
GEODE_API const char* geode_preset_replace_target(const char* raw, const char* const* names, int count);
GEODE_API int         geode_preset_built_in_clash(const char* raw, const char* const* names, int count);
GEODE_API int         geode_preset_matches_scene(const char* preset_scene_id, const char* active_scene_id);
/* A geode://preset link's framing (viz/looks/LookFiles); the shell gzips and base64s the payload. frame: the link for a
 * payload; fits: 1 when a link is short enough to share; is: 1 when the trimmed text starts with the prefix, case
 * ignored; payload: what follows it, a '#' or '?' tail cut, or NULL when it is no link or too long; find: the first
 * link in pasted text up to whitespace, or NULL; body_accepted: 1 when an inflated payload is not blank;
 * max_inflated: the most bytes a payload may inflate to. Text out as above. Pure, any thread. */
GEODE_API const char* geode_preset_link_frame(const char* payload);
GEODE_API int         geode_preset_link_fits(const char* link);
GEODE_API int         geode_preset_link_is(const char* text);
GEODE_API const char* geode_preset_link_payload(const char* text);
GEODE_API const char* geode_preset_link_find(const char* text);
GEODE_API int         geode_preset_link_body_accepted(const char* body);
GEODE_API int         geode_preset_link_max_length(void);
GEODE_API int         geode_preset_link_max_inflated(void);
/* The template library (viz/looks/LookFiles). file_name: a template's file, its name's stem, "--", its id's tag and `suffix`
 * (the template format's); file_tail: what every file of that id ends with. unique_name: the name a new template
 * takes beside the `count` names in `taken` ("<name> 2" and on when it is taken, ASCII caseless). starter_count and
 * starter(i): the bundled starter set, each a whole template file ("" past the end); is_starter: 1 for a starter's
 * id. Text out valid until the calling thread's next call of the same function. Pure, any thread. */
GEODE_API const char* geode_template_file_name(const char* name, const char* id, const char* suffix);
GEODE_API const char* geode_template_file_tail(const char* id, const char* suffix);
GEODE_API const char* geode_template_unique_name(const char* base, const char* const* taken, int count);
GEODE_API int         geode_template_starter_count(void);
GEODE_API const char* geode_template_starter(int index);
GEODE_API int         geode_template_is_starter(const char* id);
/* The template file (viz/looks/TemplateFile): the shell hands the text in and keeps the text out. read: 1 when `text` is a
 * template, out_text[6] = the file text this build writes for it, id, name, scene id, custom shader
 * (NULL when absent), saved palette id, the params into out_frame (n at least geode_viz_param_count) and attack and
 * decay into out_envelope[2]; 0 when it is JSON but no template and -1 when it cannot be read, out_text[0] the reason
 * either way. create: a new template's text (fresh id, every other section at its default) for the look given;
 * NULL for a short frame. renamed: the text with `name`; new_copy: with a fresh id, `name`, no author and
 * created_at_ms (NULL when `text` is no template). link_in: the geode://template link in `text` as the
 * geode://preset link of the same payload, or NULL; link_of: a geode://preset link as the template link, NULL when
 * it is no preset link or longer than max_length. share_name: the file name a shared copy arrives under. Text out
 * valid until the calling thread's next call of the same function. Pure, any thread. */
GEODE_API int         geode_template_read(const char* text, float* out_frame, int n, const char** out_text, float* out_envelope);
GEODE_API const char* geode_template_create(const char* name, long long created_at_ms, const char* scene_id, float attack,
                                            float decay, const char* custom_shader,
                                            const float* frame, int n, const char* custom_palette_id);
GEODE_API const char* geode_template_renamed(const char* text, const char* name);
GEODE_API const char* geode_template_new_copy(const char* text, const char* name, long long created_at_ms);
GEODE_API const char* geode_template_link_in(const char* text);
GEODE_API const char* geode_template_link_of(const char* preset_link, int max_length);
GEODE_API const char* geode_template_share_name(const char* name);
/* Your colours (viz/colour/Colour.hpp): a colour is a packed 0xRRGGBB int, -1 for none. Pure, any thread.
 * from_hsv: hue wraps, saturation and value clamp to 0..1. to_hsv writes hue, saturation, value (0..1) to out[3].
 * parse_hex: "#RRGGBB", "RRGGBB", "#RGB" or "RGB", or -1. hex: "#RRGGBB" ("" for -1), valid until the
 * calling thread's next call. average_argb: the mean of n 0xAARRGGBB pixels, alpha ignored (the eyedropper).
 * palette_accent: the shell's accent for a palette of hue base and range (0..1 of a turn each): the middle of its
 * hue span, bright enough to read on black; -1 for a non-finite input. */
GEODE_API int         geode_colour_from_hsv(float h, float s, float v);
GEODE_API void        geode_colour_to_hsv(int rgb, float* out);
GEODE_API int         geode_colour_parse_hex(const char* text);
GEODE_API const char* geode_colour_hex(int rgb);
GEODE_API int         geode_colour_average_argb(const int32_t* argb, int n);
GEODE_API int         geode_colour_palette_accent(float base, float range);
/* The artwork palette (viz/colour/ArtPalette): a cover's n 0xAARRGGBB pixels (alpha ignored) read as a hue gradient.
 * Returns 1 and writes out[3] = base hue (0..1), span (0.06..1 of a turn) and confidence (the share of coloured
 * pixels); 0, out untouched, when no pixel is coloured (greys, near-blacks, blown highlights) or n <= 0. */
GEODE_API int         geode_art_palette_extract(const int32_t* argb, int n, float* out);
/* `samples` colours along a palette, for the swatches the shell draws. ramp: your colours' loop through the
 * first `count` of `stops` (lib_palette's palStops). stops_from_hues: `count` (<= 5) stops of a hue gradient,
 * placed where ramp() puts them. params_palette_preview: what the look's own palette in the
 * geode_viz_set_params frame `params` paints before Hue shift and Hue range: its stops when set, else the
 * cosine ramp of its hue base and span. */
GEODE_API void        geode_palette_ramp(const int* stops, int count, int samples, int* out);
GEODE_API void        geode_palette_stops_from_hues(float base, float range, int count, int* out);
GEODE_API void        geode_params_palette_preview(const float* params, int samples, int* out);
/* The palette stack (viz/colour/Palette.hpp, Customize › Look): the look's palettes, the primary first, up to 8, each
 * up to 16 colours with its options (SMOOTH|STEPS, MIRROR|LOOP, RGB|HUE|OKLAB, a position per stop, and for one
 * colour SHADES|FLAT|TO_BLACK), shared by blend SHARE (each its amount's part of the ramp) or MIX, meeting at a
 * SOFT edge (a bounded cross-fade; the default) or a HARD one under SHARE, and fullRange (0: colour moves stay
 * in the stack). set_edge and set_blend change nothing for a name they do not know. The shell keeps a stack and a palette as opaque text and hands it back;
 * every edit returns the edited text, valid until the calling thread's next palette call. Pure, any thread.
 * library_text: "library|<version>|<count>" then "entry|<index>|<id>|<SOURCE>|<name>|<INTERP>|<WRAP>|<SPACE>|<SINGLE>|<stops>"
 * (static); entries 0..20 are the old `palette` ramps, 21..25 the old colour maps. library_entry: that entry as
 * a palette line ("" out of range); library_index: an id's index or -1. colour_map_name: colour map i's name
 * (`paletteLut` i), "" past the last (static). stack_unset: the stack that draws the
 * style's own palette; stack_resolve_default puts style_default in as the only layer of an unset stack.
 * stack_layer: a layer as a palette line, "" out of range. stack_set_layer at layer_count appends.
 * from_colours: a palette of `count` 0xRRGGBB colours (invalid skipped, 16 kept) from source BUILT_IN,
 * COLOUR_MAP, STYLE (ref = style id), ARTWORK or YOURS. Palette edits return ""
 * for a line that is not a palette; set_stop and add_stop keep or append the place when pos < 0.
 * swatch: `samples` colours of a palette's own run, first stop to last; stack_ramp: the stack's composed ramp
 * at texel centres under amounts[8] (NULL: 1 for the primary, 0.5 for the rest). stack_from_params: an old
 * look's colours (the geode_viz_set_params frame's palette rows) as a stack that draws as it did, and its
 * amounts into amounts_out[8]. recent_push: the recent colours text with rgb first, no repeats, at most 24;
 * recent_read: its colours into out (at most max), the count; out NULL counts them all.
 * stack_set: the look's stack for every renderer of the process (an export keeps the one it pinned,
 * geode_viz_pin_engine_settings). While it is set, shader styles draw its ramp under the look's
 * paletteAmount0..7 rows, moved along by paletteShift; unset or unreadable, the look's old colour params draw. */
GEODE_API const char* geode_palette_library_text(void);
GEODE_API const char* geode_palette_library_entry(int index);
GEODE_API int         geode_palette_library_index(const char* id);
GEODE_API const char* geode_palette_colour_map_name(int index);
GEODE_API const char* geode_palette_stack_normalize(const char* stack);
GEODE_API const char* geode_palette_stack_unset(void);
GEODE_API int         geode_palette_stack_is_unset(const char* stack);
GEODE_API int         geode_palette_stack_layer_count(const char* stack);
GEODE_API const char* geode_palette_stack_layer(const char* stack, int layer);
GEODE_API const char* geode_palette_stack_resolve_default(const char* stack, const char* style_default);
GEODE_API const char* geode_palette_stack_add(const char* stack, const char* palette_line);
GEODE_API const char* geode_palette_stack_set_layer(const char* stack, int layer, const char* palette_line);
GEODE_API const char* geode_palette_stack_remove(const char* stack, int layer);
GEODE_API const char* geode_palette_stack_move(const char* stack, int layer, int delta);
GEODE_API const char* geode_palette_stack_set_blend(const char* stack, const char* blend);
GEODE_API const char* geode_palette_stack_set_edge(const char* stack, const char* edge);
GEODE_API const char* geode_palette_stack_set_full_range(const char* stack, int on);
GEODE_API void        geode_palette_stack_set(const char* stack);
/* The stack as Customize › Look draws it: "palettestack|1|<layers>|<room>|<BLEND>", a "layer|<i>|<name>" per
 * layer and a "pick|<index>|<name>" per library palette. add_library appends library palette `index` (the
 * stack unchanged out of range or when full). layer_swatch and library_swatch: `samples` colours of that
 * palette's own run, zeros out of range. Pure, any thread. */
GEODE_API const char* geode_palette_stack_editor(const char* stack);
GEODE_API const char* geode_palette_stack_add_library(const char* stack, int index);
/* The stack with the user's own colours added last as one palette named `name`. */
GEODE_API const char* geode_palette_stack_add_colours(const char* stack, const char* name, const int* rgb, int count);
GEODE_API void        geode_palette_stack_layer_swatch(const char* stack, int layer, int samples, int* out);
GEODE_API void        geode_palette_library_swatch(int index, int samples, int* out);
GEODE_API const char* geode_palette_from_colours(const char* source, const char* ref, const char* name, const int* rgb, int count);
GEODE_API const char* geode_palette_set_stop(const char* palette_line, int stop, int rgb, float pos);
GEODE_API const char* geode_palette_add_stop(const char* palette_line, int rgb, float pos);
GEODE_API const char* geode_palette_remove_stop(const char* palette_line, int stop);
GEODE_API const char* geode_palette_set_option(const char* palette_line, const char* option);
GEODE_API const char* geode_palette_rename(const char* palette_line, const char* name);
GEODE_API const char* geode_palette_spread(const char* palette_line);
GEODE_API void        geode_palette_swatch(const char* palette_line, int samples, int* out);
GEODE_API void        geode_palette_stack_ramp(const char* stack, const float* amounts, int samples, int* out);
GEODE_API const char* geode_palette_stack_from_params(const float* params, float* amounts_out);
GEODE_API const char* geode_colour_recent_push(const char* recent, int rgb);
GEODE_API int         geode_colour_recent_read(const char* recent, int* out, int max);
/* The config params schema (core/viz/params/ConfigTable.inc): the values the data stores hold beside the scene params
 * (LFO and envelope slots, the palette maker's gradient, the look's envelope, the Studio clip edit,
 * the preset morph), one row each, as a versioned '|'-separated line table:
 *   configs|<version>|<count>
 *   config|<group>|<index>|<name>|<FLOAT|INT|BOOL>|<default>|<min>|<max>|<NONE|SECONDS|FRACTION|TURNS|DEGREES|POINTS|TIMES>
 * index = the row's place in its group (for lfo and adsr the field's place in the slot's wire, viz/params/Config.hpp). A sibling of geode_params_schema_text, whose format it leaves alone. Static; parse once. */
GEODE_API const char* geode_config_schema_text(void);
/* The two-finger transform on the visual (core/viz/input/TouchTransform): a pinch's scale factor times Zoom, a twist in
 * degrees added to Rotation, each clamped to its schema range; a broken gesture value returns current unchanged. */
GEODE_API float       geode_touch_zoom(float current, float scale_factor);
GEODE_API float       geode_touch_rotation(float current, float degrees);
/* The range Customize's slider offers for the scene param `name` (core/viz/panel/ControlRange): its schema range, cut
 * short where the control stops early (Brightness and Intensity at 1 + the flash clamp's depth, Bloom at the
 * depth, Fly at -2..2). Writes min and max to out[2] and returns 1; 0, out untouched, for an unknown name. Pure. */
GEODE_API int         geode_params_control_range(const char* name, float* out);
/* One scene param by its name in a geode_viz_set_params frame (viz/params/Params SceneParams::get and ::set), so the shell
 * keeps no slot, kind or default of its own. defaults: every row at its default, geode_viz_param_count floats into
 * out. get: the param's value, an int or a flag as a float; NaN for a NULL frame or a name with no live row (unknown
 * or retired). set: `in` with the param at `value` (an int rounds, a flag is value > 0.5) into out and 1; 0, out
 * untouched, for a name with no live row. in and out may alias. Pure, any thread. */
GEODE_API void        geode_params_defaults(float* out);
GEODE_API float       geode_params_get(const float* frame, const char* name);
/* The same param as the int or the flag it stands for (SceneParams::asInt rounds, 0 for NaN; ::asFlag is value > 0.5);
 * 0 for a NULL frame or a name with no live row. */
GEODE_API int         geode_params_get_int(const float* frame, const char* name);
GEODE_API int         geode_params_get_flag(const float* frame, const char* name);
GEODE_API int         geode_params_set(const float* in, const char* name, float value, float* out);

/* The fingers on the visual (core/viz/input/TouchGesture). _to_ndc: points (x, y) px pairs of a width x height view,
 * rewritten in place as y-up NDC (geode_viz_set_touch's frame). _pair: two fingers at start, last and now, each
 * (ax, ay, bx, by) px, into out[6]: pan x, pan y and zoom since start, twist in radians since start, and the step
 * since last as a scale factor and degrees (for geode_touch_zoom / _rotation). _pulse: touch play's stir, step's
 * two points (4 floats, NDC) into xy and the ms to hold them; 0, xy untouched, once the stir is over. */
GEODE_API void        geode_touch_to_ndc(float* xy, int points, float width, float height);
GEODE_API void        geode_touch_pair(const float* start, const float* last, const float* now, float* out);
GEODE_API int         geode_touch_pulse(int step, float* xy);
/* The gestures on the visual and their actions (core/viz/input/GestureMap); gestures and actions are the shell's
 * CanvasGesture and GestureAction ordinals. _default_action: the owner's map, -1 for no such gesture. _choices: the
 * actions Settings offers gesture (the current one kept, first, when unsuited), up to capacity into out; returns
 * how many (at most GEODE_GESTURE_MAX_CHOICES), 0 for no such gesture. _for_key: an Android key code's gesture on
 * the stage, -1 for none (the key is someone else's); a sideways step's direction (+1 next, -1 previous, else 0)
 * into step_out. */
#define GEODE_GESTURE_COUNT 10
#define GEODE_GESTURE_ACTION_COUNT 12
#define GEODE_GESTURE_MAX_CHOICES 8
GEODE_API int         geode_gesture_default_action(int gesture);
GEODE_API int         geode_gesture_choices(int gesture, int current, int* out, int capacity);
GEODE_API int         geode_gesture_for_key(int key_code, int options_shown, int* step_out);
/* The gesture reader on the visual (core/viz/input/GestureRead), one per stage, reused for every gesture; no call
 * allocates but _create. The shell keeps the pointer loop and feeds it in px of the stage and its uptime ms.
 * _configure: the platform's touch slop, double-tap slop (px), density (px per dp), long-press timeout, double-tap
 * min time and timeout (ms); _area: the window size, the system's gesture strips (px from each window edge), the
 * stage's top-left in the window and whether that is known yet. Both are read at the next _down. _down: a first
 * finger at (x, y) at t_ms; actions holds each gesture's action ordinal (GEODE_GESTURE_COUNT of them), armed
 * whether the hold may run. Then, until GEODE_GESTURE_OVER: _wait_ms (now_ms: the uptime now, on the events' clock;
 * -1 no timer; else wait that long for the next event and call _tick if none comes) and _event: t_ms (< 0 keeps the last), the pressed fingers' (x, y) in the
 * event's order, live of them (only the first two are read), whether the first finger is still down, a pinch step's
 * factor and degrees into pinch[2]. Both answer GEODE_GESTURE_* bits. _release: 1 when touch play holds fingers that
 * must now lift (call it when following ends, cancelled or not). _end: the gesture to fire, -1 none, and into out[3]
 * a sideways step, 1 for a tap (the double tap may follow), and how long to wait for a second tap's down (0 not at
 * all). Then _second_down for each down in that wait and _second_move for the followed down's change (present 0 when
 * the event has none) answer GEODE_GESTURE_SECOND_*. */
typedef struct geode_gesture geode_gesture;
#define GEODE_GESTURE_RING         (1u << 0)   /* the ring shows, at the first down */
#define GEODE_GESTURE_HOLD         (1u << 1)   /* the hold fired */
#define GEODE_GESTURE_SWIRL        (1u << 2)   /* the swirl fired */
#define GEODE_GESTURE_OVER         (1u << 3)   /* the fingers are gone: call _end */
#define GEODE_GESTURE_PUBLISH      (1u << 4)   /* hand the fingers down now to touch play */
#define GEODE_GESTURE_RELEASE      (1u << 5)   /* touch play's fingers lift */
#define GEODE_GESTURE_PINCH        (1u << 6)   /* a pinch step in pinch[2] */
#define GEODE_GESTURE_SECOND_BOUNCE    0       /* too soon after the first lift: wait on */
#define GEODE_GESTURE_SECOND_FOLLOW    1       /* follow this down */
#define GEODE_GESTURE_SECOND_DOUBLE    2       /* a double tap */
#define GEODE_GESTURE_SECOND_NEW_TOUCH 3       /* no second tap: read it again as a new gesture from its own down */
#define GEODE_GESTURE_HOLD_FILL_MS 800         /* the ring's fill after the long-press timeout */
GEODE_API geode_gesture* geode_gesture_create(void);
GEODE_API void        geode_gesture_destroy(geode_gesture*);
GEODE_API void        geode_gesture_configure(geode_gesture*, float touch_slop, float double_tap_slop, float density,
                                              long long long_press_ms, long long double_tap_min_ms, long long double_tap_timeout_ms);
GEODE_API void        geode_gesture_area(geode_gesture*, float window_w, float window_h, float left, float top, float right,
                                         float bottom, float origin_x, float origin_y, int placed);
GEODE_API void        geode_gesture_down(geode_gesture*, float x, float y, long long t_ms, const int* actions, int armed);
GEODE_API long long   geode_gesture_wait_ms(const geode_gesture*, long long now_ms);
GEODE_API unsigned    geode_gesture_tick(geode_gesture*);
GEODE_API unsigned    geode_gesture_event(geode_gesture*, long long t_ms, const float* xy, int live, int primary_down, float* pinch);
GEODE_API int         geode_gesture_release(geode_gesture*);
GEODE_API int         geode_gesture_end(geode_gesture*, float split_y, int* out);
GEODE_API int         geode_gesture_second_down(geode_gesture*, float x, float y, long long t_ms);
GEODE_API int         geode_gesture_second_move(geode_gesture*, int present, float x, float y, int pressed);

/* Customize panel map (core/viz/panel/CustomizeMap): eleven categories, each a free Basics section and Pro sections, and
 * which control lives in which. geode_customize_text is its versioned '|'-separated line table (rows in
 * CustomizeMap.hpp). Static: the pointer stays valid for the process. geode_customize_shown lists what the panel
 * offers on style_id ("CATEGORY" and "CATEGORY/section" lines, plus "CATEGORY:roll" where the category's roll has
 * something to move); the pointer stays valid until this thread's next call.
 * geode_customize_randomize rolls one category's controls (named by its CustomizeMap name; an unknown one rolls
 * nothing), holding every other control and the newline-joined Customize keys in locked; 1 when it changed something.
 * In locked, here and in geode_params_randomize, a line "#CATEGORY" keeps every control of that category and
 * "#PRO" every control below a Basics (the roll without Pro). With tab NULL geode_params_randomize rolls what the
 * panel offers on the style, as every category's roll does.
 * geode_customize_reset puts back every field the category's controls write. Wire as geode_params_randomize. */
GEODE_API const char* geode_customize_text(void);
/* 1 when the control named key (a Customize key) is one a roll can move, so the panel offers to keep it. */
GEODE_API int         geode_customize_lockable(const char* key);
/* Every such key, one per line, each once: the panel asks once for all its rows instead of once per row. Static. */
GEODE_API const char* geode_customize_lockable_keys(void);
GEODE_API const char* geode_customize_shown(const char* style_id);
GEODE_API int         geode_customize_randomize(const char* style_id, const char* category, const char* locked, uint64_t seed,
                                                const float* in, float* out);
GEODE_API void        geode_customize_reset(const char* category, const float* in, float* out);
/* How many fields of the look in (same wire) differ from the defaults: the Customize header's "changed" count. */
GEODE_API int         geode_customize_changed_count(const float* in);
/* in with every field the category's controls write taken from the look in from (same wire); an unknown category
 * copies in. */
GEODE_API void        geode_customize_borrow(const char* category, const float* in, const float* from, float* out);
/* Customize's rows (core/viz/panel/CustomizeRows): what each control in keys (one CustomizeMap key a line, a section's order)
 * draws as on the look params (the geode_viz_set_params wire) and style_id: the widget, field, words, value, range,
 * default, unit, steps, or nothing where it does not draw. query is the panel's search text, transition 1 while a style
 * transition is chosen, liquid_mode the renderer's Liquid mode. Rows in CustomizeRows.hpp. Valid until this thread's
 * next customize rows call. */
GEODE_API const char* geode_customize_rows(const char* style_id, const char* keys, const char* query, int transition, int liquid_mode,
                                           const float* params);
/* 1 when the panel search query matches text (core/viz/panel/CustomizeRows matches): blank matches all, else a case-blind substring
 * of the trimmed query. */
GEODE_API int geode_customize_matches(const char* query, const char* text);
/* 1 when the panel search query has nothing in it (only spaces and tabs), so whole sections show. */
GEODE_API int geode_customize_query_blank(const char* query);
/* A style's display name, and a Styles page family's (core/viz/styles/catalogue/StyleNames). Valid until this thread's next call. */
GEODE_API const char* geode_style_label(const char* style_id);
GEODE_API const char* geode_style_family_label(const char* family);
/* Your colours' editor (core/viz/panel/PalettePanel): the editor's text state in, text state out. _start opens the look
 * params' own colours (its stops, else its hue ramp), _from_look the same, for the editor to start over from them, _open a saved palette's stops
 * (else its base and span as a ramp). _edit applies op (0 select arg, 1 set the selected stop to colour arg, 2 add a
 * copy of the selected stop, 3 remove the selected stop). _stops writes the state's colours to out (up to capacity)
 * and returns how many; _view is the editor as the panel draws it. Each text is valid until this thread's next
 * palette panel call. */
GEODE_API const char* geode_palette_maker_start(const float* params);
GEODE_API const char* geode_palette_maker_from_look(const float* params);
GEODE_API const char* geode_palette_maker_open(const int* stops, int count, float base, float span);
GEODE_API const char* geode_palette_maker_edit(const char* state, int op, int arg);
GEODE_API int         geode_palette_maker_stops(const char* state, int* out, int capacity);
GEODE_API const char* geode_palette_maker_view(const char* state);
/* A saved palette's colours as a stack layer (its stops, else its ramp) into out; returns how many. */
GEODE_API int         geode_palette_saved_colours(const int* stops, int count, float base, float span, int* out, int capacity);
/* A saved palette's strip in Your palettes: "RRGGBB" joined by ','. */
GEODE_API const char* geode_palette_saved_swatch(const int* stops, int count, float base, float span);
/* Look's palette list on a style the stack does not draw: saved_ids and saved_names one a line, the shell's order. */
GEODE_API const char* geode_palette_list_view(const float* params, const char* custom_id, const char* saved_ids,
                                              const char* saved_names);
/* The palette stack card on the look params: its rows (core/viz/panel/PalettePanel.hpp). saved_count is how many palettes
 * the user has saved. */
GEODE_API const char* geode_palette_stack_card(const char* stack, const float* params, int saved_count);
/* Randomize everything (core/viz/panel/RandomizeAll): the two-circle gesture's new style, look and every Customize category
 * rolled. Pure, seeded: the same arguments always give the same result; pass one seed to both calls.
 * geode_randomize_all_pick chooses among every catalogue style not in locked_styles (newline-joined ids the entitlement
 * locks), not current_style while another is open; then a look for it:
 * look_styles is the style id of each of the shell's presets, one per line, and *look_out the index of one made for
 * the chosen style (-1 when none).
 * Writes the style id into style_out (NUL-terminated, truncated to style_capacity); 1 when a style was picked.
 * geode_randomize_all_roll is base with every control locked keeps (keys and "#CATEGORY" lines) put back to its value in
 * before, then, when roll_categories is 1, every category's other controls that reach style_id rolled (0: the kept
 * controls alone). A "#PRO" line is the dice's free rule: the roll moves the free Basics only, and the controls below
 * them keep base's values (it is not Keep, so nothing it names is put back). Wire as geode_params_randomize; 1 when
 * out differs from base. */
GEODE_API int         geode_randomize_all_pick(uint64_t seed, const char* current_style, const char* locked_styles,
                                               const char* look_styles, char* style_out, int style_capacity,
                                               int* look_out);
GEODE_API int         geode_randomize_all_roll(const char* style_id, const char* locked, uint64_t seed, int roll_categories,
                                               const float* before, const float* base, float* out);
/* Customize's dice (core/viz/panel/PanelRoll), seeded natively per roll. kept is the user's lock set (keys and "#CATEGORY"
 * lines); basics_only is the roll without Pro. geode_panel_roll rolls category (a CustomizeMap name), or with NULL
 * everything, as geode_customize_randomize / geode_params_randomize. geode_panel_roll_all is geode_randomize_all_roll
 * with roll_categories 1 and the gesture's seed. Both return GEODE_PANEL_ROLL_* bits: CHANGED when out differs from
 * the look, WAVES when the steady waves are to roll next (geode_mod_waves_roll with all 0). */
#define GEODE_PANEL_ROLL_CHANGED 1
#define GEODE_PANEL_ROLL_WAVES   2
GEODE_API int         geode_panel_roll(const char* style_id, const char* category, const char* kept, int basics_only,
                                       const float* in, float* out);
GEODE_API int         geode_panel_roll_all(const char* style_id, const char* kept, int basics_only, uint64_t seed,
                                           const float* before, const float* base, float* out);
/* Music's "new personality": in with the reaction DNA's seed one further, into out. */
GEODE_API void        geode_params_reroll_reactions(const float* in, float* out);
/* Customize's undo and redo (core/viz/panel/ParamHistory), process-wide. record: the look before (count floats of the
 * params wire and its saved palette id, NULL as "") is about to be replaced at now_ms (the shell's monotonic clock);
 * edits within 700 ms fold into one entry, discrete 1 (a preset, a roll, a reset) is always its own. step: the look to
 * put up instead of current (redo 1: the last undone; entries recorded on another style_id are dropped, never applied), its frame into out (count floats) and its palette id returned,
 * valid until this thread's next step; NULL when there is none. Both report GEODE_PARAM_HISTORY_* bits through state
 * (record returns them too). */
#define GEODE_PARAM_HISTORY_UNDO 1
#define GEODE_PARAM_HISTORY_REDO 2
GEODE_API int         geode_param_history_record(const float* before, int count, const char* before_palette, const char* style_id,
                                                 long long now_ms, int discrete);
GEODE_API const char* geode_param_history_step(int redo, const float* current, int count, const char* current_palette,
                                               const char* style_id, float* out);
GEODE_API int         geode_param_history_state(void);
/* The macro bank (core/viz/modulation/MacroBank): the user's knobs, each a name, a value -1..1 and the links that say which
 * scene params it moves and how far each way. Pure: no viz handle, any thread. A bank travels as the versioned
 * '|'-separated line table MacroBank.hpp documents; the shell stores that string and decides nothing about it.
 * Every call that takes a bank reads any text (NULL, empty or unreadable = the default bank) and every edit
 * returns the whole new bank, normalized but otherwise unchanged when the edit is refused (an unknown knob or
 * target, a non-finite number, a full bank, removing a built-in knob, resetting one that is not). geode_macro_bank_default (the four built-in knobs, also Reset) and
 * geode_macro_bank_targets_text (what a link may name: target, param, range, default, scope, and the caps) are
 * static; every other pointer stays valid until this thread's next macro bank call.
 * target is a link's wire name (a ModTargets name, or the schema name of a param no modulation target writes); up and down are the offsets at a full turn each way, as signed fractions of
 * the target's range; geode_macro_bank_add_link links a target the knob does not move yet at the starting weights. geode_macro_bank_live lists "knob|<id>|<up 0/1>|<down 0/1>" per knob: whether turning it
 * that way moves anything on style_id. geode_macro_bank_editor is the knob editor's one read (MacroBank.hpp's
 * editorText: the caps, what each knob offers, its links and every target, with what reaches style_id).
 * geode_macro_bank_move_knob steps a knob delta places along the screen. geode_macro_bank_set_curve sets how a link
 * follows the knob (curve: LINEAR, EASE_IN, EASE_OUT or S_CURVE; up and down stay its range). geode_macro_bank_add_knob_for
 * makes a knob that moves one Customize control (a Customize key), named by native after it; geode_macro_bank_controls
 * lists every control a knob can move, its category and the knobs that move it (MacroBank.hpp's controlsText).
 * geode_macro_bank_roll gives a knob a random set of links (the same seed, the same set); geode_macro_bank_roll_values
 * gives every knob on screen that moves something on style_id a new turn (the same seed, the same turns).
 * geode_macro_bank_roll_ranges gives the knob's links a new direction, range and curve each, keeping their params;
 * geode_macro_bank_add_random links up to count more params, picked at random among those style_id reaches that
 * Customize does not keep (the process-wide keep set, geode_director_keep_set), each at a random range.
 * geode_macro_bank_hide_knob takes a knob off screen into the bank's library and geode_macro_bank_show_knob puts one
 * back (a built-in knob too, as built, when the bank lacks it). geode_macro_bank_auto_name
 * is the name native offers when a knob is saved (not a bank: the name alone). geode_macro_bank_set makes a bank the process-wide one every renderer
 * adds to its params each frame, after the director and before the LFOs. geode_macro_bank_live_mods is
 * what the waves and envelopes on a knob (LfoTarget MACRO) add to its turn on the on-screen surface's last frame, one row
 * 5 floats per knob (id, add, sources: 1 wave, 2 envelope, 3 both, lowest add, highest add) written to out, at most
 * capacity rows, the row count returned, for the knob's second arc; poll it from the UI. */
GEODE_API void        geode_macro_bank_set(const char* bank);
GEODE_API const char* geode_macro_bank_default(void);
GEODE_API const char* geode_macro_bank_targets_text(void);
GEODE_API const char* geode_macro_bank_normalize(const char* bank);
GEODE_API const char* geode_macro_bank_set_value(const char* bank, int knob_id, float value);
GEODE_API const char* geode_macro_bank_rename(const char* bank, int knob_id, const char* name);
GEODE_API const char* geode_macro_bank_add_knob(const char* bank, const char* name);
GEODE_API const char* geode_macro_bank_remove_knob(const char* bank, int knob_id);
GEODE_API const char* geode_macro_bank_duplicate_knob(const char* bank, int knob_id);
GEODE_API const char* geode_macro_bank_move_knob(const char* bank, int knob_id, int delta);
GEODE_API const char* geode_macro_bank_reset_knob(const char* bank, int knob_id);
/* The default bank, keeping the ids the given bank already spent. */
GEODE_API const char* geode_macro_bank_reset_all(const char* bank);
GEODE_API const char* geode_macro_bank_set_link(const char* bank, int knob_id, const char* target, float up, float down);
GEODE_API const char* geode_macro_bank_add_link(const char* bank, int knob_id, const char* target);
GEODE_API const char* geode_macro_bank_remove_link(const char* bank, int knob_id, const char* target);
GEODE_API const char* geode_macro_bank_set_curve(const char* bank, int knob_id, const char* target, const char* curve);
GEODE_API const char* geode_macro_bank_add_knob_for(const char* bank, const char* control);
GEODE_API const char* geode_macro_bank_controls(const char* bank);
GEODE_API const char* geode_macro_bank_roll(const char* bank, int knob_id, long long seed);
GEODE_API const char* geode_macro_bank_roll_values(const char* bank, const char* style_id, long long seed);
GEODE_API const char* geode_macro_bank_roll_ranges(const char* bank, int knob_id, long long seed);
GEODE_API const char* geode_macro_bank_add_random(const char* bank, int knob_id, const char* style_id, int count, long long seed);
GEODE_API const char* geode_macro_bank_hide_knob(const char* bank, int knob_id);
GEODE_API const char* geode_macro_bank_show_knob(const char* bank, int knob_id);
GEODE_API const char* geode_macro_bank_auto_name(const char* bank, int knob_id);
GEODE_API const char* geode_macro_bank_live(const char* bank, const char* style_id);
GEODE_API const char* geode_macro_bank_editor(const char* bank, const char* style_id);
GEODE_API int         geode_macro_bank_live_mods(float* out, int capacity);
/* A knob's turn plus what modulation adds to it, held to -1..1: what the engine draws it at. */
GEODE_API float       geode_macro_bank_modulated(float turn, float add);
/* Background image bake (core/viz/passes/Underlay), before geode_viz_set_underlay_rgba: pure, no viz handle, any
 * thread. The shell decodes the picture, cuts it to geode_underlay_crop_rect (out = x, y, width, height of the
 * centred crop of src to the target's aspect) and scales it to the render size, then geode_underlay_finish
 * blurs and dims those ARGB_8888 pixels (0xAARRGGBB, row-major, width*height) in place. The clamps are the
 * controls' ranges: amount and dim 0..1 (NaN = 0), blur 0..geode_underlay_blur_max(). */
GEODE_API void        geode_underlay_crop_rect(int src_w, int src_h, int target_w, int target_h, int* out_xywh);
/* The power-of-two decode subsample for a src picture: fit 0, the largest leaving both sides at least the target
   (the underlay); fit 1, the smallest bringing both within it (the artwork). 1 for a non-positive size. */
GEODE_API int         geode_underlay_sample_size(int src_w, int src_h, int target_w, int target_h, int fit);
GEODE_API void        geode_underlay_finish(uint32_t* pixels, int width, int height, int blur_radius, float dim);
GEODE_API float       geode_underlay_clamp_amount(float amount);
GEODE_API float       geode_underlay_clamp_dim(float dim);
GEODE_API int         geode_underlay_clamp_blur(int radius);
GEODE_API int         geode_underlay_blur_max(void);
/* The blends geode_viz_set_underlay_rgba takes, 0..count-1 (0 screen, 1 multiply, 2 add), and the name each is
 * saved under (a static string). An unknown or null name is 0; an index out of range gives 0's name. */
GEODE_API int         geode_underlay_blend_count(void);
GEODE_API const char* geode_underlay_blend_name(int blend);
GEODE_API int         geode_underlay_blend_of(const char* name);

/* Export resampler (audio/dsp/PcmResampler): interleaved 16-bit PCM to the AAC encoder's rate; band-limited
 * (Kaiser-windowed sinc, polyphase) both ways, cutoff below the lower Nyquist rate (#375). Streaming: state
 * carries across calls, and n frames in give ceil(n * out / in) out however they are chunked, behind a delay
 * of PcmResampler::halfTaps() input frames (about 0.5 to 1.1 ms). Any one thread at a time. There is no flush: the filter's tail at the end is
 * dropped, as the export always dropped a partial output frame. */
typedef struct geode_resampler geode_resampler;
GEODE_API geode_resampler* geode_resampler_create(int in_rate, int out_rate, int channels);  /* NULL on bad args */
GEODE_API void             geode_resampler_destroy(geode_resampler*);
GEODE_API int              geode_resampler_channels(geode_resampler*);   /* 0 for NULL */
GEODE_API size_t           geode_resampler_max_out_frames(geode_resampler*, size_t in_frames);
/* Returns the frames written to out, or -1 (writing nothing) when out_capacity_frames is under
 * geode_resampler_max_out_frames(in_frames). */
GEODE_API int64_t          geode_resampler_process(geode_resampler*, const int16_t* in, size_t in_frames,
                                                   int16_t* out, size_t out_capacity_frames);

/* Export loudness meter (audio/dsp/Loudness): ITU-R BS.1770-4 integrated loudness and true peak of a whole
 * programme, fed interleaved float PCM in -1..1. Mono, stereo and 5.1 only. Any one thread at a time. */
typedef struct geode_loudness geode_loudness;
typedef struct geode_loudness_report {
    int     has_integrated;     /* 0 when every block sits under the -70 LUFS absolute gate */
    double  integrated_lufs;
    double  true_peak_dbtp;
    double  sample_peak_dbfs;
    int64_t measured_ms;
    int     gated_block_count;
} geode_loudness_report;
GEODE_API int             geode_loudness_supports(int channels);
GEODE_API geode_loudness* geode_loudness_create(int sample_rate, int channels);   /* NULL on bad args */
GEODE_API void            geode_loudness_destroy(geode_loudness*);
GEODE_API void            geode_loudness_feed(geode_loudness*, const float* interleaved, size_t samples);
/* The same feed from interleaved s16 (a decoder's default output), scaled by 1/32768 as audio/dsp/PcmConvert
 * does, so the result equals converting first and calling geode_loudness_feed. */
GEODE_API void            geode_loudness_feed_s16(geode_loudness*, const int16_t* interleaved, size_t samples);
GEODE_API int             geode_loudness_has_block(geode_loudness*);   /* 1 once a 400 ms block exists */
GEODE_API void            geode_loudness_finish(geode_loudness*, geode_loudness_report* out);   /* non-destructive */

/* Encoder fallback ladder (output/EncoderLadder, #607): every attempt an export makes at opening the video
 * encoder, in order, until configure accepts one: each candidate codec's ladder in turn. ceilings[i] is codec
 * i's upper rate for the size from its capability table (MediaCodecInfo.VideoCapabilities), 0 or less when
 * unknown; at most 2 codecs are read. Writes up to capacity attempts (4 at most) as the codec's index, frame
 * rate and bit rate, and returns how many; 0 for NULL arrays. Pure, any thread. */
GEODE_API int             geode_encoder_attempts(int requested_fps, int bit_rate, const int* ceilings,
                                                 int codec_count, int* out_codec, int* out_fps,
                                                 int* out_bit_rate, int capacity);

/* Export post-passes (viz/frame/ExportPasses, #442): the non-Pro watermark and a loop render's seam, each a
 * frame::Pass on the export surface. The pure
 * calls (text_px, tile_*, seam_weight) run anywhere; every other call needs the export's GL context current
 * on the calling thread, destroy included (it deletes GL names). A create always returns a handle (NULL only
 * when out of memory); geode_export_pass_error says whether it built ("" when it did). None of them times a
 * frame: they draw the frame the caller hands them, when it does. */
typedef struct geode_export_pass geode_export_pass;
GEODE_API const char*         geode_export_pass_error(const geode_export_pass*);   /* "" when clean or NULL */
GEODE_API void                geode_export_pass_destroy(geode_export_pass*);
/* Watermark placement (Settings › Preferences) and corner, and what the frames are (`use`). */
#define GEODE_WATERMARK_NONE 0
#define GEODE_WATERMARK_TILED 1
#define GEODE_WATERMARK_CORNER 2
#define GEODE_WATERMARK_END_CARD 3
#define GEODE_WATERMARK_TOP_LEFT 0
#define GEODE_WATERMARK_TOP_RIGHT 1
#define GEODE_WATERMARK_BOTTOM_LEFT 2
#define GEODE_WATERMARK_BOTTOM_RIGHT 3
#define GEODE_WATERMARK_USE_WHOLE 0   /* a whole video, its end known */
#define GEODE_WATERMARK_USE_SEGMENT 1 /* a Studio scene segment, layered later */
#define GEODE_WATERMARK_USE_LOOP 2
#define GEODE_WATERMARK_USE_LIVE 3    /* Record */
/* Whether frames are marked (the entitlement requires it, or Pro adds it) and where: GEODE_WATERMARK_NONE, or
 * the placement asked for, an end card becoming a corner mark where the video's end is not known. Pure. */
GEODE_API int                 geode_export_watermark_placement(int required, int pro_adds, int placement, int use);
/* The raster the placement wants. paint: the text size for a frame's short side, its alpha (0..255), letter
 * spacing in ems and its shadow's radius and alpha. raster: for the text measured with that paint (its width, and
 * the font's ascent and descent), the tile (the tiled pattern's a power of two), its rotation in degrees about
 * the pivot, and where the text is drawn. Pure. */
typedef struct geode_watermark_paint {
    float text_px;
    int   alpha;
    float letter_spacing_em;
    float shadow_radius_px;
    int   shadow_alpha;
} geode_watermark_paint;
typedef struct geode_watermark_raster {
    int   width;
    int   height;
    float angle_degrees;
    float pivot_x;
    float pivot_y;
    float text_x;
    float text_y;
} geode_watermark_raster;
GEODE_API void                geode_export_watermark_paint(int short_side, int placement, geode_watermark_paint* out);
GEODE_API void                geode_export_watermark_raster(float text_width, float ascent, float descent, float text_px,
                                                            int placement, geode_watermark_raster* out);
/* rgba: the tile, tile_w x tile_h premultiplied RGBA8, top row first (an ARGB_8888 Bitmap's pixels). length_ms
 * is the video's (an end card's last seconds), 0 when unknown. */
GEODE_API geode_export_pass*  geode_export_watermark_create(AAssetManager*, const uint8_t* rgba, int tile_w, int tile_h,
                                                            int placement, int corner, int64_t length_ms);
/* Over the default framebuffer (the encoder surface), frame_w x frame_h pixels, after the scene; time_ms is the
 * frame's time in the video. */
GEODE_API void                geode_export_watermark_draw(geode_export_pass*, int frame_w, int frame_h, int64_t time_ms);
/* Loop seam: up to `requested` full-frame textures of stash (returns how many fitted), filled by capture from
 * the default framebuffer and laid back over it by blend at closing frame `tail` of `seam_frames`. */
GEODE_API geode_export_pass*  geode_export_seam_create(AAssetManager*, int width, int height);
GEODE_API int                 geode_export_seam_allocate(geode_export_pass*, int requested);
GEODE_API void                geode_export_seam_capture(geode_export_pass*, int index);
GEODE_API void                geode_export_seam_blend(geode_export_pass*, int index, int tail, int seam_frames);

/* A capture's digital silence (sound/Silence): heard_at moved to now when the peak of the block it just wrote
 * (the ring's write answer) is above digital zero; quiet is 4 s of none since then. Start heard_at at the
 * capture's start. Pure, any thread. */
GEODE_API long long       geode_capture_heard_at(float peak, long long now_ms, long long heard_at_ms);
GEODE_API int             geode_capture_quiet(long long now_ms, long long heard_at_ms);     /* 1 = quiet */
/* A capture's format plan (audio/ingest/CapturePlan): the formats to ask AudioRecord for, first to try first.
 * device_rate_hz <= 0 is unknown; stereo 0 plans mono only. Writes min(count, capacity) rows of
 * GEODE_CAPTURE_FORMAT_INTS ints (rate Hz, channels, 1 = float / 0 = 16-bit) and returns the count.
 * buffer_bytes: the AudioRecord buffer for a format whose platform minimum is min_buffer_bytes, 0 = skip
 * the format. read_frames: the frames one read takes. Pure, any thread. */
#define GEODE_CAPTURE_FORMAT_INTS 3
GEODE_API int             geode_capture_plan(int device_rate_hz, int stereo, int* out, int capacity);
GEODE_API int             geode_capture_buffer_bytes(int min_buffer_bytes, int channels);
GEODE_API int             geode_capture_read_frames(void);
/* Live input (audio/ingest/MicInput): the microphone as a native input stream whose callback writes the
 * process's mic ring (geode_pcm_ring_shared(GEODE_PCM_RING_MIC)), declared below.
 * Opens the first format of the capture plan the device gives (mono, device_rate_hz first, <= 0 unknown),
 * with the UNPROCESSED source when unprocessed != 0, else VOICE_RECOGNITION; reopens after a disconnect.
 * Call only once RECORD_AUDIO is granted. start: 1 when running (also when it already was). stop: no write
 * after it returns. active: 0 after stop or once a closed stream could not be opened again. Main thread. */
GEODE_API int             geode_mic_start(int device_rate_hz, int unprocessed);
GEODE_API void            geode_mic_stop(void);
GEODE_API int             geode_mic_active(void);

/* What the player tells every live renderer's sound engine that the tap cannot hear (sound/PlayerSignals).
 * A track boundary: a new track (or the same one again) is a new play with `seed`; each live engine forgets
 * the tempo, key and sections at its next frame and fires sectionChange at full strength, crossfaded and
 * gapless tracks included. Returns the boundary's epoch (1, 2, …). The fade gain, 0..1: the player's pause
 * and skip fades times the crossfade, which the tap (before the volume) never hears; the frame carries it as
 * fadeGain, a reaction source. Player is source: 1 while the player writes the app's ring, 0 once the mic or
 * another app's audio takes it; a pause then holds the tempo, key and sections (no 2 s silence re-warm),
 * and fadeGain reads 1 under the others. All process-wide and latched, any thread; an export reads none. */
GEODE_API long long       geode_sound_track_boundary(long long seed);
GEODE_API void            geode_sound_set_player_source(int on);
/* Settings › Audio's listening tuning (sound/Tuning): the one schema of GeodeSoundTuning. A slot is one
 * float of the struct in field order, GEODE_SOUND_TUNING_FLOATS of them; each call touches the first
 * min(count, slots) and ignores the rest. The engine clamps each slot into its range, a NaN or an
 * infinity becoming the slot's default. The shell keeps only the saved floats (NULL for the defaults) and
 * resolves them here, so a slider never shows a value the engine would not use. Resolve writes every
 * slot to out: a save of GEODE_SOUND_TUNING_FLOATS slots, or one short (from before the input trim, which
 * then reads 0 dB), clamped; any other save the defaults. Returns 1 when the save was used. */
GEODE_API int             geode_sound_tuning_slots(void);
GEODE_API void            geode_sound_tuning_defaults(float* out, int count);
GEODE_API void            geode_sound_tuning_ranges(float* min_out, float* max_out, int count);
GEODE_API int             geode_sound_tuning_resolve(const float* saved, int count, float* out);

/* Fixed-rate frame clock (viz/frame/FixedRateClock): Cast's pacing, PTS and audio cut. Pure: the caller
 * owns GEODE_FRAME_CLOCK_STATE int64s (start ns, next slot, fps) and hands in now on one monotonic clock.
 * At most one frame per slot: a late take drops the missed slots instead of bursting. */
#define GEODE_FRAME_CLOCK_STATE 3
GEODE_API void            geode_frame_clock_start(int64_t* state, int fps, int64_t now_ns);
GEODE_API int64_t         geode_frame_clock_wait_ns(const int64_t* state, int64_t now_ns);   /* 0 = due now */
GEODE_API int64_t         geode_frame_clock_take(int64_t* state, int64_t now_ns);   /* the frame's PTS, ns */
GEODE_API int64_t         geode_frame_clock_audio_until_us(const int64_t* state);   /* end of the last frame taken */
/* The Cast stream's format: out[0] width, out[1] height, out[2] fps, out[3] keyframe interval (s), out[4] the
 * video bit rate, out[5] the audio bit rate (bit/s). out holds GEODE_CAST_FORMAT_INTS ints. */
#define GEODE_CAST_FORMAT_INTS 6
GEODE_API void            geode_cast_format(int* out);
/* What Customize's transition picker lists (core/viz/passes/TransitionLibrary, encodeTransitionPicker): the built-in
   ids it offers (TransitionChoice's pickerIds: no "cut", then "random"), RS, then the GL Transitions library's
   names (gl_transitions.json, read from the APK once per process), each list's entries separated by US. UTF-8.
   Without assets, or with the asset missing, the library list is empty. The pointer stays valid for the process. */
GEODE_API const char*     geode_transition_picker(AAssetManager* assets);
/* Settings › Performance (core/viz/frame/Quality): mode 0 Saver, 1 Balanced, 2 High. */
#define GEODE_PERF_TIER_FLOATS 4
/* Writes render scale, fps cap, march scale, post FX (0/1); returns the count, 0 for a bad mode. */
GEODE_API int             geode_perf_tier(int mode, float* out, int capacity);
/* The first-run mode for this phone. total_ram_bytes 0 = unknown. */
GEODE_API int             geode_perf_mode_for_device(long long total_ram_bytes, int low_ram, int media_performance_class);
/* The mode these values are (the tier's fps cap as an int, post_fx 0/1), or -1 for none: the tab reads "Custom". */
GEODE_API int             geode_perf_mode_of(float render_scale, int fps_cap, float march_scale, int post_fx);
/* The render and march scales the tab offers, ascending, into out (up to capacity); returns how many. */
#define GEODE_PERF_SCALE_CHOICES 3
GEODE_API int             geode_perf_scale_choices(float* out, int capacity);
/* Settings › Performance's live frame rate (core/viz/frame/LiveFps): the whole app's rate while the in-app stage
   is up, never an export or Cast. `rates` are the refresh rates of the stage's display at
   its current resolution (Display.getSupportedModes), `count` of them; NULL or 0 is an unknown display. choices: the
   rates offered, ascending, into out (up to capacity); returns how many: every refresh rate divided by a whole number,
   30 to 120 (30 and 60 for an unknown display), then GEODE_LIVE_FPS_FOLLOW ("Follow
   device") always. cap: the stored pick as this display runs it: an offered rate or Follow as it is, anything else
   the nearest offered rate (the lower on a tie), any other value <= 0 the default. refresh: the refresh rate the
   window asks the display for while `fps` is the live rate, the lowest of `rates` that shows it evenly; 0 (no
   preference) for Follow device or a rate no mode shows evenly. */
#define GEODE_LIVE_FPS_DEFAULT 60
#define GEODE_LIVE_FPS_FOLLOW (-1)
#define GEODE_LIVE_FPS_MAX_CHOICES 12
GEODE_API int             geode_live_fps_choices(const float* rates, int count, int* out, int capacity);
GEODE_API int             geode_live_fps_cap(int stored_fps, const float* rates, int count);
GEODE_API float           geode_live_fps_refresh(int fps, const float* rates, int count);
/* Auto's style timer (core/viz/director/AutoTimer), one per source, process-wide: the wait before the next look is the
 * user's interval plus a random spread drawn per look (a quarter of it, 10 s at most), never shorter than the
 * interval. now_ms is the shell's monotonic clock. restart: a look just went up. due: 1 once the wait is over
 * (it restarts itself); running = 0 (off, paused, nothing to step to) is never due. hold_s: how long the look
 * on screen is expected to show, 0 with no timer running. */
#define GEODE_AUTO_TIMER_PLAYLIST            0
#define GEODE_AUTO_TIMER_RANDOM              1
GEODE_API void            geode_auto_timer_restart(int timer, long long now_ms);
GEODE_API int             geode_auto_timer_due(int timer, long long now_ms, int interval_s, int running);
GEODE_API double          geode_auto_timer_hold_s(void);
/* Auto is about to bring `key` on (a style id): the renderer that switches to it within a few seconds plays Auto's longer fade (core/viz/director/TransitionChoice). */
GEODE_API void            geode_auto_mark_change(const char* key);
/* Auto's random draw (core/viz/director/AutoDraw): an index in 0..count-1 into the shell's pool of styles and saved looks,
 * never on_screen (the entry on screen, -1 for none) while count > 1. -1 for an empty pool. */
GEODE_API int             geode_auto_pick(int count, int on_screen);
/* Auto visuals' decisions (core/viz/director/AutoPlan), process-wide over the two timers above. now_ms as above; playing 0
 * (nothing plays) and Customize open (editing_set 1) hold every change.
 * interval_s: a timer interval held to its range. editing_set: Customize opened (1) or closed (0, both waits start
 * over). hand_pick: a style or a look picked by hand (both waits start over). playlist_tick: the playlist
 * entry (of count) to put up now, or -1; it runs while enabled with two entries or more. playlist_step: a step by
 * hand or a moment's (step < 0 back): the entry, or -1 with fewer than two. random_tick: 1 when the timed draw is due.
 * moment: a director moment on the look on screen (style, count floats of its params, its saved palette id): a
 * GEODE_AUTO_MOMENT_*; NONE when the user changed the look since Auto's last change. left_as: Auto's own change is on
 * screen now (after the shell put it up).
 * draw: Auto's draw (restarting the timed wait) among the sources (GEODE_AUTO_DRAW_* bits): the AutoDraw styles,
 * the saved looks (preset_styles, each one's style id a line),
 * none in locked_styles (one a line, what the entitlement refuses). preset_on_screen is the first saved look equal
 * to the one on screen (-1 for none). KEEP_LOOK is a moment's draw: no saved look,
 * and the style the director asks for (geode_auto_pick_among). Writes the entry's kind (GEODE_AUTO_KIND_*) and its
 * index in that list (the AutoDraw list for a style) to out2 and returns its style id, valid until this thread's
 * next draw; NULL for none. */
#define GEODE_AUTO_MOMENT_NONE               0
#define GEODE_AUTO_MOMENT_PLAYLIST           1
#define GEODE_AUTO_MOMENT_RANDOM             2
#define GEODE_AUTO_DRAW_STYLES               1
#define GEODE_AUTO_DRAW_PRESETS              2
#define GEODE_AUTO_DRAW_KEEP_LOOK            8
#define GEODE_AUTO_KIND_STYLE                0
#define GEODE_AUTO_KIND_PRESET               1
#define GEODE_AUTO_INTERVAL_MIN_S 5
#define GEODE_AUTO_INTERVAL_MAX_S 300
GEODE_API int             geode_auto_interval_s(int requested_s);
GEODE_API void            geode_auto_editing_set(int on, long long now_ms);
GEODE_API void            geode_auto_hand_pick(long long now_ms);
GEODE_API int             geode_auto_playlist_tick(int count, int enabled, int playing, int interval_s, long long now_ms);
GEODE_API int             geode_auto_playlist_step(int count, int step, long long now_ms);
GEODE_API int             geode_auto_random_tick(int enabled, int playing, int interval_s, long long now_ms);
GEODE_API int             geode_auto_moment(const char* style, const float* frame, int count, const char* palette_id,
                                            int playing, int playlist_enabled);
GEODE_API void            geode_auto_left_as(const char* style, const float* frame, int count, const char* palette_id);
GEODE_API const char*     geode_auto_draw(const char* locked_styles, const char* current_style, const char* preset_styles,
                                          int preset_on_screen,
                                          int sources, long long now_ms, int* out2);
/* Visuals › Styles (core/viz/styles/catalogue/StylePick). random: Random's draw among count styles (family[i] the family index of
 * each, open[i] nonzero when the entitlement keeps it; either may be null), never on_screen (-1 for none); every
 * family, or with family_only the on-screen style's family alone; a kept style while the pool has one. -1 for
 * none. With family_only in a family of one style it never leaves the family: GEODE_STYLES_ROLL_INSIDE when that
 * style has variety of its own to roll (inner_count: its variations, 0 for none;
 * inner_on_screen: the one showing, -1 for none; it rolls when one is not on screen), else -1. lead: writes
 * into out (count ints) the order a list shows, lead first (the item on screen, or the family holding it), the
 * rest in order; lead outside the list keeps the order. */
#define GEODE_STYLES_ROLL_INSIDE             (-2)
GEODE_API int             geode_styles_random(const int* family, const unsigned char* open, int count, int on_screen,
                                              int family_only, int inner_count, int inner_on_screen);
/* Random family: a family other than the one on screen's (equally likely, whatever its size), then a style in it;
 * never on_screen; a kept style while one is left; -1 for none. */
GEODE_API int             geode_styles_random_family(const int* family, const unsigned char* open, int count,
                                                     int on_screen);
GEODE_API void            geode_styles_lead(int count, int lead, int* out);

/* The app's own stage (viz/StageRun): flags are the GEODE_STAGE_* bits the shell observed of its Activity and
 * of Cast; the answer is GEODE_STAGE_DRAWS while somebody can see the stage (resumed, or started in a
 * picture-in-picture window or on a second screen) and GEODE_STAGE_CAST_LEADS_AUTO while a cast runs and the
 * stage does not draw, when the cast's renderer counts the auto-director's style moments instead. Pure. */
#define GEODE_STAGE_STARTED                  (1u << 0)
#define GEODE_STAGE_RESUMED                  (1u << 1)
#define GEODE_STAGE_PIP                      (1u << 2)
#define GEODE_STAGE_SECOND_SCREEN            (1u << 3)
#define GEODE_STAGE_CASTING                  (1u << 4)
#define GEODE_STAGE_DRAWS                    (1u << 0)
#define GEODE_STAGE_CAST_LEADS_AUTO          (1u << 1)
GEODE_API unsigned        geode_stage_run(unsigned flags);

/* Where the shell lays the stage's view out (viz/StageRun host): flags are GEODE_STAGE_HOST_*, bounds the Visuals
 * screen's rect in window pixels (left, top, right, bottom; empty before Visuals was laid out), window_w x window_h
 * the window. rect_out gets the view's rect (left, top, right, bottom; empty: 0 x 0): the bounds, or while casting
 * the window when there are none (the tee turns and crops the picture to the stream, tee::place). Returns
 * GEODE_STAGE_HOST_HIDDEN, _FAINT (drawing under the other screens) or _OPAQUE. Pure. */
#define GEODE_STAGE_HOST_SHOWN               (1u << 0)
#define GEODE_STAGE_HOST_CASTING             (1u << 1)
#define GEODE_STAGE_HOST_HIDDEN              0
#define GEODE_STAGE_HOST_FAINT               1
#define GEODE_STAGE_HOST_OPAQUE              2
GEODE_API int             geode_stage_host(unsigned flags, const int* bounds, int window_w, int window_h, int* rect_out);

/* The connected display the stage shows on (viz/StageRun): flags per display are the GEODE_STAGE_DISPLAY_* bits
 * plus its failures so far << GEODE_STAGE_DISPLAY_FAILURE_SHIFT. pick returns the index of the display to use
 * (preferred_id while it qualifies) or -1; presents is 1 when the GEODE_STAGE_PRESENT_* bits all hold. Pure. */
#define GEODE_STAGE_DISPLAY_PRESENTATION     (1u << 0)
#define GEODE_STAGE_DISPLAY_DEFAULT          (1u << 1)
#define GEODE_STAGE_DISPLAY_HOST             (1u << 2)
#define GEODE_STAGE_DISPLAY_ON               (1u << 3)
#define GEODE_STAGE_DISPLAY_FAILURE_SHIFT    8
#define GEODE_STAGE_PRESENT_WANTED           (1u << 0)
#define GEODE_STAGE_PRESENT_ENTITLED         (1u << 1)
#define GEODE_STAGE_PRESENT_FOUND            (1u << 2)
GEODE_API int             geode_stage_display_eligible(unsigned flags);
GEODE_API int             geode_stage_pick_display(const int* ids, const unsigned* flags, int count, int preferred_id);
GEODE_API int             geode_stage_presents(unsigned flags);

/* What the stage's "next / previous style" steps (core/viz/StageStep; owner 2026-11-12: it walks what the open
 * Visuals tab lists). page is the open page (GEODE_STAGE_STEP_PAGE_*), flags the GEODE_STAGE_STEP_* facts,
 * look_count the saved looks the user may apply; the answer is GEODE_STAGE_STEP_STYLES or _LOOKS. index: the item
 * step (+1 next, -1 previous) places from here among count items, wrapping, skipping here and every open[i] == 0
 * (open null: all open), from an end when here is outside the list; -1 when nothing else is open. */
#define GEODE_STAGE_STEP_PAGE_STYLES         0
#define GEODE_STAGE_STEP_PAGE_CUSTOMIZE      1
#define GEODE_STAGE_STEP_PAGE_PRESETS        2
#define GEODE_STAGE_STEP_PAGE_TEXTURES       4
#define GEODE_STAGE_STEP_PAGE_SETTINGS       5
#define GEODE_STAGE_STEP_SHEET_SHOWN         (1u << 0)
#define GEODE_STAGE_STEP_ON_LOOK             (1u << 2)
#define GEODE_STAGE_STEP_STYLES              0
#define GEODE_STAGE_STEP_LOOKS               1
GEODE_API int             geode_stage_step_target(int page, unsigned flags, int look_count);
GEODE_API int             geode_stage_step_index(const unsigned char* open, int count, int here, int step);

/* Memory pressure (#854): Android's onTrimMemory level, forwarded as is. Native acts on TRIM_MEMORY_UI_HIDDEN (20)
   and TRIM_MEMORY_BACKGROUND (40) only and ignores every other level. Every renderer in the process then sheds its
   spare scenes on its own GL thread at its next frame; what it draws, the layer and the requested style stay. A surface
   that is paused draws no frame: it sheds them at its pause instead (geode_viz_trim_spares). Any thread. */
GEODE_API void            geode_memory_trim(int android_level);
/* ── Export planning and PCM maths (core/output; ports) ──────────────────────────────────
 * Pure functions, any thread, no GL. The Kotlin exporters (VideoExporter, LoopRender, LoopExtend,
 * AudioTranscoder) call these instead of carrying the maths themselves; the classes behind them are
 * output/FrameClock, LoopPlan, EncodePlan, audio/dsp/PcmConvert and PcmTiming. */

/* Frame clock (output/FrameClock): each value from the frame index alone, so nothing accumulates. */
GEODE_API int64_t geode_export_total_frames(int64_t duration_us, int fps);            /* >= 1 */
GEODE_API int64_t geode_export_frame_pts_ns(int64_t frame, int fps);                  /* floor(n*1e9/fps) */
GEODE_API int64_t geode_export_frame_time_ms(int64_t frame, int fps);                 /* floor(n*1000/fps) */
/* The pre-roll before a range starting at range_start_ms (output/PreRoll): the engine hears *heard_ms of the
 * track from *from_ms before frame 0, then geode_viz_end_pre_roll. */
GEODE_API void    geode_export_pre_roll(int64_t range_start_ms, int64_t* from_ms, int64_t* heard_ms);

/* Long-form loops (output/LoopPlan). */
typedef struct geode_loop_spec {
    int64_t loop_ms;
    int64_t crossfade_ms;
    int32_t fps;
    int32_t loop_frames;       /* derived: filled by the calls below */
    int32_t crossfade_frames;
    int32_t source_frames;
    int64_t loop_duration_us;
} geode_loop_spec;
/* Loose UI numbers clamped into a valid spec (30 s..5 min, 200 ms..5 s and <= half the loop, 24..60 fps). */
GEODE_API void    geode_loop_spec_of(int64_t loop_ms, int64_t crossfade_ms, int fps, geode_loop_spec* out);
/* Fills the derived fields of a spec as given (no clamping): loop, crossfade and source frames, duration. */
GEODE_API void    geode_loop_spec_fill(geode_loop_spec* spec);
/* The ranges a loop render takes, for the shell's controls: the shortest and longest loop (ms), the shortest,
 * longest and default crossfade (ms), the most drift stops. Writes min(6, capacity) and returns 6. */
GEODE_API int     geode_loop_limits(int64_t* out, int capacity);
GEODE_API float   geode_loop_seam_weight(int tail, int seam_frames);
/* The repeats that fill total_us, spread over `loops` drift stops. Writes min(repeats, capacity) offsets and
 * loop indices (either may be NULL for a count) and returns the repeat count. */
GEODE_API int     geode_loop_extend_plan(int64_t loop_us, int64_t total_us, int loops,
                                         int64_t* out_offsets_us, int32_t* out_loop_index, int capacity);
/* What the long file will weigh (every repeat copies the loop again, plus 2 % for the sample tables), and
 * the video bit rate that keeps a file total_us long under limit_bytes. */
GEODE_API int64_t geode_loop_estimated_bytes(int64_t loop_us, int64_t total_us, int64_t loop_bytes,
                                             int64_t soundtrack_bytes);
GEODE_API int     geode_loop_fitting_bit_rate(int64_t total_us, int64_t limit_bytes, int64_t soundtrack_bytes);

/* What a loop extend decides while it copies encoded samples (output/LoopExtend); the copy itself is output/LoopMux,
 * which calls these in C++, so none has a JNI caller. geode_loop_repeat_samples: the leading samples of one repeat, times shifted by offset_us, that
 * land before limit_us; fills out_pts_us and out_flags (muxer flags from the extractor's) and returns how many.
 * geode_loop_clip_starts: where back-to-back clips begin, then the total, in out[clip_count + 1].
 * geode_loop_audio_due: the soundtrack samples due before until_us as (clip, sample) pairs with their time in the
 * soundtrack, from and advancing cursor[2] = {clip, sample}; sample_pts_us holds every clip's sample times, clip after clip. Returns how many
 * it wrote (fewer than capacity = caught up). The progress calls give the 0..1 bar position; the sample buffer
 * is the bytes one sample read may need. */
GEODE_API int     geode_loop_repeat_samples(const int64_t* sample_times_us, const int* extractor_flags, int count,
                                            int64_t offset_us, int64_t limit_us, int64_t* out_pts_us, int* out_flags);
GEODE_API void    geode_loop_clip_starts(const int64_t* durations_us, int clip_count, int64_t* out);
GEODE_API int     geode_loop_audio_due(const int64_t* clip_starts_us, const int* sample_counts, int clip_count,
                                       const int64_t* sample_pts_us, int* cursor, int64_t until_us, int* out_clip,
                                       int* out_sample, int64_t* out_pts_us, int capacity);
GEODE_API float   geode_loop_copy_progress(int done, int repeats);
GEODE_API float   geode_loop_transcode_progress(int index, float clip_progress, int clip_count);
GEODE_API int     geode_loop_sample_buffer_bytes(int declared_bytes, int width, int height);

/* Loudness advice (output/LoudnessAdvice): what an export's measured loudness asks of a platform target
 * (0 Leave as-is, 1 YouTube, 2 Shorts and TikTok). geode_loudness_window fills aim, low, high (LUFS) and the
 * true-peak ceiling (dBTP) and returns 1 for a target that normalises, else 0. geode_loudness_advise returns
 * the kind (0 nothing to measure, 1 as mixed, 2 on target, 3 normalise; gated 0 = nothing above the gate)
 * and fills out[GEODE_LOUDNESS_ADVICE_DOUBLES]: gain dB, gain the level alone needed, resulting LUFS,
 * resulting true peak, direction (1 up, -1 down, 0 none), ceiling limited (1/0), band (-1 under the window,
 * 1 over, 0 in), LU under YouTube when worth saying (else 0), peak over YouTube's ceiling (1/0). */
#define GEODE_LOUDNESS_ADVICE_DOUBLES 9
GEODE_API int     geode_loudness_window(int target, double* out4);
GEODE_API int     geode_loudness_advise(int target, int gated, double lufs, double true_peak_dbtp, double* out);

/* Encoder requests (output/EncodePlan). limit_bytes <= 0 means the render target, 3.8 GB. */
GEODE_API int     geode_export_capped_video_bps(int requested_bps, int64_t duration_us, int64_t audio_bytes,
                                                int64_t limit_bytes);
GEODE_API int64_t geode_export_muxer_hard_limit_bytes(void);
GEODE_API void    geode_export_frame_size(int short_side, int w_ratio, int h_ratio, int* out_w, int* out_h);
GEODE_API int     geode_export_clamp_fps(int requested_fps);     /* 24..60 */

/* A render's time left (output/RenderEta); its range is in geode_render_plan_api.h. */
/* The time left, from progress samples. A sample is the bar's 0..1 at at_ms on a monotonic clock; steady 0
 * marks the quick lead-in (the audio transcode), which says nothing about the render's pace and starts the
 * estimate over, as progress moving backwards does. Returns whole seconds, or GEODE_RENDER_ETA_UNKNOWN until
 * the steady render has run long enough to say. Any thread: the handle locks. */
#define GEODE_RENDER_ETA_UNKNOWN (-1)
typedef struct geode_render_eta geode_render_eta;
GEODE_API geode_render_eta* geode_render_eta_create(void);
GEODE_API void    geode_render_eta_destroy(geode_render_eta*);
GEODE_API int64_t geode_render_eta_sample(geode_render_eta*, float progress, int64_t at_ms, int steady);
GEODE_API void    geode_render_eta_reset(geode_render_eta*);
/* How an estimate is worded: the phrase, and for minutes and hours the rounded number it carries (else 0). */
#define GEODE_ETA_ALMOST_DONE 0     /* under 30 s */
#define GEODE_ETA_ABOUT_A_MINUTE 1  /* under 90 s */
#define GEODE_ETA_MINUTES 2         /* under an hour */
#define GEODE_ETA_HOURS 3
GEODE_API int     geode_render_eta_phrase(int64_t seconds_remaining);
GEODE_API int     geode_render_eta_phrase_count(int64_t seconds_remaining);

/* A recording's limits (output/RecordPlan; Settings › Export › Record). A maximum length is in seconds,
 * 0 = no limit. The offered lengths are index 0..count-1; a stored one is snapped onto them. The stop itself
 * is the recorder's (geode_recorder_*). */
GEODE_API int     geode_record_max_length_count(void);
GEODE_API int     geode_record_max_length_at(int index);
GEODE_API int     geode_record_snap_max_length(int requested_sec);
/* The bytes length_sec of video at video_bps takes with its AAC; out_lowered (may be null) is 1 when the
 * render lowers the bit rate to stay under the muxer's 3.8 GB target. */
GEODE_API int64_t geode_record_estimate_bytes(int video_bps, int length_sec, int* out_lowered);

/* Record (Visuals › Record; output/Recorder): a video of the visual plus the sound, captured live. The
 * recorder is the file's clock: which stage frames are kept and their PTS, where each sample the visual
 * heard sits beside them, and when the recording ends. The stage's renderer feeds it (geode_viz_record_start);
 * the session's encoder thread (geode_record_session_*) takes the audio out and reports what the encoder and
 * muxer did. Times are CLOCK_MONOTONIC ns (System.nanoTime). max_sec is Settings › Export › Record's maximum length and cap_ms
 * the entitlement's cap, 0 for none; the shorter one ends the recording. */
typedef struct geode_recorder geode_recorder;
#define GEODE_RECORD_STOP_USER       1
#define GEODE_RECORD_STOP_MAX_LENGTH 2
#define GEODE_RECORD_STOP_RESIZED    3 /* the stage changed size (a rotation) */
#define GEODE_RECORD_STOP_FILE_FULL  4 /* near the muxer's 4 GB line */
#define GEODE_RECORD_STOP_FAILED     5
#define GEODE_RECORD_AUDIO_RATE_HZ   48000
#define GEODE_RECORD_AUDIO_CHANNELS  2
#define GEODE_RECORD_AUDIO_BPS       192000 /* what the AAC encoder is asked for */
/* The clock every now_ns here is read from (monotonic, ns): the one the render thread stamps frames and
   heard blocks with. */
GEODE_API int64_t geode_record_now_ns(void);
GEODE_API geode_recorder* geode_recorder_create(int stage_w, int stage_h, int fps, int max_sec, int64_t cap_ms);
GEODE_API void    geode_recorder_destroy(geode_recorder*);
/* GL thread, before a stage frame is drawn: its PTS in ns when it goes into the file, else -1. */
GEODE_API int64_t geode_recorder_frame(geode_recorder*, int64_t now_ns, int stage_w, int stage_h);
/* GL thread: a block the renderer's sound engine just took; restarted when its ring started over. */
GEODE_API void    geode_recorder_hear(geode_recorder*, const float* interleaved, int frames, int channels,
                                      int sample_rate, int64_t now_ns);
GEODE_API void    geode_recorder_restarted(geode_recorder*);
/* Encoder thread: one video frame came out of the encoder; the muxer wrote `bytes` more. */
GEODE_API void    geode_recorder_video_encoded(geode_recorder*);
GEODE_API void    geode_recorder_wrote(geode_recorder*, int64_t bytes, int64_t now_ns);
/* Encoder thread: up to capacity_frames of 48 kHz stereo s16 due now, stamped *out_pts_us. Returns the
 * frames written; 0 after the recording stopped means the audio is complete. */
GEODE_API int     geode_recorder_take_audio(geode_recorder*, int64_t now_ns, int16_t* out, int capacity_frames,
                                            int64_t* out_pts_us);
/* Any thread. why is a GEODE_RECORD_STOP_*; the first stop wins. stopped answers it, 0 while rolling. */
GEODE_API void    geode_recorder_stop(geode_recorder*, int why, int64_t now_ns);
GEODE_API int     geode_recorder_stopped(const geode_recorder*);
GEODE_API int64_t geode_recorder_recorded_ms(const geode_recorder*, int64_t now_ns);
GEODE_API int64_t geode_recorder_frames_dropped(const geode_recorder*);
/* The encoder's frame for a stage (its shape; the short side the smaller of short_side and the stage's own)
 * and the video bit rate for it; limit_ms, the recording's longest length or 0, lowers the rate when the
 * file would pass the muxer's target. */
GEODE_API void    geode_record_frame_size(int stage_w, int stage_h, int short_side, int* out_w, int* out_h);
GEODE_API int     geode_record_video_bps(int width, int height, int fps, int64_t limit_ms);
/* What the video encoder is asked for on its attempt-th try (0 first): the chosen short side, then
 * each smaller rung the stage fills, with the bit rate for it. 0 past the last attempt. */
GEODE_API int     geode_record_encoder_attempt(int stage_w, int stage_h, int short_side, int fps, int64_t limit_ms,
                                               int attempt, int* out_w, int* out_h, int* out_video_bps);
/* 1 when a recording this long is kept, 0 when it is a mis-tap. */
GEODE_API int     geode_record_worth_keeping(int64_t recorded_ms);
/* The longest this recording may run, the shorter of its two limits; 0 for none. */
GEODE_API int64_t geode_recorder_limit_ms(const geode_recorder*);

/* A recording's encoders and file (output/RecordSession on output/EncodeSink; Android only, the phone's
 * MediaCodec and MediaMuxer). open makes the H.264 encoder at the first size the device takes of the ones
 * geode_record_encoder_attempt offers, the AAC encoder and an MP4 muxer on `fd` (open read-write; the muxer
 * keeps its own duplicate, so the caller may close fd once open returns), and starts the session's own
 * encoder thread, which drains both into the file on the recorder's clock; NULL when no encoder or muxer would
 * start. The recorder handle may be destroyed before or after the session. size gives the encoder's frame.
 * state is a GEODE_RECORD_SESSION_*, polled: CLOSING once the recorder stopped, when the stage's copy should
 * close (geode_viz_record_stop, then tee_closed, on the GL thread: the video stream ends once nothing draws
 * into it, or after a second without an answer); SAVED or FAILED once the file is finished. tee_start is
 * geode_viz_record_start on the session's encoder surface (render thread, the stage's context current; 0 when
 * the surface would not open or the encoders are already gone); the surface never crosses JNI. destroy joins
 * the thread: call it once the state is SAVED or FAILED and after the GL thread's tee_closed. */
typedef struct geode_record_session geode_record_session;
#define GEODE_RECORD_SESSION_RUNNING 0
#define GEODE_RECORD_SESSION_CLOSING 1
#define GEODE_RECORD_SESSION_SAVED   2
#define GEODE_RECORD_SESSION_FAILED  3
GEODE_API geode_record_session* geode_record_session_open(const geode_recorder* recorder, int fd, int stage_w,
                                                          int stage_h, int short_side, int fps);
GEODE_API void    geode_record_session_destroy(geode_record_session*);
GEODE_API void    geode_record_session_size(const geode_record_session*, int* out_w, int* out_h);
GEODE_API int     geode_record_session_state(const geode_record_session*);
GEODE_API void    geode_record_session_tee_closed(geode_record_session*);
GEODE_API int     geode_record_session_tee_start(geode_record_session*, geode_viz* v, geode_export_pass* watermark);

/* PCM conversions (audio/dsp/PcmConvert). downmix returns the frames written (0: not a downmix). */
GEODE_API size_t  geode_pcm_downmix_s16(const int16_t* in, size_t frames, int src_channels, int dst_channels,
                                        int16_t* out);
GEODE_API void    geode_pcm_float_to_s16(const float* in, size_t samples, int16_t* out);

/* PCM timing (audio/dsp/PcmTiming): the AAC encoder's rate, s16 durations, the start skip and trim. */
GEODE_API int     geode_aac_rate_for(int source_rate);          /* own rate to 48k; above: 44.1k family -> 44.1k, else 48k */
GEODE_API int     geode_mix_rate(void);                         /* the one rate a mix's clips share in its AAC track */
GEODE_API int64_t geode_aac_end_us(int64_t last_pts_us, int sample_rate);   /* + one 1024-sample AAC frame */
GEODE_API int64_t geode_pcm_s16_bytes_to_us(int64_t bytes, int rate, int channels);   /* floor; 0 on bad args */
GEODE_API int64_t geode_pcm_samples_before_us(int64_t us, int rate, int channels);    /* floor(us*rate/1e6)*channels */
/* A decoded s16 buffer of `bytes` at buf_pts_us trimmed to start_us: returns the bytes to drop from its front
 * (whole frames; == bytes when none of it is kept) and writes the kept part's pts (0 = the start) to
 * out_pts_us when not NULL. A buffer that straddles the start keeps the frames from the start on. */
GEODE_API int64_t geode_pcm_trim_s16(int64_t start_us, int64_t buf_pts_us, int64_t bytes, int rate, int channels,
                                     int64_t* out_pts_us);
/* Playout clock (sound/interp/PlayoutClock, #349): which ring sample the speaker plays now, so live visuals
 * land on what is heard (AudioTrack buffer, HAL and Bluetooth latency included) instead of on what the sink
 * took. Times: media microseconds (the sink's timeline) and monotonic nanoseconds; ring samples are absolute
 * per-channel frame indices (the ring's running write count). The playback thread writes, any GL thread
 * reads; every call is safe on a NULL handle. speaker_frame is a pure read: the same facts and now_ns give
 * the same answer at any frame rate. */
typedef struct geode_playout geode_playout;
GEODE_API geode_playout* geode_playout_create(void);
GEODE_API void           geode_playout_destroy(geode_playout*);
/* The sink handled a buffer presented at presentation_us whose first sample is ring_frame and which put
 * `frames` samples in the ring. A repeat of the newest presentation time is the same buffer retried. */
GEODE_API void           geode_playout_buffer_handled(geode_playout*, int64_t presentation_us, int64_t ring_frame,
                                                      int64_t frames);
/* The same from the ring's head (geode_pcm_ring_head) read before and after the sink handled the buffer. */
GEODE_API void           geode_playout_ring_span(geode_playout*, int64_t presentation_us, int64_t head_before,
                                                 int64_t head_after);
/* The sink's playback position in media time (AudioTrack.getTimestamp, latency included), read at now_ns.
 * GEODE_PLAYOUT_NO_POSITION (Media3's AudioSink.CURRENT_POSITION_NOT_SET) is no report and changes nothing. */
#define GEODE_PLAYOUT_NO_POSITION INT64_MIN
GEODE_API void           geode_playout_position(geode_playout*, int64_t sink_us, int64_t now_ns);
GEODE_API void           geode_playout_playing(geode_playout*, int playing, int64_t now_ns);   /* 1 play, 0 pause */
GEODE_API void           geode_playout_flush(geode_playout*);           /* a seek: every anchor and position goes */
GEODE_API void           geode_playout_speed(geode_playout*, float speed);   /* media per wall time; > 0 */
/* The user's A/V offset, clamped to +-1000 ms; positive shows the picture later. */
GEODE_API void           geode_playout_set_offset_ms(geode_playout*, int ms);
/* The offset's bound in ms: geode_playout_set_offset_ms takes -max..+max (Settings' Picture delay). */
GEODE_API int            geode_playout_max_offset_ms(void);
/* The newest ring sample heard at now_ns, offset applied: never past the newest sample written. -1 when
 * nothing was handled since create or flush (and for NULL); the first handled sample until a position. */
GEODE_API int64_t        geode_playout_speaker_frame(const geode_playout*, int64_t now_ns);
/* Cast: 1 while the TV plays the ring as written, so every reader takes the newest sample again. */
GEODE_API void           geode_playout_set_bypass(geode_playout*, int on);
/* How far a live reader of the player's ring may read at now_ns (sound/interp/SpeakerSync): speaker_frame,
 * or INT64_MAX (the newest) for NULL, under bypass and while the mic or a capture is the ring's source
 * (geode_sound_set_player_source). What geode_viz_feed_ring passes its cursor. */
GEODE_API int64_t        geode_playout_listen_limit(const geode_playout*, int64_t now_ns);
/* Wall ms from now_ns until the newest sample the sink took is heard (its buffer, the HAL, the link), at
 * most 2000; 0 with no map. A gain ramp that ended on the samples is heard after this long. */
GEODE_API int            geode_playout_lag_ms(const geode_playout*, int64_t now_ns);
/* The PCM ingest ring (audio/ingest/PcmRing): one lock-free ring per audio source, written by one thread
 * at a time (the player's tap, a capture thread) and read by any number of cursors, which never change it.
 * Every write carries its sample rate and source channel count, so a reader gets each frame with the rate
 * it was written at. Lifetime: the handle and every cursor (a renderer's feed included) share the ring, so
 * it is freed when the last of them goes; destroy the handle only once no thread writes through it. A stage
 * handed it (geode_stage_set_sound) holds the handle until it hears another, so its frames never read a freed
 * one. The player's and the mic's handles are kept for the process and never freed. Every
 * call is safe on a NULL handle. */
typedef struct geode_pcm_ring   geode_pcm_ring;
typedef struct geode_pcm_cursor geode_pcm_cursor;
/* Sample encodings of geode_pcm_ring_write_bytes, numbered as Android's AudioFormat.ENCODING_PCM_*. */
#define GEODE_PCM_S16 2     /* signed 16-bit, x / 32768 */
#define GEODE_PCM_U8 3      /* unsigned 8-bit, (x - 128) / 128 */
#define GEODE_PCM_FLOAT 4   /* 32-bit float */
/* capacity_frames a power of two; the ring stores `channels` channels; max_write_frames is the runway
 * (0 = a quarter of the ring). NULL for arguments it cannot take. */
GEODE_API geode_pcm_ring*   geode_pcm_ring_create(int capacity_frames, int channels, int max_write_frames);
/* destroy leaves a handle from geode_pcm_ring_shared alone. */
GEODE_API void              geode_pcm_ring_destroy(geode_pcm_ring*);
/* The rings the app keeps, one per source (audio/ingest/RingKinds): the player's (the tap, or a capture of
 * another app in its place), the mic's and the output-mix capture of other apps. */
#define GEODE_PCM_RING_PLAYER 0
#define GEODE_PCM_RING_MIC 1
#define GEODE_PCM_RING_OUTPUT_MIX 2
/* The process's ring of a kind kept for it (the player's, the mic's), made on the first ask at the kind's
 * size, the mic's layered over the player's, and never freed: a handle any thread holds stays valid. NULL
 * for another kind or no memory. Any thread. */
GEODE_API geode_pcm_ring*   geode_pcm_ring_shared(int kind);
/* The app's sound input, for the surfaces that hold no playback session (the stage, a
 * cast): the player's ring while a session holds it, else NULL. The shell says so as a session begins and ends.
 * Process-wide, any thread. */
GEODE_API void              geode_pcm_ring_set_live(int on);
GEODE_API geode_pcm_ring*   geode_pcm_ring_live(void);
/* A new ring of a kind's size and rules (the output mix's writes are snapshots), its owner's to destroy.
 * NULL for an unknown kind or no memory. */
GEODE_API geode_pcm_ring*   geode_pcm_ring_create_kind(int kind);
GEODE_API int               geode_pcm_ring_max_write_frames(const geode_pcm_ring*);   /* the runway */
/* Interleaved frames of `channels` source channels at sample_rate (missing ring channels stored silent,
 * extra ones dropped). Returns the frames written, 0 for a refused block; peak_out (may be NULL) gets the
 * largest magnitude stored. write_bytes takes whole frames of `encoding` from unaligned bytes in the
 * machine's byte order and ignores a trailing part of a frame. Writer thread only. */
GEODE_API int               geode_pcm_ring_write_f32(geode_pcm_ring*, const float* interleaved, int frames, int channels,
                                                     int sample_rate, float* peak_out);
GEODE_API int               geode_pcm_ring_write_s16(geode_pcm_ring*, const int16_t* interleaved, int frames, int channels,
                                                     int sample_rate, float* peak_out);
GEODE_API int               geode_pcm_ring_write_bytes(geode_pcm_ring*, const void* data, size_t bytes, int encoding,
                                                       int channels, int sample_rate, float* peak_out);
/* on != 0: the ring's writes are a capture's snapshots, not a stream (the output mix: the
 * newest ~1024 samples ~20 times a second). Each write then adds the real time since the last one, on the
 * monotonic clock, filled from that snapshot alone (audio/ingest/SnapshotStitch), so the readers' sample
 * clock is real time and no two snapshots are joined at a seam. on = 0 writes blocks as they are again.
 * Writer thread only, before the writes it applies to. Returns 0 only when it could not allocate. */
GEODE_API int               geode_pcm_ring_set_snapshots(geode_pcm_ring*, int on);
/* A second source layered over `ring` (the mic over the player's tap; audio/ingest/LayerMix): every renderer
 * fed `ring` hears the two mixed while `ring` gains frames, and `layer` alone once it has not for 0.3 s.
 * `layer` keeps its own writer and rate; begin an epoch on it when its source starts and when it stops.
 * Other readers of `ring` (a cursor, Cast's sound) hear `ring` only. NULL removes the layer. Any thread. */
GEODE_API void              geode_pcm_ring_set_layer(geode_pcm_ring* ring, const geode_pcm_ring* layer);
/* A new stream (track change, seek, source switch): every cursor restarts at its next take. Any thread. */
GEODE_API void              geode_pcm_ring_begin_epoch(geode_pcm_ring*);
/* The player's Media3 tap (the shell's PcmTap), on the ring the tap writes. The tap thread is the writer
 * while the gate is open; the gate is open by default. `owns` 0 closes it: a capture of another app writes
 * the ring, and a paused player still pushes buffers until its AudioTrack is full. Any thread. */
GEODE_API void              geode_pcm_ring_tap_set_owner(geode_pcm_ring*, int owns);
/* The chain flushed with this format (Android's AudioFormat.ENCODING_PCM_* for `encoding`): with the gate
 * open every flush starts a ring epoch, before a sample of the new format is written. Tap thread. */
GEODE_API void              geode_pcm_ring_tap_flush(geode_pcm_ring*, int sample_rate, int channels, int encoding);
/* One buffer of the chain in the format of the last flush, written where it lies. Returns the frames
 * written; 0 with the gate closed, before a flush gave a format, or for a refused block. Tap thread. */
GEODE_API int               geode_pcm_ring_tap_write(geode_pcm_ring*, const void* data, size_t bytes);
GEODE_API int64_t           geode_pcm_ring_written(const geode_pcm_ring*);   /* frames since the last epoch began */
GEODE_API int64_t           geode_pcm_ring_head(const geode_pcm_ring*);      /* frames ever written, across epochs */
/* The playout clock of the sink that plays this ring (the player's), or NULL for none: a renderer fed this
 * handle then reads up to the sample the speaker plays (geode_playout_listen_limit) instead of the newest.
 * The clock must outlive every reader of the handle; the app keeps one for the process. Any thread. */
GEODE_API void              geode_pcm_ring_set_playout(geode_pcm_ring*, const geode_playout* clock);
/* One reader's place in a ring: every frame once, in order. max_frames is the most one take hands out
 * (0 = the ring's runway). One thread per cursor. */
GEODE_API geode_pcm_cursor* geode_pcm_cursor_create(const geode_pcm_ring*, int max_frames);
GEODE_API void              geode_pcm_cursor_destroy(geode_pcm_cursor*);
GEODE_API int               geode_pcm_cursor_max_frames(const geode_pcm_cursor*);
GEODE_API int               geode_pcm_cursor_channels(const geode_pcm_cursor*);   /* the ring's channel count */
/* What the ring gained since the last take, interleaved at the ring's channel count (a mono source on every
 * channel): returns the frames, all written at *sample_rate; *restarted is 1 on a first take or a new epoch
 * (reset the consumer first). After a stall it keeps the newest audio. A take ends where the rate changes;
 * the rest comes with the next. A take the writer lapped during the copy returns 0 and the next restarts.
 * -1 without taking when out holds fewer than max_frames frames. take_s16 clamps to -1..1, scales by
 * 32767 and rounds. */
GEODE_API int               geode_pcm_cursor_take(geode_pcm_cursor*, float* out, int capacity_frames, int* restarted,
                                                  int* sample_rate);
GEODE_API int               geode_pcm_cursor_take_s16(geode_pcm_cursor*, int16_t* out, int capacity_frames,
                                                      int* restarted, int* sample_rate);
GEODE_API void              geode_pcm_cursor_reset(geode_pcm_cursor*);   /* the next take starts from the newest audio */
/* The live renderer's feed: hands what `ring` gained since this renderer's last call to its sound engine,
 * resetting the engine on a restart, with no copy through the shell. A ring with a playout clock is read up
 * to the sample the speaker plays now, so the picture never moves ahead of the sound. The renderer keeps its
 * own cursor (and so the ring) until it is fed another ring or NULL, which drops it. Render thread, before
 * geode_viz_render. */
GEODE_API void              geode_viz_feed_ring(geode_viz*, const geode_pcm_ring* ring);
/* AIFF files (audio/ingest/AiffReader), which MediaExtractor does not read: an AIFF or AIFF-C (NONE, sowt)
 * of 8, 16, 24 or 32 bits read front to back as 16-bit PCM (the top two bytes of a wider sample). open_fd
 * takes the descriptor and closes it, also when it returns NULL (not an AIFF it reads). With length >= 0 it
 * reads `length` bytes from start_offset on from the descriptor's position (an AssetFileDescriptor's span),
 * with length < 0 to the end. One thread at a time per handle. */
typedef struct geode_aiff geode_aiff;
GEODE_API geode_aiff*       geode_aiff_open_fd(int fd, int64_t start_offset, int64_t length);
GEODE_API void              geode_aiff_close(geode_aiff*);
GEODE_API int               geode_aiff_channels(const geode_aiff*);
GEODE_API int               geode_aiff_sample_rate(const geode_aiff*);
GEODE_API int64_t           geode_aiff_total_frames(const geode_aiff*);   /* 0 = read to the end */
GEODE_API float             geode_aiff_progress(const geode_aiff*);       /* frames read over the total, 0..1 */
/* Up to `capacity` interleaved 16-bit samples, whole frames while the file has them; -1 at the end. */
GEODE_API int               geode_aiff_read_s16(geode_aiff*, int16_t* out, int capacity);
/* The same header walk (audio/ingest/AiffHeaderScan) for a caller that holds the file itself, the player's
 * extractor: pass over geode_aiff_scan_skip bytes, read geode_aiff_scan_read bytes (never more than
 * GEODE_AIFF_SCAN_MAX_READ), hand them to geode_aiff_scan_step, and repeat while it answers MORE. After DONE
 * the file stands at the first sample. A step whose `n` is not the count asked for, or a NULL scan, is
 * INVALID; nothing in the file is trusted with more than a skip. One thread at a time per handle. */
#define GEODE_AIFF_SCAN_MORE      0
#define GEODE_AIFF_SCAN_DONE      1
#define GEODE_AIFF_SCAN_INVALID (-1)
#define GEODE_AIFF_SCAN_MAX_READ 22
typedef struct geode_aiff_scan geode_aiff_scan;
GEODE_API geode_aiff_scan*  geode_aiff_scan_create(void);
GEODE_API void              geode_aiff_scan_destroy(geode_aiff_scan*);
GEODE_API int64_t           geode_aiff_scan_skip(const geode_aiff_scan*);
GEODE_API int               geode_aiff_scan_read(const geode_aiff_scan*);
GEODE_API int               geode_aiff_scan_step(geode_aiff_scan*, const uint8_t* bytes, int n);
/* What the header said, GEODE_AIFF_HEADER_COUNT values in the order below; 1, or 0 writing nothing before
 * DONE or with a smaller `capacity`. PCM is how the player takes the samples: GEODE_AIFF_PCM_NONE for a
 * width or byte order it has no encoding for. DATA_BYTES is what SSND's size leaves for samples. */
#define GEODE_AIFF_HEADER_CHANNELS        0
#define GEODE_AIFF_HEADER_SAMPLE_RATE     1
#define GEODE_AIFF_HEADER_PCM             2
#define GEODE_AIFF_HEADER_BYTES_PER_FRAME 3
#define GEODE_AIFF_HEADER_FRAMES          4
#define GEODE_AIFF_HEADER_DATA_START      5
#define GEODE_AIFF_HEADER_DATA_BYTES      6
#define GEODE_AIFF_HEADER_COUNT           7
#define GEODE_AIFF_PCM_NONE   0
#define GEODE_AIFF_PCM_S16_LE 1
#define GEODE_AIFF_PCM_S16_BE 2
#define GEODE_AIFF_PCM_S24_BE 3
#define GEODE_AIFF_PCM_S32_BE 4
GEODE_API int               geode_aiff_scan_header(const geode_aiff_scan*, int64_t* out, int capacity);
/* The player's seek map over the samples, in whole frames: the start time of the frame `bytes` in lies in,
 * and the byte offset of the frame playing at time_us, held inside data_bytes. 0 for a bad layout. */
GEODE_API int64_t           geode_aiff_time_us(int64_t bytes, int bytes_per_frame, int sample_rate);
GEODE_API int64_t           geode_aiff_seek_offset(int64_t time_us, int bytes_per_frame, int sample_rate,
                                                   int64_t data_bytes);
/* The extractor's reads over the samples (data_start, data_bytes long): the bytes to pass over at `position`
 * to reach them, the most to read there (at most GEODE_AIFF_MAX_SAMPLE_BYTES; 0 once they are done), the whole
 * frames among `pending` bytes held, and the start time of those bytes, which end at `position`. */
#define GEODE_AIFF_MAX_SAMPLE_BYTES 32768
GEODE_API int64_t           geode_aiff_gap_bytes(int64_t position, int64_t data_start);
GEODE_API int               geode_aiff_read_bytes(int64_t position, int64_t data_start, int64_t data_bytes);
GEODE_API int               geode_aiff_whole_bytes(int pending, int bytes_per_frame);
GEODE_API int64_t           geode_aiff_pending_time_us(int64_t position, int64_t data_start, int pending,
                                                       int bytes_per_frame, int sample_rate);
/* An export's track audio (audio/ingest/TrackFeed): the shell pushes each decoded chunk, and the renderer's
 * sound engine hears exactly the frames up to each video frame's end, counted from the range start.
 * Frames before start_ms are dropped. One chunk is held at a time: push only after a deliver returned 0.
 * One thread per handle. */
typedef struct geode_track_feed geode_track_feed;
GEODE_API geode_track_feed* geode_track_feed_create(int64_t start_ms);
GEODE_API void              geode_track_feed_destroy(geode_track_feed*);
/* A decoder's output: `bytes` of interleaved float (machine order, is_float 1) or 16-bit little-endian PCM,
 * its first frame at pts_us. */
GEODE_API void              geode_track_feed_push_decoded(geode_track_feed*, const void* data, size_t bytes, int is_float,
                                                          int channels, int sample_rate, int64_t pts_us);
/* One read of an AIFF into the feed: 0 once the file has run out, else 1. */
GEODE_API int               geode_track_feed_push_aiff(geode_track_feed*, geode_aiff*);
GEODE_API int               geode_track_feed_has_rate(const geode_track_feed*);   /* 1 once a chunk told the rate */
/* Aims the next delivers at end_ms into the range, at the latest chunk's rate. */
GEODE_API void              geode_track_feed_target_ms(geode_track_feed*, int64_t end_ms);
/* Aims the next delivers at the end of video frame `frame` (from the range start) at fps: llround((frame + 1) * rate
 * / fps) frames of the latest chunk's rate, sample-exact (core/output/FrameClock samplesThrough). */
GEODE_API void              geode_track_feed_target_frame(geode_track_feed*, int64_t frame, int32_t fps);
/* Hands sink the held frames up to the target: 1 once it is reached, 0 when the held chunk ran out first. */
typedef void (*geode_track_sink)(void* context, const float* interleaved, int frames, int channels, int sample_rate);
GEODE_API int               geode_track_feed_deliver(geode_track_feed*, geode_track_sink sink, void* context);
/* geode_track_feed_deliver into this renderer's sound engine (geode_viz_write_audio); a NULL renderer hears
 * nothing but the feed still moves on. Render thread. */
GEODE_API int               geode_viz_hear_track(geode_viz*, geode_track_feed*);
/* After the pre-roll was heard (geode_export_pre_roll), before frame 0: 0 when the engine heard nothing (the
 * track gave no audio), and the render must fail rather than export a still picture. Render thread. */
GEODE_API int               geode_viz_end_pre_roll(geode_viz*, int64_t range_start_ms);
/* The playback output (audio/output, Android only): an Oboe stream fed through a lock-free FIFO, standing
 * where the AudioTrack stands under Media3's DefaultAudioSink. For plain PCM only: 16-bit or float (numbered
 * as GEODE_PCM_S16 and GEODE_PCM_FLOAT), mono or stereo, 8 to 192 kHz. One control thread (Media3's playback
 * thread) makes every call; the audio callback only reads the FIFO. Every call is safe on a NULL handle. */
typedef struct geode_audio_out geode_audio_out;
/* geode_audio_out_events' bits, each reported once. */
#define GEODE_AUDIO_OUT_ADVANCING 1   /* the position moved for the first time since play */
#define GEODE_AUDIO_OUT_UNDERRUN 2    /* the stream ran dry mid-stream */
#define GEODE_AUDIO_OUT_FAILED 4      /* the stream is gone for good; writes take nothing (-1): the shell reports
                                         the output stalled, so the sink builds a new one */
/* The back-off after a stream failed for good (GEODE_AUDIO_OUT_FAILED): the shell tells when it happened, and
 * for 60 s after it geode_audio_out_takes answers 0. now_ms is one monotonic clock for both calls. Process-wide. */
GEODE_API void             geode_audio_out_note_failed(int64_t now_ms);
/* Whether the native output is wanted at all: the user chose it, and bit-perfect USB (which needs the
 * platform track) is not both on and supported. 1 or 0. */
GEODE_API int              geode_audio_out_wanted(int chosen, int bit_perfect, int bit_perfect_supported);
/* Whether the output plays one of Media3's output configs (audio/output/OutputChoice), its fields passed as
 * they are: channel_mask an AudioFormat.CHANNEL_OUT_* mask, usage C.USAGE_*, virtual_device_id C.INDEX_UNSET
 * for none. 0 for anything but plain PCM it supports, and within the back-off; the platform track plays it. */
GEODE_API int              geode_audio_out_takes(int64_t now_ms, int offload, int tunneling, int offload_gapless,
                                                 int playback_parameters, int virtual_device_id, int usage,
                                                 int channel_mask, int encoding, int sample_rate);
/* Opens the stream for writes of `encoding` at channel_mask's channels. buffer_bytes is the sink's buffer for
 * this output (the FIFO and the stream's own buffer share it; 0 = 250 ms); session_id the player's audio
 * session (0 or below = allocate one). NULL when the format is not supported or the device gave no stream
 * that is exactly it. */
GEODE_API geode_audio_out* geode_audio_out_open(int sample_rate, int channel_mask, int encoding, int buffer_bytes,
                                                int session_id);
/* Silences the stream at once and closes it on a thread of its own; the handle is gone when this returns. */
GEODE_API void             geode_audio_out_release(geode_audio_out*);
GEODE_API void             geode_audio_out_play(geode_audio_out*);
GEODE_API void             geode_audio_out_pause(geode_audio_out*);   /* the position stands still */
GEODE_API void             geode_audio_out_flush(geode_audio_out*);   /* drops what waits; the position restarts */
GEODE_API void             geode_audio_out_stop(geode_audio_out*);    /* the last frame was written: play it out */
/* Never blocks: takes the whole frames that fit and returns the bytes taken; -1 once the stream has failed. */
GEODE_API int64_t          geode_audio_out_write(geode_audio_out*, const void* data, size_t bytes);
/* How much of what was written the listener has heard, in microseconds of audio: from the stream's
 * timestamp, never backwards, never past what was written, exactly the amount written once played out. */
GEODE_API int64_t          geode_audio_out_position_us(geode_audio_out*);
GEODE_API void             geode_audio_out_set_volume(geode_audio_out*, float volume);   /* 0..1, ramped */
/* The device to play on (AudioDeviceInfo.getId; 0 = the default). A change reopens the stream. */
GEODE_API void             geode_audio_out_set_device(geode_audio_out*, int device_id);
GEODE_API int              geode_audio_out_events(geode_audio_out*);    /* GEODE_AUDIO_OUT_* since the last call */
GEODE_API int              geode_audio_out_stalled(geode_audio_out*);   /* 1: playing, frames wait, nothing pulls */
GEODE_API int              geode_audio_out_session_id(const geode_audio_out*);   /* the stream's audio session */
GEODE_API int64_t          geode_audio_out_buffer_frames(const geode_audio_out*);   /* FIFO + stream buffer */
/* Cast's live HLS timing (viz/frame/HlsSegments): a segment lasts the media time its fragment holds (the
 * video track's trun sample durations over its mdhd timescale), never the wall time between cuts. */
#define GEODE_HLS_WINDOW 6           /* segments kept and listed */
#define GEODE_HLS_START_SEGMENTS 3   /* listed before the playlist is served */
GEODE_API int             geode_hls_window(void);   /* GEODE_HLS_WINDOW, for Kotlin */
GEODE_API double          geode_hls_fragment_seconds(const uint8_t* init, size_t init_size,
                                                     const uint8_t* fragment, size_t fragment_size);   /* 0 = unknown */
/* The playlist for count segments numbered from first_sequence (the newest GEODE_HLS_WINDOW listed; a
 * duration of 0 = the nominal segment) into out, NUL-terminated. Returns its length; 0 while fewer than
 * GEODE_HLS_START_SEGMENTS are given. When length + 1 > cap nothing is written (out[0] = 0 if cap > 0):
 * call again with a buffer of length + 1. out may be null with cap 0, to ask the size. */
GEODE_API int             geode_hls_playlist(int64_t first_sequence, const double* durations, int count,
                                             const char* init_uri, const char* segment_prefix,
                                             const char* segment_suffix, char* out, int cap);
/* A fragment (moof + mdat) rewritten into a self-contained segment: default-base-is-moof, a tfdt per traf
 * from decode_times (GEODE_HLS_TRACKS int64s the caller keeps, zeroed at the stream's start, moved on by
 * each rewrite), data offsets and sizes fixed. moof_at is the moof's position in the muxer's output. out
 * needs fragment_size + GEODE_HLS_FRAGMENT_GROWTH bytes. Returns the length written; 0 = refused. */
#define GEODE_HLS_TRACKS 8
#define GEODE_HLS_FRAGMENT_GROWTH 160
GEODE_API size_t          geode_hls_rewrite_fragment(const uint8_t* init, size_t init_size, int64_t moof_at,
                                                     const uint8_t* fragment, size_t fragment_size,
                                                     int64_t* decode_times, uint8_t* out, size_t cap);

/* Cast's audio pump (viz/frame/CastAudio): how much of what the ring gained to keep, and how many frames to
 * queue and pad up to the frame clock's audio cut. Pure counts: the caller owns GEODE_CAST_AUDIO_STATE int64s
 * (frames queued so far, frames held, rate) and the PCM. Start returns the hold's capacity in frames (half a
 * second). A step takes the frames just gained (already resampled to the rate) and the cut in µs, and fills
 * GEODE_CAST_AUDIO_STEP int64s, applied in order: out[0] evict that many of the oldest held, out[1] skip that
 * many at the head of the gained, out[2] append the next that many to the hold, out[3] queue that many from the
 * hold's head, out[4] then that many of silence, out[5] the block's PTS in µs. out[3] + out[4] = 0: nothing to
 * queue this frame (the cut has not moved past what was queued; what was gained waits in the hold). */
#define GEODE_CAST_AUDIO_STATE 3
#define GEODE_CAST_AUDIO_STEP 6
GEODE_API int             geode_cast_audio_start(int64_t* state, int rate_hz);   /* the hold's capacity, frames */
GEODE_API void            geode_cast_audio_step(int64_t* state, int64_t gained_frames, int64_t until_us, int64_t* out);
/* The AAC encoder's backlog of queued blocks (viz/frame/CastAudio, CastAudioFeed): GEODE_CAST_AUDIO_FEED_STATE
 * int64s the caller owns (frames taken or dropped, frames pending, rate). Start returns the cap in frames.
 * Add takes a block's frames and returns how many of the oldest pending to drop first; take hands the next
 * input buffer up to buffer_frames from the head, its PTS in *pts_us, and returns how many (0 = none wait). */
#define GEODE_CAST_AUDIO_FEED_STATE 3
GEODE_API int             geode_cast_audio_feed_start(int64_t* state, int rate_hz);
GEODE_API int64_t         geode_cast_audio_feed_add(int64_t* state, int64_t frames);
GEODE_API int64_t         geode_cast_audio_feed_take(int64_t* state, int64_t buffer_frames, int64_t* pts_us);
/* Cast's sound (output/CastAudioPump): the player's ring `music` (NULL: silence throughout) read through its
 * own cursor, resampled to out_rate_hz (audio/dsp/PcmResampler), held and padded to each frame's audio cut by
 * the rule above. Create returns NULL for a rate or channel count <= 0. A pump is one video frame: `current`
 * is the ring the visuals hear now, only compared with `music` and never read (another ring or NULL takes
 * nothing and the cut is padded with silence); until_us is geode_frame_clock_audio_until_us. It returns the
 * frames of the block to queue (0: none this frame). Read copies that block into out (capacity int16s) and
 * returns its frames, 0 when out is too small. One thread; nothing allocates per frame. */
typedef struct geode_cast_audio geode_cast_audio;
GEODE_API geode_cast_audio* geode_cast_audio_create(const geode_pcm_ring* music, int out_rate_hz, int channels);
GEODE_API void            geode_cast_audio_destroy(geode_cast_audio*);
GEODE_API int             geode_cast_audio_pump(geode_cast_audio*, const geode_pcm_ring* current, int64_t until_us);
GEODE_API int             geode_cast_audio_read(const geode_cast_audio*, int16_t* out, int capacity);

/* Show on a computer (viz/frame/LinkShare, LinkPage): the browser link's routes, headers, page, master playlist
 * and state; core/cast/LiveServer owns only the socket and the bytes. Every path sits under a token of
 * GEODE_LINK_TOKEN_CHARS lowercase hex characters (16 random bytes from the kernel), compared in constant time. */
#define GEODE_LINK_TOKEN_CHARS 32
#define GEODE_LINK_NONE     0   /* 404 */
#define GEODE_LINK_PAGE     1   /* /<token>/ and /<token>/index.html */
#define GEODE_LINK_SCRIPT   2   /* player.js */
#define GEODE_LINK_STYLE    3   /* player.css */
#define GEODE_LINK_MASTER   4   /* index.m3u8: CODECS for a browser */
#define GEODE_LINK_PLAYLIST 5   /* live.m3u8 */
#define GEODE_LINK_INIT     6   /* init.mp4 */
#define GEODE_LINK_SEGMENT  7   /* seg<n>.m4s, n in *sequence */
#define GEODE_LINK_REDIRECT 8   /* /<token>: 308 to /<token>/ */
#define GEODE_LINK_STREAM   9   /* stream.mp4: the browser's live feed, init then each fragment as it is cut */
#define GEODE_LINK_MAX_STREAMS 3   /* browsers fed at once; past it 503 (the page retries) */
#define GEODE_LINK_RTC     10   /* rtc: POST the browser's WebRTC offer (application/sdp), the answer comes back */
#define GEODE_LINK_PAIR_PAGE   11   /* "/": the typed path's page, off the token (geode_link_pair_*) */
#define GEODE_LINK_PAIR_SCRIPT 12   /* "/pair.js" */
#define GEODE_LINK_PAIR        13   /* "/pair": POST the typed code, the token's path comes back */
/* A request's method and request-target against the token: returns the status (200, 204, 308, 404, 405) and
 * fills *resource (GEODE_LINK_*) and *sequence (a segment's, else -1). */
GEODE_API int             geode_link_route(const char* method, const char* target, const char* token, int* resource,
                                           int64_t* sequence);
/* The response head for status and resource with a body of length bytes, NUL-terminated into out; the size-query
 * convention of geode_hls_playlist (returns the length; nothing written when length + 1 > cap). */
GEODE_API int             geode_link_response_head(int status, int resource, int64_t length, const char* token,
                                                   char* out, int cap);
/* A static file's bytes (GEODE_LINK_PAGE, _SCRIPT, _STYLE, _PAIR_PAGE or _PAIR_SCRIPT), UTF-8, *size set; null
 * for any other resource. */
GEODE_API const char*     geode_link_asset(int resource, size_t* size);
/* The master playlist for an init segment (CODECS read from its avcC and esds), same convention; 0 = not yet. */
GEODE_API int             geode_link_master_playlist(const uint8_t* init, size_t init_size, char* out, int cap);
/* The share's state: the caller owns GEODE_LINK_STATE ints (phase, reason), zeroed = idle. A step takes one
 * GEODE_LINK_EVENT_* and its argument and returns GEODE_LINK_DO_* bits for the shell to carry out. */
#define GEODE_LINK_STATE 2
#define GEODE_LINK_PHASE_IDLE     0
#define GEODE_LINK_PHASE_STARTING 1
#define GEODE_LINK_PHASE_LIVE     2
#define GEODE_LINK_PHASE_ENDED    3
#define GEODE_LINK_REASON_STREAM     1
#define GEODE_LINK_REASON_NO_LAN     2
#define GEODE_LINK_REASON_BACKGROUND 3
#define GEODE_LINK_REASON_NETWORK    4
#define GEODE_LINK_REASON_TRIAL      5
#define GEODE_LINK_EVENT_START  1   /* arg: GEODE_LINK_ENTITLED | GEODE_LINK_HAS_LAN */
#define GEODE_LINK_EVENT_READY  2
#define GEODE_LINK_EVENT_FAILED 3   /* arg: GEODE_LINK_REASON_* */
#define GEODE_LINK_EVENT_STOP   4
#define GEODE_LINK_EVENT_SHOWN  5
#define GEODE_LINK_ENTITLED 1
#define GEODE_LINK_HAS_LAN  2
#define GEODE_LINK_DO_START_STREAM 1
#define GEODE_LINK_DO_STOP_STREAM  2
#define GEODE_LINK_DO_PAYWALL      4
GEODE_API int             geode_link_step(int* state, int event, int arg);
/* The body a request announces in its count header lines (each NUL-terminated, without its line break): the
 * Content-Length, GEODE_LINK_NO_BODY for none, GEODE_LINK_BAD_BODY for a Transfer-Encoding, two lengths, a
 * malformed one or one over max. */
#define GEODE_LINK_NO_BODY  (-1)
#define GEODE_LINK_BAD_BODY (-2)
GEODE_API int64_t         geode_link_body_length(const char* const* headers, int count, int64_t max);
/* The HTTP status for a POST on GEODE_LINK_RTC from the WebRTC link's answer (a length or a
 * GEODE_WEBRTC_REFUSED_*): 200, 503 (busy: no picture
 * yet, or GEODE_WEBRTC_MAX_PEERS browsers), else 400. */
GEODE_API int             geode_link_rtc_status(int answer_result);
/* The link the user sees, copies and shares: the page's address with its IPv4 host as one decimal number (the
 * WHATWG URL standard reads it as the whole address; no DNS, no internet), size-query convention; 0 when
 * address is 0 or port out of range. It hides the dotted address from the eye, not from the network. */
GEODE_API int             geode_link_share_url(uint32_t address, int port, const char* token, char* out, int cap);
/* A short friendly name for the token ("Blue Comet"), the sheet's label beside the QR code; same convention. */
GEODE_API int             geode_link_label(const char* token, char* out, int cap);
/* The typed path (viz/frame/LinkPair, for a computer with no scanner): the short link with no token, the host
 * as one number (size-query convention, 0 on the terms of geode_link_share_url), and the code that unlocks it.
 * One pairing per process, any thread. open (1) starts it with a fresh code from the OS's secure random, a no-op
 * while open; open (0) closes it. code: the code to show now ("ABC-DEF"; size-query convention, 0 while
 * closed), a new one once the last has lived GEODE_LINK_PAIR_CODE_LIFE_MS. try: one POST on GEODE_LINK_PAIR,
 * its body as typed (at most GEODE_LINK_PAIR_BODY_MAX bytes); returns the status (200 right: a single-session
 * code, spent at once; 403 wrong; 404 closed; 429 too soon after a wrong try or locked out) and on 200 writes
 * the token's path "/<token>/" into out, NUL-terminated (cap at least GEODE_LINK_TOKEN_CHARS + 3; a try spends
 * the code, so it is no size query). Clocks
 * are one monotonic clock in ms. */
#define GEODE_LINK_PAIR_BODY_MAX     64
#define GEODE_LINK_PAIR_CODE_LIFE_MS 300000
GEODE_API int             geode_link_pair_url(uint32_t address, int port, char* out, int cap);
GEODE_API void            geode_link_pair_open(int open, int64_t now_ms);
GEODE_API int             geode_link_pair_code(int64_t now_ms, char* out, int cap);
GEODE_API int             geode_link_pair_try(const char* typed, int64_t now_ms, const char* token, char* out, int cap);
/* Who may connect, by source address, before a byte is read (LinkShare's admit): IPv4 in host order, 0 =
 * unknown; prefix the bound address's prefix length; wants GEODE_LINK_WANT_* bits. A TV cast admits the Cast
 * device's address alone (the bound subnet when the SDK names none), a live link any site-local peer on the
 * bound subnet. Returns GEODE_LINK_PEER_*. */
#define GEODE_LINK_WANT_TV   1
#define GEODE_LINK_WANT_LINK 2
#define GEODE_LINK_PEER_REFUSED  0
#define GEODE_LINK_PEER_RECEIVER 1   /* the TV: its fetches count for geode_link_receiver_health */
#define GEODE_LINK_PEER_BROWSER  2
GEODE_API int             geode_link_admit(uint32_t peer, uint32_t bound, int prefix, uint32_t tv, int wants);
/* Whether the TV plays the stream after the LOAD: the receiver's MediaStatus player state and idle reason, and
 * when the TV last fetched (one monotonic clock, ms, 0 = never). Returns GEODE_LINK_RECEIVER_*. */
#define GEODE_LINK_RECEIVER_OK          0
#define GEODE_LINK_RECEIVER_ERROR       1   /* idle, reason ERROR: it could not play the stream */
#define GEODE_LINK_RECEIVER_GONE        2   /* idle for another reason: our stream is off the TV */
#define GEODE_LINK_RECEIVER_UNREACHABLE 3   /* no fetch 20 s after the LOAD, or none for 30 s while it plays */
GEODE_API int             geode_link_receiver_health(int64_t now_ms, int64_t load_ms, int64_t last_fetch_ms,
                                                     int player_state, int idle_reason);

/* Show on a computer over WebRTC (core/cast/WebRtcLink, rules in viz/frame/WebRtcRule; libdatachannel): the
 * browser's low-delay path, its media encrypted with DTLS-SRTP. One handle per cast stream, bound to the
 * phone's Wi-Fi or Ethernet address (dotted, and in host order with its prefix length, as geode_link_admit
 * reads it); no STUN or TURN server, host candidates on that address only. Any thread; every call is cheap.
 * A browser's offer is answered inside native (the cast's server, geode_cast_server_open, asks the link): the
 * answer, or a negative GEODE_WEBRTC_REFUSED_* (the page then falls back to the fMP4 feed), after up to
 * GEODE_WEBRTC_GATHER_MS for the phone's candidate.
 *   video: one encoded H.264 frame as MediaCodec wrote it (Annex B), its PTS, whether it is a keyframe; config
 *          the codec config buffer (SPS and PPS), kept for keyframes that come without it.
 *   audio: one Opus packet and its PTS (48 kHz stereo).
 *   peers: browsers connected now (the shell runs the Opus encoder only while there are any). */
#define GEODE_WEBRTC_MAX_PEERS 3
#define GEODE_WEBRTC_GATHER_MS 2000
#define GEODE_WEBRTC_ANSWER_CAP (16 * 1024)
#define GEODE_WEBRTC_OFFER_MAX (16 * 1024)
#define GEODE_WEBRTC_JITTER_TARGET_MS 500
#define GEODE_WEBRTC_REFUSED_MALFORMED (-1)
#define GEODE_WEBRTC_REFUSED_NO_VIDEO  (-2)
#define GEODE_WEBRTC_REFUSED_NO_H264   (-3)
#define GEODE_WEBRTC_REFUSED_FULL      (-4)   /* GEODE_WEBRTC_MAX_PEERS browsers already */
#define GEODE_WEBRTC_REFUSED_NOT_READY (-5)   /* no SPS from the encoder yet: the page retries */
#define GEODE_WEBRTC_REFUSED_FAILED    (-6)   /* the stack refused it or no candidate came */
GEODE_API int64_t         geode_webrtc_open(const char* bound_dotted, uint32_t bound, int prefix);
GEODE_API void            geode_webrtc_close(int64_t handle);
GEODE_API void            geode_webrtc_video(int64_t handle, const uint8_t* data, size_t size, int64_t pts_us, int key);
GEODE_API void            geode_webrtc_video_config(int64_t handle, const uint8_t* data, size_t size);
GEODE_API void            geode_webrtc_audio(int64_t handle, const uint8_t* data, size_t size, int64_t pts_us);
GEODE_API int             geode_webrtc_peers(int64_t handle);

/* The cast's HTTP server (core/cast/LiveServer, LiveHttp, LiveSegments): the live stream served on the phone's
 * own Wi-Fi or Ethernet address (bound_dotted, and bound in host order with its prefix length, as
 * geode_link_admit reads them) on a port the OS picks, under a random path token. It cuts the muxer's
 * fragmented MP4 into the init segment and the fragments, keeps the window, writes the playlist, and reads and
 * answers every request under hard bounds. One handle per cast stream, 0 = none; any thread.
 *   open:  webrtc is the stream's geode_webrtc_open handle (0 = none), asked for the answer to a browser's
 *          offer; it must outlive this handle (close this one first).
 *   start: binds and serves; the port, or 0 when the address could not be bound.
 *   stop:  ends every connection and joins the server's threads; the texts and state stay readable.
 *   close: stops and frees.
 *   admit: who the stream is for (GEODE_LINK_WANT_* bits) and the Cast device's address (0 = none named). It
 *          opens the typed path's pairing (geode_link_pair_open, on CLOCK_BOOTTIME ms) while a browser link is
 *          wanted and closes it otherwise; stop closes it too. POST /pair is answered by geode_link_pair_try.
 *   write: the next bytes of the muxer's output, in whatever pieces they come.
 *   text:  GEODE_CAST_TEXT_* NUL-terminated into out (GEODE_CAST_TEXT_CAP is always enough); its length, 0 and
 *          "" when there is none: the playlist's URL as the TV loads it, the link a user is shown
 *          (geode_link_share_url, else the dotted page), the link's short name, after stop a note of what
 *          was refused (counts and the first refused address, never a path or the token), and the short link
 *          a computer types (geode_link_pair_url, else the dotted address).
 *   state: GEODE_CAST_STATE int64s into out: when the TV last got a file (CLOCK_BOOTTIME ms, Android's
 *          elapsedRealtime; 0 = never), ms since the last segment was cut, 1 once the playlist lists enough to
 *          start on, fragments kept as the muxer wrote them, 1 once the muxer's bytes could not be cut. */
#define GEODE_CAST_TEXT_PLAYLIST_URL 0
#define GEODE_CAST_TEXT_SHARE_URL    1
#define GEODE_CAST_TEXT_LABEL        2
#define GEODE_CAST_TEXT_REFUSALS     3
#define GEODE_CAST_TEXT_PAIR_URL     4
#define GEODE_CAST_TEXT_CAP 256
#define GEODE_CAST_STATE 5
#define GEODE_CAST_STATE_LAST_FETCH_MS  0
#define GEODE_CAST_STATE_SEGMENT_AGE_MS 1
#define GEODE_CAST_STATE_READY          2
#define GEODE_CAST_STATE_AS_MUXED       3
#define GEODE_CAST_STATE_BROKEN         4
GEODE_API int64_t         geode_cast_server_open(const char* bound_dotted, uint32_t bound, int prefix, int64_t webrtc);
GEODE_API int             geode_cast_server_start(int64_t handle);
GEODE_API void            geode_cast_server_stop(int64_t handle);
GEODE_API void            geode_cast_server_close(int64_t handle);
GEODE_API void            geode_cast_server_admit(int64_t handle, uint32_t tv, int wants);
GEODE_API void            geode_cast_server_write(int64_t handle, const uint8_t* data, size_t size);
GEODE_API int             geode_cast_server_text(int64_t handle, int which, char* out, int cap);
GEODE_API void            geode_cast_server_state(int64_t handle, int64_t* out);
/* The cast's encoders (core/cast/CastEncoder, Fmp4Writer; MediaCodec): H.264 from the surface the cast's render
 * thread draws into and AAC from the player's ring (output/CastAudioPump, as geode_cast_audio_* reads it), written
 * as fragmented MP4 into the server's stream and, as they leave the codec, to WebRTC, with Opus while a browser
 * listens; on a thread of their own. Android build only.
 *   open:   server and webrtc are the stream's handles (0 = none), which must outlive this one; music the
 *           player's ring (NULL: silence throughout). NULL when an encoder would not start.
 *   window: the video encoder's input surface (an ANativeWindow*), the encoder's own.
 *   pump:   one video frame's sound, on the render thread: current the ring the visuals hear now, until_us
 *           geode_frame_clock_audio_until_us.
 *   failed: 1 once the video or AAC encoder failed: the cast should end.
 *   close:  stops the thread and releases every codec; after nothing draws into the surface any more. */
typedef struct geode_cast_encoder geode_cast_encoder;
GEODE_API geode_cast_encoder* geode_cast_encoder_open(int64_t server, int64_t webrtc, const geode_pcm_ring* music);
GEODE_API void*           geode_cast_encoder_window(const geode_cast_encoder*);
GEODE_API void            geode_cast_encoder_pump(geode_cast_encoder*, const geode_pcm_ring* current, int64_t until_us);
GEODE_API int             geode_cast_encoder_failed(const geode_cast_encoder*);
GEODE_API void            geode_cast_encoder_close(geode_cast_encoder*);
/* When a cast is given up on (core/cast/CastWatch): one verdict per poll, GEODE_CAST_WATCH_*. announced: the
 * stream was handed over already; now_ms and waiting_since_ms on one clock; playlist_ready: enough segments to
 * start on; segment_age_ms: since the last cut (0 before the first); address_here: the phone still holds the
 * address it serves from. next_ms: when to poll again. fallback_release: 1 when the cast's own renderer, last
 * drawing at drew_ns, should be freed now that the stage leads. Pure. */
#define GEODE_CAST_WATCH_WAIT          0
#define GEODE_CAST_WATCH_READY         1
#define GEODE_CAST_WATCH_START_TIMEOUT 2
#define GEODE_CAST_WATCH_PLAYING       3
#define GEODE_CAST_WATCH_FROZEN        4
#define GEODE_CAST_WATCH_NETWORK_GONE  5
GEODE_API int             geode_cast_watch(int announced, int64_t now_ms, int64_t waiting_since_ms, int playlist_ready,
                                           int64_t segment_age_ms, int address_here);
GEODE_API int64_t         geode_cast_watch_next_ms(int announced);
GEODE_API int             geode_cast_fallback_release(int64_t now_ns, int64_t drew_ns);
/* The cast's render thread (core/output/android/CastRenderHost; EGL, MediaCodec; Android build only). It opens the
 * encoders (geode_cast_encoder_*), makes an EGL context current on the encoder's input window and, once a frame
 * slot (geode_frame_clock_*), blits the stage's copy from the tee (geode_cast_tee_*) or, while the stage draws
 * nothing, draws a renderer of its own on the look handed to set_look, swaps the frame into the encoder and pumps
 * its sound; the fallback renderer is made on first need and freed by geode_cast_fallback_release.
 *   start:    server and webrtc are the stream's handles and music the player's ring (as geode_cast_encoder_open
 *             takes them); assets and cache_dir make the fallback renderer. NULL when the thread would not start.
 *   set_look: the whole look, from any thread, before or after start; a renderer made later is given it whole.
 *   stop:     asks the thread to end and waits up to timeout_ms for it to close the encoders; 1 once it has. The
 *             server and WebRTC may be freed only after that, however late; free joins and frees.
 *   failed:   1 once the thread ended on an error it was not asked to (the cast should end).
 *   take_director_moments: the fallback renderer's Auto moments since the last call.
 *   set_stage_flags: what the shell last saw of the stage (GEODE_STAGE_*), kept for the process; set_device_ring:
 *             the phone's output-mix ring while the app hears other apps, NULL for the player's. Any thread. */
typedef struct geode_cast_render geode_cast_render;
GEODE_API geode_cast_render* geode_cast_render_start(int64_t server, int64_t webrtc, const geode_pcm_ring* music,
                                                     AAssetManager* assets, const char* cache_dir);
GEODE_API void            geode_cast_render_set_look(geode_cast_render*, const geode_viz_look* look);
GEODE_API int             geode_cast_render_stop(geode_cast_render*, int64_t timeout_ms);
GEODE_API int             geode_cast_render_failed(const geode_cast_render*);
GEODE_API int             geode_cast_render_take_director_moments(geode_cast_render*);
GEODE_API void            geode_cast_render_free(geode_cast_render*);
GEODE_API void            geode_cast_render_set_stage_flags(unsigned flags);
GEODE_API void            geode_cast_render_set_device_ring(const geode_pcm_ring* ring);

/* A QR code (util/QrCode): byte mode, level M, versions 1..10, the best mask. Writes side * side modules (1 =
 * dark, row-major) into out (GEODE_QR_MAX_MODULES bytes) and returns side; 0 when size > GEODE_QR_MAX_BYTES. */
#define GEODE_QR_MAX_BYTES 213
#define GEODE_QR_MAX_SIDE 57
#define GEODE_QR_MAX_MODULES (GEODE_QR_MAX_SIDE * GEODE_QR_MAX_SIDE)
GEODE_API int             geode_qr_encode(const uint8_t* data, int size, uint8_t* out, int cap);

/* A Customize slider's maths (viz/panel/SliderMap). Pure, any thread.
 * off_default: 1 when value is more than 1e-4 from def (NaN: no default, 0), so Reset would move it.
 * int_steps: the ticks between first and last of a whole-number slider. int_value: the whole number a
 * stepped position means, rounded to the nearest and clamped into first..last.
 * log_position: value's 0..1 place on a log track over lo..hi (0 < lo < hi); log_value: its inverse,
 * lo * (hi / lo)^position. A track that is not 0 < lo < hi pins to position 0 and value lo. */
GEODE_API int             geode_slider_off_default(float value, float def);
GEODE_API int             geode_slider_int_steps(int first, int last);
GEODE_API int             geode_slider_int_value(float position, int first, int last);
GEODE_API float           geode_slider_log_position(float value, float lo, float hi);
GEODE_API float           geode_slider_log_value(float position, float lo, float hi);

/* Which controls of one wave or envelope slot do anything as it stands (viz/panel/ModControls), so Customize shows only
 * those: GEODE_MOD_SHOWS_* bits. lfos and adsrs are the saved texts (api/geode_modulation_api.h); 0 for a slot
 * outside them. Pure, any thread. */
#define GEODE_MOD_SHOWS_WAVE 1u /* wave shape, timing and moment: the source is the steady wave */
#define GEODE_MOD_SHOWS_RATE 2u /* the cycle time: a steady wave on its own period (free timing, or on a moment) */
#define GEODE_MOD_SHOWS_KNOB 4u /* the macro knob picker: the slot moves a Macro knob */
GEODE_API unsigned        geode_mod_lfo_shows(const char* lfos, int slot);
GEODE_API unsigned        geode_mod_adsr_shows(const char* adsrs, int slot);

/* The widget's generated glow (viz/WidgetGlow), for a style with no thumbnail: writes 3 lights x 6 floats into
 * out[18] (x and y as shares of the bitmap's width and height, radius as a share of its width, hue in degrees,
 * saturation, value) and returns 3; 0 for a null out. `art_hue` below zero means no cover; `style_hash` is Java's
 * String.hashCode of the style id and counts only when `has_style`. Pure, any thread. */
GEODE_API int             geode_widget_glow(float art_hue, float art_span, int has_style, int style_hash, int tick,
                                            float* out);

#ifdef __cplusplus
}
#endif
