#pragma once
#include <stddef.h>
#include <stdint.h>
#include <android/asset_manager.h>
#ifdef __cplusplus
extern "C" {
#endif
#define GEODE_API __attribute__((visibility("default")))

typedef struct geode_analysis geode_analysis;
typedef struct geode_drums    geode_drums;
typedef struct geode_viz      geode_viz;
typedef struct geode_dsp      geode_dsp;
typedef struct geode_player   geode_player;
typedef struct geode_tags     geode_tags;

#define GEODE_BAND_COUNT 64
#define GEODE_WAVEFORM_POINTS 128
#define GEODE_CHROMA_BINS 12
#define GEODE_FEATURE_FRAME_FLOATS (33 + GEODE_BAND_COUNT + GEODE_WAVEFORM_POINTS + GEODE_CHROMA_BINS)

/* Every member is a float so the whole frame crosses JNI as one float array in this order.
 * Units: levels 0..1, phases 0..1 of a cycle, bpm in beats per minute, flags 0 or 1. */
typedef struct GeodeFeatureFrame {
    float rms;
    float bass;
    float mid;
    float treble;
    float centroid;
    float flux;
    float onset;
    float beat;
    float beatStrength;
    float transient;
    float beatPhase;
    float pulseConfidence;
    float bpm;
    float tempoStability;
    float barPhase;
    float beatInBar;
    float downbeat;
    float downbeatConfidence;
    float macroEnergy;
    float kick;
    float snare;
    float hat;
    float novelty;
    float sectionBoundary;
    float buildup;
    float drop;
    float arrival;
    float harmonicity;
    float warmup;
    float stereoWidth;
    float stereoCorrelation;
    float stereoPan;
    float chromaConfidence;
    float bands[GEODE_BAND_COUNT];
    float waveform[GEODE_WAVEFORM_POINTS];
    float chroma[GEODE_CHROMA_BINS];
} GeodeFeatureFrame;

GEODE_API const char* geode_version(void);

/* hop_rate_hz sets every tempo-domain filter; the hop in samples for push/pull is sample_rate / hop_rate_hz,
 * clamped to fft_size. fft_size must be a power of two >= GEODE_WAVEFORM_POINTS (so the waveform decimation
 * step is never zero); NULL on any other value. */
GEODE_API geode_analysis* geode_analysis_create(int sample_rate, int fft_size, float hop_rate_hz);
GEODE_API void            geode_analysis_destroy(geode_analysis*);
GEODE_API void            geode_analysis_set_sample_rate(geode_analysis*, int sample_rate);
GEODE_API void            geode_analysis_set_tuning(geode_analysis*, float sensitivity, float refractory_ms,
                                                    float attack_seconds, float release_seconds);
GEODE_API void            geode_analysis_reset(geode_analysis*);
/* One window (fft_size mono samples, optional side channel) analysed now; waveform is block-averaged. */
GEODE_API void            geode_analysis_analyze(geode_analysis*, const float* mid, const float* side, size_t frames,
                                                 float dt_seconds, GeodeFeatureFrame* out);   /* frames >= fft_size */
/* Hop-locked path: push interleaved PCM, pull one frame per hop; waveform is decimated and the key accumulates. */
GEODE_API void            geode_analysis_push(geode_analysis*, const float* interleaved, size_t frames, int channels);
GEODE_API int             geode_analysis_pull(geode_analysis*, GeodeFeatureFrame* out);   /* 1 = new frame */
GEODE_API size_t          geode_analysis_key(geode_analysis*, char* out, size_t capacity); /* "" until decided */

/* Six floats per frame: beat, strength, transient, phase, confidence, energy. */
GEODE_API void geode_pulse_replay(const float* flux, size_t count, const float* rms, size_t rms_count, float hop_rate_hz,
                                  float sensitivity, float refractory_ms, float* out);

GEODE_API geode_drums* geode_drums_create(int band_count, float hop_rate_hz, int sample_rate);
GEODE_API void         geode_drums_destroy(geode_drums*);
GEODE_API void         geode_drums_step(geode_drums*, const float* bands, float* kick_snare_hat);
/* band_count this handle was created with, so a caller can size/validate `bands` before calling step; 0 if null. */
GEODE_API int          geode_drums_band_count(geode_drums*);

/* Native visualizer: one renderer per GL surface. Setters may be called from any thread and are latched
 * for the next frame; the calls under "GL thread" need the surface's context current. */
#define GEODE_LFO_SLOTS 3
#define GEODE_LFO_CONFIG_FLOATS 8   /* enabled, source, target, wave, rate_seconds, depth, polarity, curve */
#define GEODE_ADSR_SLOTS 2
#define GEODE_ADSR_CONFIG_FLOATS 10 /* enabled, attack, decay, sustain, release, amount, band, gate_threshold,
                                       sustain_track, retrigger; followed by the target ordinals */

GEODE_API geode_viz*  geode_viz_create(AAssetManager* assets, const char* cache_dir);
GEODE_API void        geode_viz_destroy(geode_viz*);
GEODE_API int         geode_viz_param_count(void);
GEODE_API const char* geode_viz_param_name(int index);   /* NULL past the end */
/* Every scene parameter in geode_viz_param_name order; ints and flags travel as floats. */
GEODE_API void        geode_viz_set_params(geode_viz*, const float* values, int count);
GEODE_API int         geode_viz_set_param(geode_viz*, const char* name, float value);   /* 1 = known name */
GEODE_API void        geode_viz_set_features(geode_viz*, const GeodeFeatureFrame*);
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
/* The preset MilkDrop last compiled successfully, once; returns the full length, 0 when none is pending. */
GEODE_API size_t      geode_viz_take_milk_preset_loaded(geode_viz*, char* out, size_t capacity);
GEODE_API void        geode_viz_push_pcm(geode_viz*, const float* mono, int count);
GEODE_API void        geode_viz_set_custom_shader(geode_viz*, const char* scene_id, const char* fragment_source);
/* The user source the scene last compiled; returns its full length, 0 when it draws the built-in style. */
GEODE_API size_t      geode_viz_custom_shader(geode_viz*, const char* scene_id, char* out, size_t capacity);
GEODE_API void        geode_viz_set_lfo(geode_viz*, int slot, const float* config, int count);
GEODE_API void        geode_viz_set_adsr(geode_viz*, int slot, const float* config, int count);
GEODE_API void        geode_viz_set_thermal(geode_viz*, int platform_status, float headroom); /* status < 0 = unknown */
GEODE_API void        geode_viz_set_paced_fps(geode_viz*, float fps);
GEODE_API void        geode_viz_set_offscreen(geode_viz*, int on);
GEODE_API int         geode_viz_knows(geode_viz*, const char* scene_id);
GEODE_API size_t      geode_viz_scene_ids(geode_viz*, char* out, size_t capacity);   /* newline-joined; returns the full length */
GEODE_API const char* geode_viz_last_error(geode_viz*);   /* "" when clean */
/* Full-frame RGBA8 overlay drawn last, premultiplied alpha, over the finished composite (cover art,
 * text, lyrics, logo, background layers). `pixels` is Android's Bitmap.getPixels ARGB_8888 layout
 * (0xAARRGGBB per int), row-major, width*height entries; NULL or a 0x0 size clears it. */
GEODE_API void        geode_viz_set_overlay_rgba(geode_viz*, const uint32_t* pixels, int width, int height);
/* Full-frame RGBA8 underlay blended UNDER/INTO the scene, before the composite's own post-FX:
 * blend 0 = screen (replaces the scene where it is black), 1 = multiply, 2 = add; amount 0..1 is how
 * much of that blend result replaces the plain scene colour. Same pixel layout as the overlay above;
 * NULL or a 0x0 size clears it. */
GEODE_API void        geode_viz_set_underlay_rgba(geode_viz*, const uint32_t* pixels, int width, int height, int blend, float amount);
/* GL thread. */
GEODE_API void        geode_viz_surface_created(geode_viz*);
GEODE_API void        geode_viz_surface_changed(geode_viz*, int width, int height);
GEODE_API int         geode_viz_set_scene(geode_viz*, const char* scene_id);   /* 0 = native cannot draw it */
GEODE_API void        geode_viz_warm_transition(geode_viz*, const char* id);
/* Forgets the drawn scene so the next frame cuts instead of transitioning from it. */
GEODE_API void        geode_viz_cut(geode_viz*);
GEODE_API void        geode_viz_render(geode_viz*, double time_seconds, uint32_t target_fbo);
GEODE_API void        geode_viz_release_scenes(geode_viz*);

/* Playback DSP: gain -> 10-band equalizer -> crossfeed -> lookahead limiter on float interleaved PCM.
 * Setters may be called from any thread; geode_dsp_process runs on the audio thread and never allocates,
 * locks or logs. */
#define GEODE_DSP_BANDS 10
#define GEODE_DSP_MIN_MILLIBELS (-1500)
#define GEODE_DSP_MAX_MILLIBELS 1500

GEODE_API geode_dsp* geode_dsp_create(int sample_rate, int channels);   /* channels 1 or 2 */
GEODE_API void       geode_dsp_destroy(geode_dsp*);
GEODE_API int        geode_dsp_band_count(void);
GEODE_API float      geode_dsp_band_center_hz(int band);
GEODE_API void       geode_dsp_set_enabled(geode_dsp*, int enabled);          /* the equalizer and bass boost */
GEODE_API void       geode_dsp_set_band(geode_dsp*, int band, int millibels);
GEODE_API int        geode_dsp_band(geode_dsp*, int band);
GEODE_API void       geode_dsp_set_bass_boost(geode_dsp*, int permille);      /* 0..1000 */
GEODE_API void       geode_dsp_set_loudness_mb(geode_dsp*, int millibels);    /* 0..1000 makeup gain */
GEODE_API void       geode_dsp_set_gain_db(geode_dsp*, float db);             /* ReplayGain + preamp */
GEODE_API void       geode_dsp_set_crossfeed(geode_dsp*, int enabled);
GEODE_API void       geode_dsp_set_limiter(geode_dsp*, int enabled);
GEODE_API void       geode_dsp_reset(geode_dsp*);                              /* clears filter state after a seek or flush */
GEODE_API void       geode_dsp_process(geode_dsp*, float* interleaved, size_t frames);   /* in place, RT-safe */
/* Rebuilds the chain's filters and lookahead buffers for a new rate; allocates, so it is not RT-safe and
 * must only be called on a chain not yet installed via geode_player_set_dsp. The owner otherwise detects a
 * rate mismatch (e.g. against geode_player_output_sample_rate) with geode_dsp_sample_rate and hands over a
 * freshly built geode_dsp instead of mutating one already in use. */
GEODE_API void       geode_dsp_set_sample_rate(geode_dsp*, int sample_rate);
GEODE_API int        geode_dsp_sample_rate(geode_dsp*);   /* 0 for a null chain */

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
 * slot, leaves that field unchanged; "" clears it). 1 = saved, 0 = failed (see geode_tags_last_error). */
