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
#define GEODE_LFO_CONFIG_FLOATS 9   /* enabled, source, target, wave, rate_seconds, depth, polarity, curve, beats (0 = free; a shorter array of 8 stays free) */
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
/* The reaction source catalogue, one line per source in wire order: "NAME|Label|GROUP|event(0/1)\n".
 * Writes at most capacity bytes (NUL-terminated when it fits) and returns the length it needs. */
GEODE_API int         geode_react_source_catalog(char* out, int capacity);
/* The auto-director (core/viz/Director): the music's feeling deciding behaviour (its axes, camera moves,
 * motion dials) over every style; never a colour, never a route. on is the user's switch as set (1/0), which
 * an export directs by (geode_viz_pin_engine_settings);
 * amount 0 off .. 1 full is what live surfaces drive with now, eased in and out by each renderer; seed is the
 * play's, so a new seed is a new play (the renderer forgets the song so far) and an export pinned with it
 * makes the same film. */
GEODE_API void        geode_director_set(int on, float amount, long long seed);
/* The director's reach past its look (core/viz/DirectorReach), process-wide like geode_director_set and pinned
 * with it by an export. level: 0 the look alone, 1 + it shuffles Customize categories at a new section or a
 * drop, 2 + its own slow modulators on sliders; anything else is 0. Nothing happens while the director is off. */
GEODE_API void        geode_director_reach_set(int level);
/* Customize's keep set, which the reach never moves: one control key or "#CATEGORY" per line (the lock set
 * geode_params_randomize takes). Null or "" keeps nothing. */
GEODE_API void        geode_director_keep_set(const char* keys);
/* Note & key reactions (core/viz/NoteResponse): notes, chords, key and mood driving the six style knobs each
 * style spends its own way; no scene param and no colour. Process-wide like the director; each
 * renderer eases it in and out, and off (0) is exactly the picture without it. The seed is the director's. */
GEODE_API void        geode_note_response_set(int enabled);
/* Customize › Advanced › Layers (core/viz/LayerState): a second style over the main one, process-wide like
 * geode_director_set, so live, export, the wallpaper and the dream all draw it. "" (or null) = no layer; mix
 * 0..1; blend_mode is the BlendMode ordinal. An export pins the layer it started with (geode_viz_set_offscreen). */
GEODE_API void        geode_layer_set(const char* scene_id, float mix, int blend_mode);
/* The renderer's own sound engine: interleaved PCM in play order, every sample once, written on the render
 * thread before geode_viz_render; the next render steps it with that frame's dt. */
GEODE_API void        geode_viz_write_audio(geode_viz*, const float* interleaved, int frames, int channels, int sample_rate);
GEODE_API void        geode_viz_set_sound_tuning(geode_viz*, const GeodeSoundTuning*);
/* Customize › Advanced › Shader code (core/viz/ShaderOverrides): the user's fragment source for a style, process-wide
 * like geode_director_set, so live, export, the wallpaper and the dream all draw it. A null or empty source clears it
 * (the built-in style again). Every renderer applies it at scene build and to its resident scenes on the next frame. */
GEODE_API void        geode_shader_override_set(const char* style_id, const char* fragment_source);
/* The style's override; returns its full length, 0 when it draws the built-in style. */
GEODE_API size_t      geode_shader_override(const char* style_id, char* out, size_t capacity);
GEODE_API void        geode_viz_set_lfo(geode_viz*, int slot, const float* config, int count);
GEODE_API void        geode_viz_set_adsr(geode_viz*, int slot, const float* config, int count);
/* The routes layout: GEODE_REACT_ROUTE_FLOATS per route. What the reaction matrix plays is resolved natively
   (core/viz/ReactionResolve, #750) from the scene playing (geode_viz_set_scene, and each switch of an export's style schedule, #352), the params' reactSeed, and
   the user's routes under the hand mask set here: the style's DNA with no user list (has_user 0, routes and
   mask ignored), else geode_style_reactions_hand's merge. It is worked out again before a render only when one
   of those changed, so the shell pushes the user's side only when it changes. */
GEODE_API void        geode_viz_set_user_reactions(geode_viz*, const float* routes, int count, int has_user, unsigned mask);
/* A style's DNA (core/viz/ReactionDna): the routes it plays until the user edits them, in the
   routes layout. Writes at most capacity / GEODE_REACT_ROUTE_FLOATS routes into
   out and returns how many. Pure: no viz handle, the same id and seed always give the same routes. */
