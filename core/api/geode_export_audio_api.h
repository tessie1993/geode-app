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

#ifdef __cplusplus
}
#endif

#endif
