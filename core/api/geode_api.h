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

/* Native visualizer: one renderer per GL surface. Setters may be called from any thread and are latched
 * for the next frame; the calls under "GL thread" need the surface's context current. */
#define GEODE_LFO_SLOTS 3
#define GEODE_LFO_CONFIG_FLOATS 8   /* enabled, source, target, wave, rate_seconds, depth, polarity, curve */
#define GEODE_ADSR_SLOTS 2
#define GEODE_REACT_ROUTES 12
#define GEODE_REACT_ROUTE_FLOATS 8  /* enabled, source, target, mode, amount, curve, attack_seconds, release_seconds */
#define GEODE_ADSR_CONFIG_FLOATS 10 /* enabled, attack, decay, sustain, release, amount, band, gate_threshold,
                                       sustain_track, retrigger; followed by the target ordinals */

GEODE_API geode_viz*  geode_viz_create(AAssetManager* assets, const char* cache_dir);
GEODE_API void        geode_viz_destroy(geode_viz*);
GEODE_API int         geode_viz_param_count(void);
GEODE_API const char* geode_viz_param_name(int index);   /* NULL past the end */
/* Every scene parameter in geode_viz_param_name order; ints and flags travel as floats. */
GEODE_API void        geode_viz_set_params(geode_viz*, const float* values, int count);
GEODE_API void        geode_viz_set_reduced_motion(geode_viz*, int on);
GEODE_API void        geode_viz_set_layer(geode_viz*, const char* scene_id, float mix, int blend_mode); /* "" = none */
GEODE_API void        geode_viz_set_transition(geode_viz*, const char* id, int64_t duration_ms);
GEODE_API void        geode_viz_begin_param_morph(geode_viz*, float seconds);
GEODE_API void        geode_viz_set_touch(geode_viz*, const float* xy_ndc, int points);   /* 0 points = all lifted */
/* One finger sample in 0..1 surface space with its motion, for the fluid smear and the water ripples. */
GEODE_API void        geode_viz_queue_touch_stroke(geode_viz*, float nx, float ny, float ndx, float ndy, float dt, float strength);
/* GLSL for the fluid style's force and dye injection; "" restores the built-in splat. */
GEODE_API void        geode_viz_set_fluid_injection(geode_viz*, const char* force_source, const char* dye_source);
GEODE_API void        geode_viz_load_milk_preset(geode_viz*, const char* path);
GEODE_API void        geode_viz_reload_milk_preset(geode_viz*);
GEODE_API void        geode_viz_set_milk_texture_dir(geode_viz*, const char* dir);
/* The preset MilkDrop last compiled successfully, once; returns the full length, 0 when none is pending.
 * Every call takes it, a length query (out = NULL) too: pass a buffer the first time. */
GEODE_API size_t      geode_viz_take_milk_preset_loaded(geode_viz*, char* out, size_t capacity);
/* Section changes and drops the auto-director committed a look at since the last call; handed out once. */
GEODE_API int         geode_viz_take_director_style_moments(geode_viz*);
GEODE_API void        geode_viz_push_pcm(geode_viz*, const float* mono, int count);
/* The reaction source catalogue, one line per source in wire order: "NAME|Label|GROUP|event(0/1)\n".
 * Writes at most capacity bytes (NUL-terminated when it fits) and returns the length it needs. */
GEODE_API int         geode_react_source_catalog(char* out, int capacity);
/* Customize › Macros (core/viz/Macros): six knobs, -1..1, process-wide, read by every renderer each frame.
 * surprise writes `count` knobs to out; starters lists "Name|e,s,c,m,sp,ch" lines like the source catalogue. */
GEODE_API void        geode_macros_set(const float* knobs, int count);
GEODE_API void        geode_macros_surprise(const float* knobs, int count, unsigned lock_mask, long long seed, float* out);
GEODE_API int         geode_macro_starters(char* out, int capacity);
/* The auto-director (core/viz/Director): the music's feeling deciding palette, behaviour and routing over
 * every style. amount 0 off .. 1 full, eased in and out by each renderer; seed is the play's, so a new seed
 * is a new play (the renderer forgets the song so far) and an export given the same seed makes the same film. */