GEODE_API int         geode_style_reactions(const char* style_id, int seed, float* out, int capacity);
/* Customize › Music, "the hand wins per moment" (core/viz Reactions.hpp hand::, ReactionDna.hpp). The hand mask
   is bit i for moment i (GEODE_MUSIC_MOMENTS, in the page's order) plus GEODE_HAND_UNCLAIMED for every source
   no moment claims; GEODE_HAND_ALL is a whole list of the user's (the route editor, a list saved before the
   mask). `user` is the user's routes in the routes layout, NULL (or has_user 0) for none.
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
/* Customize › Music's ready-made recipes (core/viz ReactionDna.hpp EventRecipe), GEODE_EVENT_RECIPES in the
   menu's order. `current` is what plays, in the routes layout. Pure: no viz handle. */
#define GEODE_EVENT_RECIPES 30
#define GEODE_RECIPE_ON 1u    /* every route it would add to the style is in current */
#define GEODE_RECIPE_FITS 2u  /* turning it on stays within GEODE_REACT_ROUTES */
/* The recipe's routes the style can show (every route for a NULL style_id) into out; returns how many. */
GEODE_API int         geode_event_recipe_routes(int recipe, const char* style_id, float* out, int capacity);
/* GEODE_RECIPE_* flags for the recipe on the style over current; 0 for an unknown recipe or a NULL style. */
GEODE_API unsigned    geode_event_recipe_state(int recipe, const char* style_id, const float* current, int current_floats);
/* current with the recipe turned off if on, else on if it fits (replacing a route on the same source and
   target), into out; returns the route count. An unknown recipe or a NULL style leaves current as it was. */
GEODE_API int         geode_event_recipe_toggle(int recipe, const char* style_id, const float* current, int current_floats,
                                                float* out, int capacity);
/* Modulation targets (core/viz/ModTargets, #642): bit per Music action whose target reaches `style_id`, so the
   shell hides a chip the style ignores; and the whole target, source and curve table (scopes, chained slots,
   slot defaults and rate range, each source's natural polarity, and per style what a reaction, an envelope and
   each wave may move) as the line table ModTargets.hpp documents.
   Static: the pointer stays valid for the process; the shell parses it once. Labels stay in Kotlin. */
GEODE_API unsigned    geode_music_actions_for(const char* style_id);
GEODE_API const char* geode_mod_targets_text(void);
/* One gyro reading from SensorManager.getOrientation (radians: azimuth -pi..pi, pitch -pi/2..pi/2). The first after a
   reset is the baseline; viz/GyroLook turns the tilt from it into the -1..1 look per axis the camera reads only while
   camGyro is on. GL thread. */
GEODE_API void        geode_viz_gyro_sample(geode_viz*, float azimuth, float pitch);
/* The gyro is off (or switched on again): recentre the look to (0,0) and recapture on the next sample. GL thread. */
GEODE_API void        geode_viz_gyro_reset(geode_viz*);
/* One live frame's start (core/viz/frame/Cadence, #604), on the GL thread before geode_viz_set_scene and
   geode_viz_render. The shell passes what it holds every frame and native applies only what changed:
   paced_fps is the rate this surface's pacer draws at (GEODE_PACER_PACED_SLOT; 0 = free-running; the thermal
   cap is applied under it natively);
   the rest is Settings › Performance (scales 0..1, post_fx / adaptive 0 or 1, liquid_hot_policy 0 simplify,
   1 full, 2 pause: liquid::HotPolicy). The four *_version arguments are the shell's version counters (each
   grows on every write) of its scene params, LFO configs, ADSR configs and sound tuning (core/viz/frame/
   PushVersions, #751): native sets GEODE_FRAME_PUSH_* for each one it has not applied yet (an equal or older
   version is skipped; a new surface wants all four again), and the shell pushes exactly those, through
   geode_viz_set_params / _set_lfo / _set_adsr / _set_sound_tuning, before geode_viz_render. A clear-only
   frame wants none. Export never calls it. Answers a bitmask of GEODE_FRAME_*; 0 for a null handle. */