GEODE_API int         geode_tags_write(int fd, const char* const* texts, int year, int track);

typedef enum GeodeTagsError {
    GEODE_TAGS_OK = 0,
    GEODE_TAGS_ERR_OPEN,
    GEODE_TAGS_ERR_READ_ONLY,
    GEODE_TAGS_ERR_UNSUPPORTED,
    GEODE_TAGS_ERR_SAVE
} GeodeTagsError;
/* Why the most recent geode_tags_write on this thread returned 0; GEODE_TAGS_OK otherwise (the tags API is
 * called from one thread at a time, so this is tracked per-thread rather than per-call). */
GEODE_API int         geode_tags_last_error(void);

/* Native player: AMediaCodec decode -> resampler -> mixer (gapless join, crossfade) -> Oboe. Every call is
 * asynchronous and may come from any thread; a file descriptor belongs to the player from the call on.
 * token names the track to the caller and comes back from geode_player_current_token. */
typedef enum GeodePlayerState {
    GEODE_PLAYER_IDLE = 0,
    GEODE_PLAYER_BUFFERING,
    GEODE_PLAYER_READY,
    GEODE_PLAYER_ENDED,
    GEODE_PLAYER_ERROR
} GeodePlayerState;
#define GEODE_CROSSFADE_LINEAR 0
#define GEODE_CROSSFADE_EQUAL_POWER 1
#define GEODE_CROSSFADE_SMOOTH 2

