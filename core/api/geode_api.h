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

#ifdef __cplusplus
}
#endif