#define GEODE_FRAME_CLEAR_ONLY     0x1 /* the first frame on a new surface, cleared to black: present it, do nothing else */
#define GEODE_FRAME_THERMAL_DUE    0x2 /* ask PowerManager's headroom and call geode_thermal_push */
#define GEODE_FRAME_LIQUID_CHANGED 0x4 /* Liquid mode changed: GEODE_FRAME_LIQUID_MODE(due) */
#define GEODE_FRAME_ERROR_CHANGED  0x8 /* the error text changed, or this is the handle's first frame: geode_viz_last_error */
/* What Liquid mode ran last frame: 0 off, 1 full, 2 lite, 3 warp, 4 paused (liquid::Mode). */
#define GEODE_FRAME_LIQUID_MODE(due) ((int)(((unsigned)(due) >> 8) & 0xFu))
#define GEODE_FRAME_PUSH_PARAMS    0x10 /* push the scene params (geode_viz_set_params) */
#define GEODE_FRAME_PUSH_LFO       0x20 /* push every LFO slot (geode_viz_set_lfo) */
#define GEODE_FRAME_PUSH_ADSR      0x40 /* push every ADSR slot (geode_viz_set_adsr) */
#define GEODE_FRAME_PUSH_TUNING    0x80 /* push the sound tuning (geode_viz_set_sound_tuning) */
GEODE_API int         geode_viz_frame_begin(geode_viz*, float paced_fps, float render_scale, float march_scale,
                                            int post_fx, int adaptive, int liquid_hot_policy,
                                            long long params_version, long long lfo_version,
                                            long long adsr_version, long long tuning_version);
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
/* The live wallpaper's window and frame rate (core/viz/frame/WallpaperWindow.hpp), pure and stateless.
 * geode_wallpaper_window: the part of a surface_w x surface_h surface on a display_w x display_h screen,
 * from the raw WallpaperService.Engine.onOffsetsChanged values in its order (xOffset, yOffset, xOffsetStep,
 * yOffsetStep, xPixels, yPixels; fewer = unknown). Writes left, top, width, height (surface pixels, y down,
 * as geode_viz_set_edge_glow takes them) to out[4]. geode_wallpaper_fps: the wallpaper's target fps lowered
 * to the Performance cap (<= 0 or NaN = no cap); never 0 or NaN. Any thread. */
#define GEODE_WALLPAPER_OFFSETS 6
GEODE_API void        geode_wallpaper_window(float surface_w, float surface_h, float display_w, float display_h,
                                             const float* offsets, int count, float* out);
GEODE_API float       geode_wallpaper_fps(float target_fps, float cap_fps);
/* An export's engine settings (signal::SettingsLatch, #350), once at prepare: from now on this renderer never
 * reads geode_macro_bank_set, geode_note_response_set, geode_hand_set or geode_director_set again, so live
 * edits, the player's next track and its free taste never reach the render. The macro bank, the Note & key switch
 * and the hand mask are copied as they are now; the director is the user's switch and the play's seed, full for the
 * whole export when entitled (Pro or the trial, 1/0) or when duration_ms fits the free taste, else off
 * (director::forExport); the note entropy is 0, so an export is deterministic. Returns the pinned hand mask,
 * for the export's route merge (geode_style_reactions_hand); 0 for a null handle. */
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
 * segment began, style id, MilkDrop preset path or "" for none) and a beat cut stepping through
 * `cut_count` style ids every `every_beats` beats of the renderer's own sound engine. Both empty
 * clears it. `locked_ids` is the entitlement: the `locked_count` styles this user may not draw (none
 * for Pro); cues and cut ids naming one are dropped. Strings are copied. */
GEODE_API void        geode_viz_set_style_schedule(geode_viz*, const double* at_ms, const char* const* scene_ids,
                                                   const char* const* milk_paths, int count,
                                                   const char* const* cut_ids, int cut_count, int every_beats,
                                                   const char* const* locked_ids, int locked_count);
GEODE_API void        geode_viz_render(geode_viz*, double time_seconds, uint32_t target_fbo);
/* Offline (export) render: prepare pins the renderer for it, any thread: the offscreen tier
 * (geode_viz_set_offscreen(1)), cut transitions, and fixed fluid quality on every later set_params. */
GEODE_API void        geode_viz_prepare_offscreen(geode_viz*);
GEODE_API void        geode_viz_release_scenes(geode_viz*);
/* The surface is about to stop drawing (the host pauses it, the wallpaper hides): sheds this renderer's spare
 * scenes now, exactly as a TRIM_MEMORY_UI_HIDDEN trim does (geode_memory_trim), because that signal is only read
 * by a frame and a paused surface draws none. What is drawn, the layer, the born and the requested style stay.
 * GL thread, while the context is still current; without one it does nothing. */
GEODE_API void        geode_viz_trim_spares(geode_viz*);

/* Playback DSP: gain -> 10-band equalizer -> loudness makeup -> crossfeed -> lookahead limiter on float
 * interleaved PCM, one chain per input format, switched on the Media3 AudioProcessor contract (audio/dsp/DspProcessor):
 * configure stages the next format's chain, and the old one keeps processing the draining tail until flush
 * makes the staged one live. Setters may be called from any thread and reach every chain; configure, flush
 * and process run on the playback thread, and process never allocates, blocks or logs. */
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
GEODE_API void       geode_dsp_set_crossfeed(geode_dsp*, int enabled);
GEODE_API void       geode_dsp_set_limiter(geode_dsp*, int enabled);