GEODE_API void        geode_director_set(float amount, long long seed);
/* Note & key reactions (core/viz/NoteResponse): notes, chords, key and mood driving the style knobs, note
 * colours, the tint and a gentle shape and distortion on every style. Process-wide like the director; each
 * renderer eases it in and out, and off (0) is exactly the picture without it. The seed is the director's. */
GEODE_API void        geode_note_response_set(int enabled);
/* The renderer's own sound engine: interleaved PCM in play order, every sample once, written on the render
 * thread before geode_viz_render; the next render steps it with that frame's dt. */
GEODE_API void        geode_viz_write_audio(geode_viz*, const float* interleaved, int frames, int channels, int sample_rate);
GEODE_API void        geode_viz_reset_audio(geode_viz*);
GEODE_API void        geode_viz_set_sound_tuning(geode_viz*, const GeodeSoundTuning*);
GEODE_API void        geode_viz_set_custom_shader(geode_viz*, const char* scene_id, const char* fragment_source);
/* The user source the scene last compiled; returns its full length, 0 when it draws the built-in style. */
GEODE_API size_t      geode_viz_custom_shader(geode_viz*, const char* scene_id, char* out, size_t capacity);
GEODE_API void        geode_viz_set_lfo(geode_viz*, int slot, const float* config, int count);
GEODE_API void        geode_viz_set_adsr(geode_viz*, int slot, const float* config, int count);
/* Every reaction route at once, GEODE_REACT_ROUTE_FLOATS per route; routes past count / ROUTE_FLOATS are disabled. */
GEODE_API void        geode_viz_set_reactions(geode_viz*, const float* routes, int count);
/* A style's DNA (core/viz/ReactionDna): the routes it plays until the user edits them, in the
   geode_viz_set_reactions layout. Writes at most capacity / GEODE_REACT_ROUTE_FLOATS routes into
   out and returns how many. Pure: no viz handle, the same id and seed always give the same routes. */
GEODE_API int         geode_style_reactions(const char* style_id, int seed, float* out, int capacity);
/* Customize › Music, "the hand wins per moment" (core/viz Reactions.hpp hand::, ReactionDna.hpp). The hand mask
   is bit i for moment i (GEODE_MUSIC_MOMENTS, in the page's order) plus GEODE_HAND_UNCLAIMED for every source
   no moment claims; GEODE_HAND_ALL is a whole list of the user's (the route editor, a list saved before the
   mask). `user` is the user's routes in the geode_viz_set_reactions layout, NULL (or has_user 0) for none.
   geode_hand_set is process-wide like geode_director_set: every renderer's director keeps off those moments. */
#define GEODE_MUSIC_MOMENTS 10
#define GEODE_MUSIC_ACTIONS 7
#define GEODE_HAND_UNCLAIMED 0x40000000u
#define GEODE_HAND_ALL 0x400003FFu
#define GEODE_MUSIC_STATE_FLOATS 5  /* per moment: mine, route count, strength, on-action bits, fits-action bits */
GEODE_API void        geode_hand_set(unsigned mask);
/* What plays: the user's routes on the hand's sources, the DNA's (dealt from id and seed) on the rest. */
GEODE_API int         geode_style_reactions_hand(const char* style_id, int seed, const float* user, int user_floats, int has_user,
                                                 unsigned mask, float* out, int capacity);
/* Every moment's row, GEODE_MUSIC_STATE_FLOATS each, into out (capacity floats); returns the moments written. */
GEODE_API int         geode_music_state(const char* style_id, int seed, const float* user, int user_floats, int has_user,
                                        unsigned mask, float* out, int capacity);
/* One tap: op 0 toggles `action` on `moment`, 1 sets its strength to `value`, 2 resets it to Auto. Writes the
   user's new routes into out and the new mask and has_user (0: all Auto) into out_mask / out_has_user; returns
   the route count. */
GEODE_API int         geode_music_edit(const char* style_id, int seed, const float* user, int user_floats, int has_user,
                                       unsigned mask, int op, int moment, int action, float value, float* out, int capacity,
                                       unsigned* out_mask, int* out_has_user);
