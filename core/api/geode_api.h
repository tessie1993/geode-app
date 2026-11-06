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
/* Customize › Advanced › Layers (core/viz/LayerState): a second style over the main one, process-wide like
 * geode_director_set, so live, export, the wallpaper and the dream all draw it. "" (or null) = no layer; mix
 * 0..1; blend_mode is the BlendMode ordinal. An export pins the layer it started with (geode_viz_set_offscreen). */
GEODE_API void        geode_layer_set(const char* scene_id, float mix, int blend_mode);
/* The renderer's own sound engine: interleaved PCM in play order, every sample once, written on the render
 * thread before geode_viz_render; the next render steps it with that frame's dt. */
GEODE_API void        geode_viz_write_audio(geode_viz*, const float* interleaved, int frames, int channels, int sample_rate);
GEODE_API void        geode_viz_reset_audio(geode_viz*);
GEODE_API void        geode_viz_set_sound_tuning(geode_viz*, const GeodeSoundTuning*);
/* Customize › Advanced › Shader code (core/viz/ShaderOverrides): the user's fragment source for a style, process-wide
 * like geode_director_set, so live, export, the wallpaper and the dream all draw it. A null or empty source clears it
 * (the built-in style again). Every renderer applies it at scene build and to its resident scenes on the next frame. */