/* Playback gain (audio/dsp/GainStage): ReplayGain, the UI fades, the sleep fade and the track crossfade, as
 * ramps linear in dB counted in samples, before the chain (#483). The shell only says targets and lengths;
 * the curves, the dB maths and when each applies are here. "Silence" is GEODE_DSP_SILENCE_DB or below (-inf
 * included); a frame there is multiplied by exactly 0. Setters from any thread, never blocking the audio. */
#define GEODE_DSP_SILENCE_DB (-80.0f)
/* The UI fade (pause, resume, skip): from wherever it is to db over ms; ms 0 jumps. */
GEODE_API void       geode_dsp_ramp_to(geode_dsp*, float db, int ms);
/* The sleep timer's time left in ms (< 0: cancelled or done). Its last 3 s ramp to silence, ending as the
 * timer ends; a cancel comes back up in 50 ms. Returns the ms until it next needs to hear the time left. */
GEODE_API int64_t    geode_dsp_sleep_fade(geode_dsp*, int64_t remaining_ms);
/* Settings › Playback › Crossfade: half fades out the tail of a track something follows, half fades in the
 * track that followed on. A skip, seek or pick starts at full level. */
GEODE_API void       geode_dsp_set_crossfade(geode_dsp*, int ms);
GEODE_API void       geode_dsp_set_follows_on(geode_dsp*, int follows_on);  /* a next item, or repeat one */
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

/* Scene and style catalogue (core/viz/Catalogue): every style id, its renderer kind and Styles look group, the style families,
 * which Customize controls reach which styles, and the ranges the shell's controls quote, as one versioned
 * '|'-separated line table (see Catalogue.hpp for the rows). Static: the pointer stays valid for the
 * process; the shell parses it once. Display labels and the Pro rule are not in it. */
GEODE_API const char* geode_catalogue_text(void);

/* The catalogue's presets section (core/viz/CataloguePresets, #648): the bundled looks, each a named set of
 * param values by #346's param name, and every bundled preset (name, style id, look), as one versioned
 * '|'-separated line table (see CataloguePresets.hpp for the rows). Static, like geode_catalogue_text. */
GEODE_API const char* geode_catalogue_presets_text(void);

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
/* The config params schema (core/viz/ConfigTable.inc): the values the data stores hold beside the scene params
 * (LFO and envelope slots, the palette maker's gradient, the look's envelope, the Studio clip edit,
 * the preset morph), one row each, as a versioned '|'-separated line table:
 *   configs|<version>|<count>
 *   config|<group>|<index>|<name>|<FLOAT|INT|BOOL>|<default>|<min>|<max>|<NONE|SECONDS|FRACTION|TURNS|DEGREES|POINTS|TIMES>
 * index = the row's place in its group (for lfo and adsr the float index of geode_viz_set_lfo / _adsr). A sibling of geode_params_schema_text, whose format it leaves alone. Static; parse once. */
GEODE_API const char* geode_config_schema_text(void);
/* The two-finger transform on the visual (core/viz/TouchTransform): a pinch's scale factor times Zoom, a twist in
 * degrees added to Rotation, each clamped to its schema range; a broken gesture value returns current unchanged. */
GEODE_API float       geode_touch_zoom(float current, float scale_factor);
GEODE_API float       geode_touch_rotation(float current, float degrees);

/* Customize panel map (core/viz/CustomizeMap): nine categories, each a free Basics section and Pro sections, and
 * which control lives in which. geode_customize_text is its versioned '|'-separated line table (rows in
 * CustomizeMap.hpp). Static: the pointer stays valid for the process. geode_customize_shown lists what the panel
 * offers on style_id ("CATEGORY" and "CATEGORY/section" lines); the pointer stays valid until this thread's next call.
 * geode_customize_randomize rolls one category's controls (named by its CustomizeMap name; an unknown one rolls
 * nothing), holding every other control and the newline-joined ParamKeys in locked; 1 when it changed something.
 * In locked, here and in geode_params_randomize, a line "#CATEGORY" keeps every control of that category.
 * geode_customize_reset puts back every field the category's controls write. Wire as geode_params_randomize. */