GEODE_API geode_player* geode_player_create(void);
GEODE_API void          geode_player_destroy(geode_player*);
GEODE_API void          geode_player_open(geode_player*, int fd, int64_t offset, int64_t length, int64_t token); /* length <= 0 = to the end */
GEODE_API void          geode_player_set_next(geode_player*, int fd, int64_t offset, int64_t length, int64_t token); /* fd < 0 = none */
GEODE_API void          geode_player_play(geode_player*);
GEODE_API void          geode_player_pause(geode_player*);
GEODE_API void          geode_player_stop(geode_player*);
GEODE_API void          geode_player_seek(geode_player*, int64_t position_us);
GEODE_API void          geode_player_set_crossfade(geode_player*, int duration_ms, int curve);   /* 0 ms = gapless join */
/* A chain built for geode_player_output_sample_rate and 2 channels; NULL bypasses. Ownership of the chain
 * passes to the player: it retires the previous chain and calls geode_dsp_destroy on it itself once the
 * audio thread has moved past it (or at player destruction). The caller must not destroy a chain it has
 * installed here; a chain that was never installed remains the caller's to destroy. Never blocks. */
GEODE_API void          geode_player_set_dsp(geode_player*, geode_dsp*);
GEODE_API void          geode_player_set_volume(geode_player*, float volume);
GEODE_API int           geode_player_state(geode_player*);
GEODE_API int           geode_player_play_when_ready(geode_player*);
GEODE_API int64_t       geode_player_position_us(geode_player*);
GEODE_API int64_t       geode_player_duration_us(geode_player*);      /* 0 = unknown */
GEODE_API int64_t       geode_player_current_token(geode_player*);    /* -1 = nothing loaded */
GEODE_API int           geode_player_output_sample_rate(geode_player*); /* 0 until the stream opened */
GEODE_API size_t        geode_player_last_error(geode_player*, char* out, size_t capacity);   /* returns the full length */
/* The stereo mix as sent to the device, for analysis; one reader thread. Returns frames copied. */
GEODE_API size_t        geode_player_read_tap(geode_player*, float* stereo, size_t frames);

#ifdef __cplusplus
}
#endif
