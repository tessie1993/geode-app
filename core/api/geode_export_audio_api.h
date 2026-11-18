#ifndef GEODE_EXPORT_AUDIO_API_H
#define GEODE_EXPORT_AUDIO_API_H

#include "api/geode_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An export's audio work that runs the platform codecs (AMediaExtractor, AMediaCodec): Android only. The rules
   are core/output's, host-tested with fake codecs; these calls only open the codecs and wire them. Blocking, on
   the caller's thread. */

/* What a finished render's loudness says for a platform target (output/LoudnessScan, output/LoudnessAdvice):
   sentence is a loudness::Sentence (-1: nothing measured), the rest the numbers its wording reads. */
typedef struct geode_loudness_reading {
    int    sentence;
    double lufs;
    double true_peak_dbtp;
    double gain_db;
    double gain_for_level_db;
    double resulting_lufs;
    double resulting_true_peak_dbtp;
    double ceiling_dbtp;
    double quiet_lu;
    int    peak_warning;
} geode_loudness_reading;

/* Decodes the file at fd (owned from here on and closed; offset and length its AssetFileDescriptor's span,
   length < 0: none, the whole descriptor) whole and measures it for target (0 Leave as-is, 1 YouTube, 2 Shorts
   and TikTok). out->sentence is -1 when the file has no audio the platform decodes or it could not be measured. */
GEODE_API void geode_export_loudness_measure(int fd, int64_t offset, int64_t length, int target,
                                             geode_loudness_reading* out);

/* An export's soundtrack encoded to AAC-LC (output/AacTranscode): the track at fd (offset and length its
   AssetFileDescriptor's span, length < 0: the whole descriptor) from start_ms, for max_duration_ms (0: to the end),
   at output_rate_hz (0: the AAC rate for the track's own). Each access unit is appended to out_fd and listed in
   the sample table. Both descriptors are owned from here on and closed, also when this returns null (only for
   no memory). The shell runs it, polls it between runs and destroys it; a destroy before the end is the cancel. */
typedef struct geode_aac_job geode_aac_job;

enum { GEODE_AAC_JOB_RUNNING = 1, GEODE_AAC_JOB_DONE = 2, GEODE_AAC_JOB_FAILED = 3 };
/* Why a job failed (output/AacTranscode's Failure): 1 read but no audio track, 2 a codec, 3 a stall, 4 no format,
   5 the scratch file could not be written, 6 the source could not be opened or read. */
enum { GEODE_AAC_TRACK_RATE = 0, GEODE_AAC_TRACK_CHANNELS = 1, GEODE_AAC_TRACK_BIT_RATE = 2,
       GEODE_AAC_TRACK_DURATION_US = 3, GEODE_AAC_TRACK_BYTES = 4, GEODE_AAC_TRACK_SAMPLES = 5,
       GEODE_AAC_TRACK_VALUES = 6 };
/* Each sample-table row: offset in the file, size, pts (us), flags. */
enum { GEODE_AAC_SAMPLE_VALUES = 4 };

GEODE_API geode_aac_job* geode_aac_job_create(int fd, int64_t offset, int64_t length, int out_fd, int64_t start_ms,
                                              int64_t max_duration_ms, int output_rate_hz);
GEODE_API void geode_aac_job_destroy(geode_aac_job* job);
/* Steps the job for about slice_ms; 1 while it runs on, 0 once it is done or failed. */
GEODE_API int geode_aac_job_run(geode_aac_job* job, int slice_ms);
GEODE_API int geode_aac_job_state(const geode_aac_job* job);
GEODE_API int geode_aac_job_failure(const geode_aac_job* job);
/* Valid until the next call on the job; never null. */
GEODE_API const char* geode_aac_job_detail(const geode_aac_job* job);
GEODE_API float geode_aac_job_progress(const geode_aac_job* job);
/* Once done: GEODE_AAC_TRACK_VALUES values into out, by the indices above; zeros before. */
GEODE_API void geode_aac_job_track(const geode_aac_job* job, int64_t* out);
/* Rows first..first+count of the sample table into out (GEODE_AAC_SAMPLE_VALUES each); the rows written. */
GEODE_API int geode_aac_job_samples(const geode_aac_job* job, int first, int count, int64_t* out);
/* The track's codec-specific data (csd-0) once done, *size its bytes; null when there is none. */
GEODE_API const uint8_t* geode_aac_job_csd(const geode_aac_job* job, int* size);

#ifdef __cplusplus
}
#endif

#endif