GEODE_API const char* geode_customize_text(void);
/* 1 when the control named key (a ParamKeys string) is one a roll can move, so the panel offers to keep it. */
GEODE_API int         geode_customize_lockable(const char* key);
/* Every such key, one per line, each once: the panel asks once for all its rows instead of once per row. Static. */
GEODE_API const char* geode_customize_lockable_keys(void);
GEODE_API const char* geode_customize_shown(const char* style_id);
GEODE_API int         geode_customize_randomize(const char* style_id, const char* category, const char* locked, uint64_t seed,
                                                const float* in, float* out);
GEODE_API void        geode_customize_reset(const char* category, const float* in, float* out);
/* Randomize for the waves (core/viz/WaveRandomizer): in and out are GEODE_LFO_SLOTS slots of GEODE_LFO_CONFIG_FLOATS
 * each, as geode_viz_set_lfo reads one; params is the look on screen (the geode_viz_set_params wire). running_only 0
 * is the wave editor's button: every slot switched on and rolled. 1 is a category roll: only the steady waves already
 * on, and none when locked keeps "@waves" or "#MUSIC". 1 when it changed something. */
GEODE_API int         geode_waves_randomize(const char* style_id, const char* locked, uint64_t seed, int running_only,
                                            const float* params, const float* in, float* out);
/* in with every field the category's controls write taken from the look in from (same wire); an unknown category
 * copies in. */
GEODE_API void        geode_customize_borrow(const char* category, const float* in, const float* from, float* out);
/* The macro bank (core/viz/MacroBank): the user's knobs, each a name, a value -1..1 and the links that say which
 * scene params it moves and how far each way. Pure: no viz handle, any thread. A bank travels as the versioned
 * '|'-separated line table MacroBank.hpp documents; the shell stores that string and decides nothing about it.
 * Every call that takes a bank reads any text (NULL, empty or unreadable = the default bank) and every edit
 * returns the whole new bank, normalized but otherwise unchanged when the edit is refused (an unknown knob or
 * target, a non-finite number, a full bank, removing a built-in knob, resetting one that is not). geode_macro_bank_default (the four built-in knobs, also Reset) and
 * geode_macro_bank_targets_text (what a link may name: target, param, range, default, scope, and the caps) are
 * static; every other pointer stays valid until this thread's next macro bank call.
 * target is a ModTargets wire name; up and down are the offsets at a full turn each way, as signed fractions of
 * the target's range; geode_macro_bank_add_link links a target the knob does not move yet at the starting weights. geode_macro_bank_live lists "knob|<id>|<up 0/1>|<down 0/1>" per knob: whether turning it
 * that way moves anything on style_id. geode_macro_bank_editor is the knob editor's one read (MacroBank.hpp's
 * editorText: the caps, what each knob offers, its links and every target, with what reaches style_id).
 * geode_macro_bank_move_knob steps a knob delta places along the bank. geode_macro_bank_set_curve sets how a link
 * follows the knob (curve: LINEAR, EASE_IN, EASE_OUT or S_CURVE; up and down stay its range). geode_macro_bank_add_knob_for
 * makes a knob that moves one Customize control (a ParamKeys name), named by native after it; geode_macro_bank_controls
 * lists every control a knob can move, its category and the knobs that move it (MacroBank.hpp's controlsText).
 * geode_macro_bank_roll gives a knob a random set of links (the same seed, the same set); geode_macro_bank_auto_name
 * is the name native offers when a knob is saved (not a bank: the name alone). geode_macro_bank_set makes a bank the process-wide one every renderer
 * adds to its params each frame, after the director and the note layer and before the LFOs. */
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
GEODE_API const char* geode_macro_bank_set_link(const char* bank, int knob_id, const char* target, float up, float down);
GEODE_API const char* geode_macro_bank_add_link(const char* bank, int knob_id, const char* target);
GEODE_API const char* geode_macro_bank_remove_link(const char* bank, int knob_id, const char* target);
GEODE_API const char* geode_macro_bank_set_curve(const char* bank, int knob_id, const char* target, const char* curve);
GEODE_API const char* geode_macro_bank_add_knob_for(const char* bank, const char* control);
GEODE_API const char* geode_macro_bank_controls(const char* bank);
GEODE_API const char* geode_macro_bank_roll(const char* bank, int knob_id, long long seed);
GEODE_API const char* geode_macro_bank_auto_name(const char* bank, int knob_id);
GEODE_API const char* geode_macro_bank_live(const char* bank, const char* style_id);
GEODE_API const char* geode_macro_bank_editor(const char* bank, const char* style_id);
/* Background image bake (core/viz/Underlay), before geode_viz_set_underlay_rgba: pure, no viz handle, any
 * thread. The shell decodes the picture, cuts it to geode_underlay_crop_rect (out = x, y, width, height of the
 * centred crop of src to the target's aspect) and scales it to the render size, then geode_underlay_finish
 * blurs and dims those ARGB_8888 pixels (0xAARRGGBB, row-major, width*height) in place. The clamps are the
 * controls' ranges: amount and dim 0..1 (NaN = 0), blur 0..geode_underlay_blur_max(). */
