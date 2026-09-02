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

/* hop_rate_hz sets every tempo-domain filter; the hop in samples for push/pull is sample_rate / hop_rate_hz. */
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
/* GL thread. */
GEODE_API void        geode_viz_surface_created(geode_viz*);
GEODE_API void        geode_viz_surface_changed(geode_viz*, int width, int height);
GEODE_API int         geode_viz_set_scene(geode_viz*, const char* scene_id);   /* 0 = native cannot draw it */
GEODE_API void        geode_viz_warm_transition(geode_viz*, const char* id);
/* Forgets the drawn scene so the next frame cuts instead of transitioning from it. */
GEODE_API void        geode_viz_cut(geode_viz*);
GEODE_API void        geode_viz_render(geode_viz*, double time_seconds, uint32_t target_fbo);
GEODE_API void        geode_viz_release_scenes(geode_viz*);

#ifdef __cplusplus
}
#endif