/* The LfoTarget ordinal a new route of `action` moves, so the shell can hide a chip the style ignores. */
GEODE_API int         geode_music_action_target(int action);
/* Gyro look, -1..1 per axis; the camera reads it only while camGyro is on. */
GEODE_API void        geode_viz_set_look(geode_viz*, float yaw, float pitch);
GEODE_API void        geode_viz_set_thermal(geode_viz*, int platform_status, float headroom); /* status < 0 = unknown */
GEODE_API void        geode_viz_set_paced_fps(geode_viz*, float fps);
/* Settings › Performance (live surfaces): scales 0..1, post_fx / adaptive 0 or 1. Export never calls it. */
GEODE_API void        geode_viz_set_quality(geode_viz*, float render_scale, float march_scale, int post_fx, int adaptive);
/* Settings › Performance › Liquid on a hot phone: 0 simplify, 1 full, 2 pause (liquid::HotPolicy). */
GEODE_API void        geode_viz_set_liquid_hot_policy(geode_viz*, int policy);
/* What Liquid mode ran last frame: 0 off, 1 full, 2 lite, 3 warp, 4 paused (liquid::Mode). 0 for a null handle. */
GEODE_API int         geode_viz_liquid_mode(const geode_viz*);
GEODE_API void        geode_viz_set_offscreen(geode_viz*, int on);
/* Frosted glass under the app's sheet (core/viz/UiBlurPass): blurs the finished frame inside one rounded
 * rect, after the composite. The rect is normalised to the viewport with y DOWN (a view's left/top/right/
 * bottom in pixels over the visual's width/height); strength 0..1 mixes the blur in, 0 turns it off and
 * costs nothing; corner_px is the corner radius in surface pixels. Any thread, latched for the next frame.
 * An offscreen (export) renderer never draws it. */
GEODE_API void        geode_viz_set_ui_blur_region(geode_viz*, float left, float top, float right, float bottom,
                                                   float strength, float corner_px);
/* An export's own auto-director (amount 0..1, the play's seed): from now on this renderer ignores
 * geode_director_set, so the live player's switch, track and free taste never reach the render. */
GEODE_API void        geode_viz_set_director(geode_viz*, float amount, long long seed);
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
 * segment began, style id, MilkDrop preset path or "" for none) and a beat cut stepping through
 * `cut_count` style ids every `every_beats` beats of the renderer's own sound engine. Both empty
 * clears it. `locked_ids` is the entitlement: the `locked_count` styles this user may not draw (none
 * for Pro); cues and cut ids naming one are dropped. Strings are copied. */
GEODE_API void        geode_viz_set_style_schedule(geode_viz*, const double* at_ms, const char* const* scene_ids,
                                                   const char* const* milk_paths, int count,
                                                   const char* const* cut_ids, int cut_count, int every_beats,
                                                   const char* const* locked_ids, int locked_count);
GEODE_API void        geode_viz_render(geode_viz*, double time_seconds, uint32_t target_fbo);
GEODE_API void        geode_viz_release_scenes(geode_viz*);

/* Playback DSP: gain -> 10-band equalizer -> crossfeed -> lookahead limiter on float interleaved PCM, one
 * chain per input format, switched on the Media3 AudioProcessor contract (audio/dsp/DspProcessor):
 * configure stages the next format's chain, and the old one keeps processing the draining tail until flush
 * makes the staged one live. Setters may be called from any thread and reach every chain; configure, flush
 * and process run on the playback thread, and process never allocates, locks or logs. */
#define GEODE_DSP_BANDS 10
#define GEODE_DSP_MIN_MILLIBELS (-1500)
#define GEODE_DSP_MAX_MILLIBELS 1500

GEODE_API geode_dsp* geode_dsp_create(void);
GEODE_API void       geode_dsp_destroy(geode_dsp*);
/* Stages the chain for the format queued after the next flush; 0 when it cannot run it (not 1 or 2
 * channels), and that format then passes through untouched. */
GEODE_API int        geode_dsp_configure(geode_dsp*, int sample_rate, int channels);
GEODE_API void       geode_dsp_flush(geode_dsp*);                              /* commits, then clears filter state */
GEODE_API float      geode_dsp_band_center_hz(int band);
GEODE_API void       geode_dsp_set_enabled(geode_dsp*, int enabled);          /* the equalizer and bass boost */
GEODE_API void       geode_dsp_set_band(geode_dsp*, int band, int millibels);
GEODE_API void       geode_dsp_set_bass_boost(geode_dsp*, int permille);      /* 0..1000 */
GEODE_API void       geode_dsp_set_loudness_mb(geode_dsp*, int millibels);    /* 0..1000 makeup gain */
GEODE_API void       geode_dsp_set_gain_db(geode_dsp*, float db);             /* ReplayGain + preamp */
GEODE_API void       geode_dsp_set_crossfeed(geode_dsp*, int enabled);
GEODE_API void       geode_dsp_set_limiter(geode_dsp*, int enabled);
/* In place, RT-safe. `channels` is the format the block was queued in: a block that is not the live chain's
 * passes through untouched, and frames are clamped to capacity_floats / channels. */