GEODE_API void        geode_underlay_crop_rect(int src_w, int src_h, int target_w, int target_h, int* out_xywh);
GEODE_API void        geode_underlay_finish(uint32_t* pixels, int width, int height, int blur_radius, float dim);
GEODE_API float       geode_underlay_clamp_amount(float amount);
GEODE_API float       geode_underlay_clamp_dim(float dim);
GEODE_API int         geode_underlay_clamp_blur(int radius);
GEODE_API int         geode_underlay_blur_max(void);

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
/* Pure, the loop render's report (viz/frame/LoopSeam, #752). loop_progress: the fraction of the whole render done
 * once source frame `frame` of stop `stop` of `stops` is drawn (each stop loop_frames + seam_frames frames),
 * rising in 0..1 and exactly 1 on the last frame. reported_crossfade_ms: seam_frames at fps in whole ms. */
GEODE_API float               geode_export_loop_progress(int stop, int stops, int frame, int loop_frames, int seam_frames);
GEODE_API int64_t             geode_export_loop_reported_crossfade_ms(int seam_frames, int fps);
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

/* What the player tells every live renderer's sound engine that the tap cannot hear (sound/PlayerSignals).
 * A track boundary: a new track (or the same one again) is a new play with `seed`; each live engine forgets
 * the tempo, key and sections at its next frame and fires sectionChange at full strength, crossfaded and
 * gapless tracks included. Returns the boundary's epoch (1, 2, …). The fade gain, 0..1: the player's pause
 * and skip fades times the crossfade, which the tap (before the volume) never hears; the frame carries it as
 * fadeGain, a reaction source. Player is source: 1 while the player writes the app's ring, 0 once the mic or
 * another app's audio takes it; a pause then holds the tempo, key and sections (no 2 s silence re-warm),
 * and fadeGain reads 1 under the others. All process-wide and latched, any thread; an export reads none. */
GEODE_API long long       geode_sound_track_boundary(long long seed);
GEODE_API void            geode_sound_set_fade_gain(float gain);
GEODE_API void            geode_sound_set_player_source(int on);
/* Which sound the wallpaper and the dream hear (sound/SoundSource): the app's ring while it is fed, else the
 * output-mix capture when permitted. The caller keeps the state (GEODE_SOUND_SOURCE_STATE_LONGS int64s, zeroed
 * = fresh) and polls with now, whether the app has a ring, which ring and its written-frame counter, whether
 * capture is permitted (option on and permission granted, or already running) and whether it runs. The answer
 * is GEODE_SOUND_SOURCE_* bits; the ring falls back after 1 s without a write and takes over again after 0.5 s
 * fed, so it never flaps. A start the shell could not make is reported back and retried after 10 s. Pure. */
#define GEODE_SOUND_SOURCE_STATE_LONGS 8
#define GEODE_SOUND_SOURCE_USE_CAPTURE 1     /* read the capture's input (while it runs), not the ring */
#define GEODE_SOUND_SOURCE_START_CAPTURE 2   /* try to start the capture now */
#define GEODE_SOUND_SOURCE_STOP_CAPTURE 4    /* stop the capture now */
GEODE_API int             geode_sound_source_step(long long* state, long long now_ms, int ring_present,
                                                  long long ring_id, long long ring_written,
                                                  int capture_permitted, int capture_running);
GEODE_API void            geode_sound_source_start_failed(long long* state, long long now_ms);
/* Settings › Audio's listening tuning (sound/Tuning): the one schema of GeodeSoundTuning. A slot is one
 * float of the struct in field order, GEODE_SOUND_TUNING_FLOATS of them; each call touches the first
 * min(count, slots) and ignores the rest. Clamping puts each slot in its range and turns a NaN or an
 * infinity into the slot's default. The engine clamps whatever it is given; the shell clamps too, so a
 * slider never shows a value the engine would not use. */
GEODE_API int             geode_sound_tuning_slots(void);
GEODE_API void            geode_sound_tuning_defaults(float* out, int count);
GEODE_API void            geode_sound_tuning_ranges(float* min_out, float* max_out, int count);
GEODE_API void            geode_sound_tuning_clamp(float* values, int count);   /* in place */

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

