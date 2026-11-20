#pragma once

// A finished AAC job's track (core/api/geode_export_audio_api.h) read out through that header's own calls, as the
// muxers copy it: the access units and the format. Shared by the video export and the loop extend, which each take
// the track from the job the shell ran and so never see its table or its format keys.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "api/geode_export_audio_api.h"
#include "output/AacTranscode.hpp"
#include "output/VideoExport.hpp"

namespace geode::api {

struct AacTrack {
    std::vector<output::videoexport::AudioSample> samples;
    std::vector<output::sink::FormatEntry> format;
    int64_t durationUs = 0;
    int rateHz = 0;
    int channels = 0;
};

// False when the job is not done (its track reads as zeros) or its table cannot be read whole.
inline bool readAacTrack(const geode_aac_job* job, AacTrack* out) {
    int64_t track[GEODE_AAC_TRACK_VALUES] = {};
    geode_aac_job_track(job, track);
    if (track[GEODE_AAC_TRACK_RATE] <= 0 || track[GEODE_AAC_TRACK_CHANNELS] <= 0) return false;
    const int count = static_cast<int>(track[GEODE_AAC_TRACK_SAMPLES]);
    std::vector<int64_t> rows(static_cast<size_t>(count) * GEODE_AAC_SAMPLE_VALUES);
    if (count > 0 && geode_aac_job_samples(job, 0, count, rows.data()) != count) return false;
    out->samples.clear();
    out->samples.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const int64_t* row = rows.data() + static_cast<ptrdiff_t>(i) * GEODE_AAC_SAMPLE_VALUES;
        out->samples.push_back({row[0], static_cast<int32_t>(row[1]), row[2], static_cast<uint32_t>(row[3])});
    }
    int csdBytes = 0;
    const uint8_t* csd = geode_aac_job_csd(job, &csdBytes);
    out->rateHz = static_cast<int>(track[GEODE_AAC_TRACK_RATE]);
    out->channels = static_cast<int>(track[GEODE_AAC_TRACK_CHANNELS]);
    out->durationUs = track[GEODE_AAC_TRACK_DURATION_US];
    out->format = output::aac::formatEntries(out->rateHz, out->channels, static_cast<int>(track[GEODE_AAC_TRACK_BIT_RATE]),
                                             csd, csdBytes > 0 ? static_cast<size_t>(csdBytes) : 0);
    return true;
}

}  // namespace geode::api