GEODE_API void       geode_dsp_process(geode_dsp*, float* interleaved, size_t capacity_floats, size_t frames, int channels);

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

/* Voice "play X" over the library (core/library/Search). `rows` holds 3 UTF-8 strings per track, title,
 * artist, album, `track_count` tracks in library order. `query` is the spoken query; `focus`, `artist`,
 * `album` and `title` are MediaStore's EXTRA_MEDIA_FOCUS / _ARTIST / _ALBUM / _TITLE (NULL = absent).
 * Writes up to `max_out` matching track indices to `out` and returns how many match in all, so a count
 * above `max_out` means the buffer was short. Nothing asked plays the whole library; 0 = no match. */
GEODE_API int         geode_library_search(const char* const* rows, int track_count, const char* query,
                                           const char* focus, const char* artist, const char* album,
                                           const char* title, int* out, int max_out);

/* Scene and style catalogue (core/viz/Catalogue): every style id, its renderer kind, the style families,
 * which Customize controls reach which styles, and the ranges the shell's controls quote, as one versioned
 * '|'-separated line table (see Catalogue.hpp for the rows). Static: the pointer stays valid for the
 * process; the shell parses it once. Display labels and the Pro rule are not in it. */
GEODE_API const char* geode_catalogue_text(void);

/* Customize randomizer and Reset tab (core/viz/ParamRandomizer) over SceneParams
 * in the geode_viz_param_name wire order: geode_viz_param_count floats, ints and flags as floats. Pure: no viz
 * handle; the same arguments always give the same result. Controls are named by their ParamKeys strings, tabs
 * by their CustomizeTab names (a tab the engine does not know rolls and resets nothing). in and out may alias.
 * style_id NULL or empty = any style; tab NULL = every tab; locked = newline-joined control names, NULL = none.
 * Returns 1 when the roll changed something, 0 when out is a copy of in. */
GEODE_API int         geode_params_randomize(const char* style_id, const char* tab, const char* locked, uint64_t seed,
                                             const float* in, float* out);
GEODE_API void        geode_params_reset_tab(const char* tab, const float* in, float* out);
/* Every rolled control as "TAB|ParamKeys string" lines, in roll order: what the panel offers to lock and what
 * each tab's Randomize and Reset cover. Static: the pointer stays valid for the process. */
GEODE_API const char* geode_params_roll_keys(void);

/* Export resampler (audio/dsp/PcmResampler): interleaved 16-bit PCM to the AAC encoder's rate; box filter
 * down, linear up. Streaming: state carries across calls. Any one thread at a time. There is no flush: a
 * partial output frame at the end is dropped, as the export always did. */
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
GEODE_API int             geode_loudness_has_block(geode_loudness*);   /* 1 once a 400 ms block exists */
GEODE_API void            geode_loudness_finish(geode_loudness*, geode_loudness_report* out);   /* non-destructive */

/* Export post-passes (viz/frame/ExportPasses, #442): the non-Pro watermark, a loop render's seam, and a Studio
 * render's GL Transition with the capture that feeds it, each a frame::Pass on the export surface. The pure
 * calls (text_px, tile_*, seam_weight) run anywhere; every other call needs the export's GL context current
 * on the calling thread, destroy included (it deletes GL names). A create always returns a handle (NULL only
 * when out of memory); geode_export_pass_error says whether it built ("" when it did). None of them times a
 * frame: they draw the frame the caller hands them, when it does. */