/* Auto's style timer (core/viz/AutoTimer), one per source, process-wide: the wait before the next look is the
 * user's interval plus a random spread drawn per look (a quarter of it, 10 s at most), never shorter than the
 * interval. now_ms is the shell's monotonic clock. restart: a look just went up. due: 1 once the wait is over
 * (it restarts itself); running = 0 (off, paused, nothing to step to) is never due. hold_s: how long the look
 * on screen is expected to show, 0 with no timer running; a .milk preset's `progress` runs over it. */
#define GEODE_AUTO_TIMER_PLAYLIST            0
#define GEODE_AUTO_TIMER_RANDOM              1
GEODE_API void            geode_auto_timer_restart(int timer, long long now_ms);
GEODE_API int             geode_auto_timer_due(int timer, long long now_ms, int interval_s, int running);
GEODE_API double          geode_auto_timer_hold_s(void);

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

/* The frame pacer (core/viz/frame/Pacer): which vsyncs a surface draws on, and the dt it draws with.
   `state` is eight int64s the shell owns, zeroed at first (GEODE_PACER_STATE_LONGS). */
#define GEODE_PACER_STATE_LONGS 8
/* The slot holding the cap the last vsync paced to, in millihertz (0 = none): the shell re-asks
   Surface.setFrameRate when it moves. */
#define GEODE_PACER_CAP_SLOT 6
/* The slot holding the rate the last vsync really paced to, in millihertz (0 = every vsync): the cap, or
   the next whole divide of the panel under it (45 fps for a 60 fps cap on a 90 Hz panel). The shell passes
   it to geode_viz_frame_begin as paced_fps. */
#define GEODE_PACER_PACED_SLOT 7
/* One vsync: the dt in seconds to draw with, or 0 to skip it. cap_fps is the surface's own cap (<= 0 draws
   every vsync); the pacer applies min(cap_fps, the thermal cap). */
GEODE_API double          geode_pacer_on_vsync(long long* state, long long frame_time_ns, float cap_fps);
/* A start after a stop: forget the gap, keep the panel's period. */
GEODE_API void            geode_pacer_resume(long long* state);
/* The cap a surface asking for cap_fps runs at now, min(cap_fps, the thermal cap); 0 = none. For
   Surface.setFrameRate. */
GEODE_API float           geode_pacer_effective_cap(float cap_fps);

/* The process's one thermal state (core/viz/frame/Thermal): every renderer reads the same tier. The
   cadence across the process is native: a live renderer learns from GEODE_FRAME_THERMAL_DUE
   (geode_viz_frame_begin), every ten seconds, that it asks PowerManager's headroom and pushes it; the
   status is pushed by PowerManager's status listener when it changes. */
/* One reading: PowerManager's thermal status (< 0 = none this time; the last one holds) and its headroom
   forecast (NaN = none this time; the last real one holds a while). Settles the shared tier. */
GEODE_API void            geode_thermal_push(int platform_status, float headroom);

/* Memory pressure (#854): Android's onTrimMemory level, forwarded as is. Native acts on TRIM_MEMORY_UI_HIDDEN (20)
   and TRIM_MEMORY_BACKGROUND (40) only and ignores every other level. Every renderer in the process then sheds its
   spare scenes on its own GL thread at its next frame; what it draws, the layer and the requested style stay. A surface
   that is paused draws no frame: it sheds them at its pause instead (geode_viz_trim_spares). Any thread. */
GEODE_API void            geode_memory_trim(int android_level);
/* ── Export planning and clip maths (ADR 0006: core/output, core/signal/timeline; ADR 0003 ports) ──────────
 * Pure functions and plain handles, any thread, no GL. The Kotlin exporters (VideoExporter, LoopRender,
 * LoopExtend, StudioExporter, the Media3 clip effects, AudioTranscoder) call these instead of carrying the
 * maths themselves; the classes behind them are output/FrameClock, LoopPlan, EncodePlan, Chapters,
 * signal/timeline/Keyframes, SpeedRamp, viz/ClipGrade, util/CubeLut, audio/dsp/PcmConvert and PcmTiming. */

/* Frame clock (output/FrameClock): each value from the frame index alone, so nothing accumulates. */
GEODE_API int64_t geode_export_total_frames(int64_t duration_us, int fps);            /* >= 1 */
GEODE_API int64_t geode_export_frame_pts_ns(int64_t frame, int fps);                  /* floor(n*1e9/fps) */
GEODE_API int64_t geode_export_frame_time_ms(int64_t frame, int fps);                 /* floor(n*1000/fps) */

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
GEODE_API int     geode_export_clamp_fps(int requested_fps);     /* 24..60 */