GEODE_API void        geode_shader_override_set(const char* style_id, const char* fragment_source);
/* The style's override; returns its full length, 0 when it draws the built-in style. */
GEODE_API size_t      geode_shader_override(const char* style_id, char* out, size_t capacity);
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
   geode_hand_set is process-wide like geode_director_set: every renderer's director keeps off those moments.
   has_user 0 (no list of the user's: the DNA plays) makes the mask count as 0, whatever its bits. */
#define GEODE_MUSIC_MOMENTS 10
#define GEODE_MUSIC_ACTIONS 7
#define GEODE_HAND_UNCLAIMED 0x40000000u
#define GEODE_HAND_ALL 0x400003FFu
#define GEODE_MUSIC_STATE_FLOATS 5  /* per moment: mine, route count, strength, on-action bits, fits-action bits */
GEODE_API void        geode_hand_set(unsigned mask, int has_user);
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
/* One live frame's start (core/viz/frame/Cadence, #604), on the GL thread before geode_viz_set_scene and
   geode_viz_render. The shell passes what it holds every frame and native applies only what changed:
   paced_fps is this surface's own fps cap (0 = free-running; the thermal cap is applied under it natively);
   the rest is Settings › Performance (scales 0..1, post_fx / adaptive 0 or 1, liquid_hot_policy 0 simplify,
   1 full, 2 pause: liquid::HotPolicy). Export never calls it. Answers a bitmask of GEODE_FRAME_*; 0 for a
   null handle. */
#define GEODE_FRAME_CLEAR_ONLY     0x1 /* the first frame on a new surface, cleared to black: present it, do nothing else */
#define GEODE_FRAME_THERMAL_DUE    0x2 /* read PowerManager and call geode_thermal_push */
#define GEODE_FRAME_LIQUID_CHANGED 0x4 /* Liquid mode changed: GEODE_FRAME_LIQUID_MODE(due) */
/* What Liquid mode ran last frame: 0 off, 1 full, 2 lite, 3 warp, 4 paused (liquid::Mode). */
#define GEODE_FRAME_LIQUID_MODE(due) ((int)(((unsigned)(due) >> 8) & 0xFu))
GEODE_API int         geode_viz_frame_begin(geode_viz*, float paced_fps, float render_scale, float march_scale,
                                            int post_fx, int adaptive, int liquid_hot_policy);
GEODE_API void        geode_viz_set_offscreen(geode_viz*, int on);
/* Export: step every frame by exactly 1/fps instead of measuring the time between frames (#377); 0 = live. */
GEODE_API void        geode_viz_set_frame_rate(geode_viz*, int fps);
/* Frosted glass under the app's sheet (core/viz/UiBlurPass): blurs the finished frame inside one rounded
 * rect, after the composite. The rect is normalised to the viewport with y DOWN (a view's left/top/right/
 * bottom in pixels over the visual's width/height); strength 0..1 mixes the blur in, 0 turns it off and
 * costs nothing; corner_px is the corner radius in surface pixels. Any thread, latched for the next frame.
 * An offscreen (export) renderer never draws it. */
GEODE_API void        geode_viz_set_ui_blur_region(geode_viz*, float left, float top, float right, float bottom,
                                                   float strength, float corner_px);
/* The wallpaper's edge look (core/viz/frame/EdgeGlowPass): after the composite, black over the frame except
 * along the screen's rounded rim, the band breathing with the music's level. opts, in this order
 * (core/viz/frame/EdgeGlowMath.hpp): enabled (> 0.5 = on), then the part of the surface on screen in
 * surface pixels, y down: left, top, width, height (width or height <= 0 = the whole surface). Fewer than
 * GEODE_EDGE_GLOW_FLOATS floats, or any non-finite one, turns it off. Any thread, latched for the next
 * frame. Off by default; an offscreen (export) renderer never draws it. */
#define GEODE_EDGE_GLOW_FLOATS 5
GEODE_API void        geode_viz_set_edge_glow(geode_viz*, const float* opts, int count);
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
/* The same on 16-bit PCM in place (the sink's encoding): the int16 <-> float conversion is native too. A block
 * the chain does not run is left byte for byte as it came. */
GEODE_API void       geode_dsp_process_pcm16(geode_dsp*, int16_t* interleaved, size_t capacity_samples, size_t frames, int channels);
/* Bit-perfect USB (#353): whether the platform takes the next AudioTrack bit-perfect, from any thread. From the
 * next flush (Media3 flushes whenever it builds an AudioTrack) until a flush with it off, process passes every
 * block through untouched, whatever gain, ramp, equalizer, crossfeed or limiter is set. _live reports the
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

/* Voice "play X" over the library (core/library/Search). `rows` holds 3 UTF-8 strings per track, title,
 * artist, album, `track_count` tracks in library order. `query` is the spoken query; `focus`, `artist`,
 * `album` and `title` are MediaStore's EXTRA_MEDIA_FOCUS / _ARTIST / _ALBUM / _TITLE (NULL = absent).
 * Writes up to `max_out` matching track indices to `out` and returns how many match in all, so a count
 * above `max_out` means the buffer was short. Nothing asked plays the whole library; 0 = no match. */
GEODE_API int         geode_library_search(const char* const* rows, int track_count, const char* query,
                                           const char* focus, const char* artist, const char* album,
                                           const char* title, int* out, int max_out);

/* Studio's folders (core/library/StudioFiles): moves takes saved inside project folders
 * (projects_dir/<id>/takes/) to takes_dir/<id>/, once; a take already there wins. Returns how many could not
 * be moved (they stay where they were). Paths are UTF-8. */
GEODE_API int         geode_studio_move_takes_out(const char* projects_dir, const char* takes_dir);

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
/* The scene params schema (core/viz/ParamTable.inc): one row per geode_viz_param_name slot, giving the field's
 * name, kind, default, control range and whether a fade glides it, as a versioned '|'-separated line table:
 *   params|<version>|<count>
 *   param|<slot>|<name>|<FLOAT|INT|BOOL|CIRCULAR>|<default>|<min>|<max>|<FADE|HOLD>
 * slots 0..count-1 in order; ints and bools as numbers, like the wire. Static: the pointer stays valid for the
 * process; the shell parses it once. Labels and page placement stay in Kotlin. */
GEODE_API const char* geode_params_schema_text(void);

/* Customize panel map (core/viz/CustomizeMap): ten categories, each a free Basics section and Pro sections, and
 * which control lives in which. geode_customize_text is its versioned '|'-separated line table (rows in
 * CustomizeMap.hpp). Static: the pointer stays valid for the process. geode_customize_shown lists what the panel
 * offers on style_id ("CATEGORY" and "CATEGORY/section" lines); the pointer stays valid until this thread's next call.
 * geode_customize_randomize rolls one category's controls (named by its CustomizeMap name; an unknown one rolls
 * nothing), holding every other control and the newline-joined ParamKeys in locked; 1 when it changed something.
 * geode_customize_reset puts back every field the category's controls write. Wire as geode_params_randomize. */
GEODE_API const char* geode_customize_text(void);
/* 1 when the control named key (a ParamKeys string) is one a roll can move, so the panel offers to keep it. */
GEODE_API int         geode_customize_lockable(const char* key);
GEODE_API const char* geode_customize_shown(const char* style_id);
GEODE_API int         geode_customize_randomize(const char* style_id, const char* category, const char* locked, uint64_t seed,
                                                const float* in, float* out);
GEODE_API void        geode_customize_reset(const char* category, const float* in, float* out);

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
/* The same feed from interleaved s16 (a decoder's default output), scaled by 1/32768 as audio/dsp/PcmConvert
 * does, so the result equals converting first and calling geode_loudness_feed. */
GEODE_API void            geode_loudness_feed_s16(geode_loudness*, const int16_t* interleaved, size_t samples);
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

/* Fixed-rate frame clock (viz/frame/FixedRateClock): Cast's pacing, PTS and audio cut. Pure: the caller
 * owns GEODE_FRAME_CLOCK_STATE int64s (start ns, next slot, fps) and hands in now on one monotonic clock.
 * At most one frame per slot: a late take drops the missed slots instead of bursting. */
#define GEODE_FRAME_CLOCK_STATE 3
GEODE_API void            geode_frame_clock_start(int64_t* state, int fps, int64_t now_ns);
GEODE_API int64_t         geode_frame_clock_wait_ns(const int64_t* state, int64_t now_ns);   /* 0 = due now */
GEODE_API int64_t         geode_frame_clock_take(int64_t* state, int64_t now_ns);   /* the frame's PTS, ns */
GEODE_API int64_t         geode_frame_clock_audio_until_us(const int64_t* state);   /* end of the last frame taken */
/* The Cast stream's format: out[0] width, out[1] height, out[2] fps, out[3] keyframe interval (s). */
GEODE_API void            geode_cast_format(int* out);
/* The GL Transitions library (gl_transitions.json, core/viz/TransitionLibrary): the shell's one read of it,
   as the table encodeTransitionTable describes (UTF-8, control-separated). Read from the APK once per
   process; "" when the asset is missing. The pointer stays valid for the process. */
GEODE_API const char*     geode_transitions_table(AAssetManager* assets);
/* Settings › Performance (core/viz/frame/Quality): mode 0 Saver, 1 Balanced, 2 High. */
#define GEODE_PERF_TIER_FLOATS 5
/* Writes render scale, fps cap, march scale, post FX (0/1), adaptive (0/1); returns the count, 0 for a bad mode. */
GEODE_API int             geode_perf_tier(int mode, float* out, int capacity);
/* The first-run mode for this phone. total_ram_bytes 0 = unknown. */
GEODE_API int             geode_perf_mode_for_device(long long total_ram_bytes, int low_ram, int media_performance_class);
/* Surface run rule (viz/SurfaceRun): when the live wallpaper and the dream draw. flags are the GEODE_SURFACE_*
 * bits the caller observed; animate_until_ms is the wall-clock time the entitlement lets the visual move until
 * (GEODE_SURFACE_FOREVER for Pro, a trial's end, 0 for free). Pure: the caller restates on every change, and
 * after geode_surface_recheck_in_ms (-1 = never), since nothing announces a trial's end. */
#define GEODE_SURFACE_HAS_SURFACE            (1u << 0)
#define GEODE_SURFACE_VISIBLE                (1u << 1)
#define GEODE_SURFACE_PREVIEW                (1u << 2)
#define GEODE_SURFACE_COVERED                (1u << 3)
#define GEODE_SURFACE_BATTERY_SAVER          (1u << 4)
#define GEODE_SURFACE_PAUSE_IN_BATTERY_SAVER (1u << 5)
#define GEODE_SURFACE_SILENT                 (1u << 6)
#define GEODE_SURFACE_REST_WHEN_SILENT       (1u << 7)
#define GEODE_SURFACE_REDUCED_MOTION         (1u << 8)   /* the system's Remove animations */
#define GEODE_SURFACE_FOREVER                9223372036854775807LL
GEODE_API int             geode_surface_run_mode(unsigned flags, long long animate_until_ms, long long now_ms);   /* 0 stopped, 1 still, 2 animated */
GEODE_API int             geode_surface_listening(unsigned flags, long long animate_until_ms, long long now_ms);  /* 1 = hear sound */
GEODE_API int             geode_surface_entitled(long long animate_until_ms, long long now_ms);                   /* 1 = may move */
GEODE_API long long       geode_surface_recheck_in_ms(long long animate_until_ms, long long now_ms);              /* -1 = never */
/* Milliseconds to keep pacing after a restate from previous_mode to mode before stopping: the still frame's
 * settle (a scene is built across frames), 0 when one frame or the plain on/off is enough. */
GEODE_API long long       geode_surface_settle_ms(int previous_mode, int mode);
/* A surface's own frame rate (wallpaper options, Behaviour › Dream): the stored choice as the pacer caps to,
 * snapped to a listed rate; <= 0 gives the default. */
#define GEODE_SURFACE_DEFAULT_FPS            30
GEODE_API int             geode_surface_fps_choice(int requested_fps);
/* The listed rates, ascending, for a settings row: writes up to capacity, returns how many there are. */
GEODE_API int             geode_surface_fps_choices(int* out, int capacity);

/* The frame pacer (core/viz/frame/Pacer): which vsyncs a surface draws on, and the dt it draws with.
   `state` is seven int64s the shell owns, zeroed at first (GEODE_PACER_STATE_LONGS). */
#define GEODE_PACER_STATE_LONGS 7
/* The slot holding the cap the last vsync paced to, in millihertz (0 = none): the shell re-asks
   Surface.setFrameRate when it moves. */
#define GEODE_PACER_CAP_SLOT 6
/* One vsync: the dt in seconds to draw with, or 0 to skip it. cap_fps is the surface's own cap (<= 0 draws
   every vsync); the pacer applies min(cap_fps, the thermal cap). */
GEODE_API double          geode_pacer_on_vsync(long long* state, long long frame_time_ns, float cap_fps);
/* A start after a stop: forget the gap, keep the panel's period. */
GEODE_API void            geode_pacer_resume(long long* state);
/* The cap a surface asking for cap_fps runs at now, min(cap_fps, the thermal cap); 0 = none. For
   Surface.setFrameRate. */
GEODE_API float           geode_pacer_effective_cap(float cap_fps);

/* The process's one thermal state (core/viz/frame/Thermal): every renderer reads the same tier. The
   once-a-second cadence across the process is native; a live renderer learns its turn from
   GEODE_FRAME_THERMAL_DUE (geode_viz_frame_begin), then reads PowerManager and pushes. */
/* One reading: PowerManager's thermal status (< 0 = no thermal API) and its headroom forecast (NaN = none
   this time; the last real one holds a while). Settles the shared tier. */
GEODE_API void            geode_thermal_push(int platform_status, float headroom);
/* ── Export planning and clip maths (ADR 0006: core/output, core/signal/timeline; ADR 0003 ports) ──────────
 * Pure functions and plain handles, any thread, no GL. The Kotlin exporters (VideoExporter, LoopRender,
 * LoopExtend, StudioExporter, the Media3 clip effects, AudioTranscoder) call these instead of carrying the
 * maths themselves; the classes behind them are output/FrameClock, LoopPlan, EncodePlan, Chapters,
 * signal/timeline/Keyframes, SpeedRamp, viz/ClipGrade, util/CubeLut, audio/dsp/PcmConvert and PcmTiming. */

/* Frame clock (output/FrameClock): each value from the frame index alone, so nothing accumulates. */
GEODE_API int64_t geode_export_total_frames(int64_t duration_us, int fps);            /* >= 1 */
GEODE_API int64_t geode_export_frame_pts_ns(int64_t frame, int fps);                  /* floor(n*1e9/fps) */
GEODE_API int64_t geode_export_frame_time_ms(int64_t frame, int fps);                 /* floor(n*1000/fps) */
GEODE_API int64_t geode_export_samples_through(int64_t frame, int fps, int sample_rate); /* llround((n+1)*rate/fps) */

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
/* The dissolve shortened to what the seam stash holds at width x height; out may alias in. */
GEODE_API void    geode_loop_spec_budgeted(const geode_loop_spec* in, int width, int height, geode_loop_spec* out);
/* Fills the derived fields of a spec as given (no clamping): loop, crossfade and source frames, duration. */
GEODE_API void    geode_loop_spec_fill(geode_loop_spec* spec);
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

/* Encoder requests (output/EncodePlan). limit_bytes <= 0 means the render target, 3.8 GB. */
GEODE_API int     geode_export_capped_video_bps(int requested_bps, int64_t duration_us, int64_t audio_bytes,
                                                int64_t limit_bytes);
GEODE_API int64_t geode_export_audio_bytes(int64_t duration_us, int bps);
GEODE_API int64_t geode_export_muxer_hard_limit_bytes(void);
GEODE_API void    geode_export_frame_size(int short_side, int w_ratio, int h_ratio, int* out_w, int* out_h);
GEODE_API int     geode_export_fallback_fps(int requested_fps);

/* Chapter spans (output/Chapters); titles stay Kotlin, keyed by out_source (-1 = the lead-in chapter that
 * makes a marker list start at 0:00). Each writes min(count, capacity) spans and returns the count. */
GEODE_API int     geode_chapters_from_clips(const int64_t* starts_ms, const int64_t* durations_ms, int count,
                                            int64_t* out_start_ms, int64_t* out_duration_ms, int32_t* out_source,
                                            int capacity);
GEODE_API int     geode_chapters_from_markers(const int64_t* markers_ms, int count, int64_t total_ms,
                                              int64_t* out_start_ms, int64_t* out_duration_ms, int32_t* out_source,
                                              int capacity);

/* Keyframe tracks (signal/timeline/Keyframes). kind: 0 scalar, 1 vector2, 2 colour (4 floats), 3 toggle,
 * 4 choice; interp: 0 hold, 1 linear, 2 bezier (c1x, c1y, c2x, c2y; the Ease presets are beziers too).
 * A key at a time that already has one replaces it. One thread at a time per track. */
typedef struct geode_track geode_track;
GEODE_API geode_track* geode_track_create(void);
GEODE_API void         geode_track_destroy(geode_track*);
GEODE_API void         geode_track_clear(geode_track*);
GEODE_API void         geode_track_add_key(geode_track*, int64_t at_ms, int kind, const float* value4, int interp,
                                           float c1x, float c1y, float c2x, float c2y);
/* 1 and the value (kind, 4 floats) at t_ms; 0 when the track has no keys. */
GEODE_API int          geode_track_value_at(const geode_track*, int64_t t_ms, int* out_kind, float* out_value4);
GEODE_API float        geode_bezier_eval(float c1x, float c1y, float c2x, float c2y, float x);

/* Clip speed ramps (signal/timeline/SpeedRamp) for Media3's SpeedProvider, which asks in source time. */
typedef struct geode_speed_ramp geode_speed_ramp;
GEODE_API geode_speed_ramp* geode_speed_ramp_create(const geode_track* speed_track, int64_t clip_start_ms,
                                                    int64_t clip_duration_ms, int64_t step_ms); /* NULL: no keys */
GEODE_API void              geode_speed_ramp_destroy(geode_speed_ramp*);
GEODE_API float             geode_speed_ramp_speed_at(const geode_speed_ramp*, int64_t source_us);
GEODE_API int64_t           geode_speed_ramp_next_change_us(const geode_speed_ramp*, int64_t source_us); /* -1: none */
GEODE_API int64_t           geode_speed_ramp_source_span_us(const geode_speed_ramp*);
/* Copies min(count, capacity) segment starts (source µs) and speeds; returns the segment count. */
GEODE_API int               geode_speed_ramp_segments(const geode_speed_ramp*, int64_t* out_starts_us, float* out_speeds,
                                                      int capacity);
GEODE_API float             geode_clip_speed_sanitize(float speed);  /* NaN/<=0 -> 1, else 0.25..4 */

/* Clip grade (viz/ClipGrade): hue . saturation . contrast . brightness as a column-major 4x4. */
GEODE_API void    geode_clip_grade_matrix(float brightness, float contrast, float saturation_percent,
                                          float hue_degrees, float* out16);

/* Colour cubes (util/CubeLut): size^3 opaque ARGB, [r][g][b]. */
typedef struct geode_cube_lut geode_cube_lut;
GEODE_API geode_cube_lut* geode_cube_lut_parse(const char* text, size_t length);   /* NULL: not a usable .cube */
GEODE_API geode_cube_lut* geode_cube_lut_gamma(float red, float green, float blue, int size);
GEODE_API void            geode_cube_lut_destroy(geode_cube_lut*);
GEODE_API int             geode_cube_lut_size(const geode_cube_lut*);
GEODE_API const uint32_t* geode_cube_lut_argb(const geode_cube_lut*);              /* size^3 entries */

/* PCM conversions (audio/dsp/PcmConvert). downmix returns the frames written (0: not a downmix). */
GEODE_API size_t  geode_pcm_downmix_s16(const int16_t* in, size_t frames, int src_channels, int dst_channels,
                                        int16_t* out);
GEODE_API void    geode_pcm_float_to_s16(const float* in, size_t samples, int16_t* out);
GEODE_API void    geode_pcm_s16_to_float(const int16_t* in, size_t samples, float* out);

/* PCM timing (audio/dsp/PcmTiming): the AAC encoder's rate, s16 durations, the start skip and trim. */
GEODE_API int     geode_aac_rate_for(int source_rate);          /* own rate to 48k; above: 44.1k family -> 44.1k, else 48k */
GEODE_API int64_t geode_aac_end_us(int64_t last_pts_us, int sample_rate);   /* + one 1024-sample AAC frame */
GEODE_API int64_t geode_pcm_s16_bytes_to_us(int64_t bytes, int rate, int channels);   /* floor; 0 on bad args */
GEODE_API int64_t geode_pcm_samples_before_us(int64_t us, int rate, int channels);    /* floor(us*rate/1e6)*channels */
/* A decoded s16 buffer of `bytes` at buf_pts_us trimmed to start_us: returns the bytes to drop from its front
 * (whole frames; == bytes when none of it is kept) and writes the kept part's pts (0 = the start) to
 * out_pts_us when not NULL. A buffer that straddles the start keeps the frames from the start on. */
GEODE_API int64_t geode_pcm_trim_s16(int64_t start_us, int64_t buf_pts_us, int64_t bytes, int rate, int channels,
                                     int64_t* out_pts_us);
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

#ifdef __cplusplus
}
#endif