typedef struct geode_export_pass geode_export_pass;
GEODE_API const char*         geode_export_pass_error(const geode_export_pass*);   /* "" when clean or NULL */
GEODE_API void                geode_export_pass_destroy(geode_export_pass*);
/* The wordmark's text size for a frame's short side, and the power-of-two tile for the measured text width. */
GEODE_API float               geode_export_watermark_text_px(int short_side);
GEODE_API int                 geode_export_watermark_tile_width(float text_width);
GEODE_API int                 geode_export_watermark_tile_height(int tile_width);
/* rgba: the tile, tile_w x tile_h premultiplied RGBA8, top row first (an ARGB_8888 Bitmap's pixels). */
GEODE_API geode_export_pass*  geode_export_watermark_create(AAssetManager*, const uint8_t* rgba, int tile_w, int tile_h);
/* Over the default framebuffer (the encoder surface), frame_w x frame_h pixels, after the scene. */
GEODE_API void                geode_export_watermark_draw(geode_export_pass*, int frame_w, int frame_h);
/* Loop seam: up to `requested` full-frame textures of stash (returns how many fitted), filled by capture from
 * the default framebuffer and laid back over it by blend at closing frame `tail` of `seam_frames`. */
GEODE_API float               geode_export_seam_weight(int tail, int seam_frames);
GEODE_API geode_export_pass*  geode_export_seam_create(AAssetManager*, int width, int height);
GEODE_API int                 geode_export_seam_allocate(geode_export_pass*, int requested);
GEODE_API void                geode_export_seam_capture(geode_export_pass*, int index);
GEODE_API void                geode_export_seam_blend(geode_export_pass*, int index, int tail, int seam_frames);
/* The seam driver (viz/frame/LoopSeam, #603) over those two. The scene has just drawn source frame `frame` of
 * a stop of loop_frames + seam_frames into the default framebuffer; seam_frame captures it (an opening frame:
 * returns -1, not presented) or, on a closing frame, blends the stash over it, and returns the frame's
 * presentation time in ns at fps. The caller then draws the watermark, sets that time and swaps. Needs the GL
 * context. budgeted_crossfade_ms (pure): the crossfade a loop keeps under the stash's 192 MB GPU budget at
 * width x height (crossfade_frames: its frames at fps); crossfade_ms unchanged when it fits. */
GEODE_API int64_t             geode_export_seam_frame(geode_export_pass*, int frame, int loop_frames, int seam_frames, int fps);
GEODE_API int64_t             geode_export_seam_budgeted_crossfade_ms(int64_t crossfade_ms, int crossfade_frames, int fps,
                                                                      int width, int height);
/* Studio transitions, on Media3's GL thread inside the framebuffer and viewport it bound. id is a GL
 * Transitions name (gl_transitions.json); an unknown one only copies. from_rgba: bottom row first, taken once. */
GEODE_API geode_export_pass*  geode_export_transition_create(AAssetManager*, const char* id, int64_t duration_us);
GEODE_API void                geode_export_transition_configure(geode_export_pass*, int width, int height);
GEODE_API void                geode_export_transition_set_from(geode_export_pass*, const uint8_t* rgba, int width, int height);
GEODE_API void                geode_export_transition_draw(geode_export_pass*, unsigned input_tex, int64_t pts_us);
/* A clip's last frame for the next clip's transition: draws input_tex through unchanged and keeps a copy;
 * read writes it (width x height RGBA8, bottom row first) and returns 1, or 0 when none or capacity is short. */
GEODE_API geode_export_pass*  geode_export_capture_create(AAssetManager*);
GEODE_API void                geode_export_capture_configure(geode_export_pass*, int width, int height);
GEODE_API void                geode_export_capture_draw(geode_export_pass*, unsigned input_tex);
GEODE_API int                 geode_export_capture_read(geode_export_pass*, uint8_t* out, size_t capacity);

/* Rest when silent (sound/Silence): the live wallpaper's and the dream's silence rule. A poll hands in the
 * newest planar samples written since the last one (frames 0 when nothing was written) and the time sound
 * was last heard, and gets that time back, moved to now when any sample is above about -42 dBFS. Resting is
 * nothing heard for 6 s. Pure: the caller keeps the one timestamp. */
GEODE_API long long       geode_silence_heard_at(const float* const* channels, int channel_count, int frames,
                                                 long long now_ms, long long heard_at_ms);
GEODE_API int             geode_silence_resting(long long now_ms, long long heard_at_ms);   /* 1 = may rest */

#ifdef __cplusplus
}
#endif