/* A recorded take's limits (output/RecordPlan; Settings › Export › Record). A maximum length is in seconds,
 * 0 = no limit. The offered lengths are index 0..count-1; a stored one is snapped onto them. */
GEODE_API int     geode_record_max_length_count(void);
GEODE_API int     geode_record_max_length_at(int index);
GEODE_API int     geode_record_snap_max_length(int requested_sec);
GEODE_API int     geode_record_limit_reached(int64_t recorded_ms, int max_sec);       /* 1 = stop the take */
GEODE_API int64_t geode_record_clamp_length_ms(int64_t length_ms, int max_sec);
/* The bytes length_sec of video at video_bps takes with its AAC; out_lowered (may be null) is 1 when the
 * render lowers the bit rate to stay under the muxer's 3.8 GB target. */
GEODE_API int64_t geode_record_estimate_bytes(int video_bps, int length_sec, int* out_lowered);

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
/* The sink's playback position in media time (AudioTrack.getTimestamp, latency included), read at now_ns. */
GEODE_API void           geode_playout_position(geode_playout*, int64_t sink_us, int64_t now_ns);
GEODE_API void           geode_playout_playing(geode_playout*, int playing, int64_t now_ns);   /* 1 play, 0 pause */
GEODE_API void           geode_playout_flush(geode_playout*);           /* a seek: every anchor and position goes */
GEODE_API void           geode_playout_speed(geode_playout*, float speed);   /* media per wall time; > 0 */
/* The user's A/V offset, clamped to +-1000 ms; positive shows the picture later. */
GEODE_API void           geode_playout_set_offset_ms(geode_playout*, int ms);
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
/* The player's ring, sized so a reader at the speaker is not overrun at 192 kHz: capacity and runway. */
GEODE_API int            geode_playout_ring_frames(void);
GEODE_API int            geode_playout_ring_runway(void);
/* The PCM ingest ring (audio/ingest/PcmRing): one lock-free ring per audio source, written by one thread
 * at a time (the player's tap, a capture thread) and read by any number of cursors, which never change it.
 * Every write carries its sample rate and source channel count, so a reader gets each frame with the rate
 * it was written at. Lifetime: the handle and every cursor (a renderer's feed included) share the ring, so
 * it is freed when the last of them goes; destroy the handle only once no thread writes through it. Every
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
GEODE_API void              geode_pcm_ring_destroy(geode_pcm_ring*);
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
/* A second source layered over `ring` (the mic over the player's tap; audio/ingest/LayerMix): every renderer
 * fed `ring` hears the two mixed while `ring` gains frames, and `layer` alone once it has not for 0.3 s.
 * `layer` keeps its own writer and rate; begin an epoch on it when its source starts and when it stops.
 * Other readers of `ring` (a cursor, Cast's sound) hear `ring` only. NULL removes the layer. Any thread. */
GEODE_API void              geode_pcm_ring_set_layer(geode_pcm_ring* ring, const geode_pcm_ring* layer);
/* A new stream (track change, seek, source switch): every cursor restarts at its next take. Any thread. */
GEODE_API void              geode_pcm_ring_begin_epoch(geode_pcm_ring*);
GEODE_API int64_t           geode_pcm_ring_written(const geode_pcm_ring*);   /* frames since the last epoch began */
GEODE_API int64_t           geode_pcm_ring_head(const geode_pcm_ring*);      /* frames ever written, across epochs */
/* The playout clock of the sink that plays this ring (the player's), or NULL for none: a renderer fed this
 * handle then reads up to the sample the speaker plays (geode_playout_listen_limit) instead of the newest.
 * The clock must outlive every reader of the handle; the app keeps one for the process. Any thread. */
GEODE_API void              geode_pcm_ring_set_playout(geode_pcm_ring*, const geode_playout* clock);
/* geode_silence_heard_at over the ring's newest `frames` frames: heard_at_ms unchanged when the epoch has
 * not written that many, else now_ms when any of them is audible. */
GEODE_API long long         geode_pcm_ring_heard_at(const geode_pcm_ring*, int frames, long long now_ms,
                                                    long long heard_at_ms);
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
GEODE_API int              geode_audio_out_supports(int encoding, int channels, int sample_rate);   /* 1 or 0 */
/* Opens the stream for writes of `encoding`. buffer_bytes is the sink's buffer for this output (the FIFO
 * and the stream's own buffer share it; 0 = 250 ms); session_id the player's audio session (0 = allocate
 * one). NULL when the format is not supported or the device gave no stream that is exactly it. */
GEODE_API geode_audio_out* geode_audio_out_open(int sample_rate, int channels, int encoding, int buffer_bytes,
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

#ifdef __cplusplus
}
#endif
