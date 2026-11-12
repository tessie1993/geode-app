// The C ABI over an export's platform audio work (geode_export_audio_api.h): the codecs opened through
// audio/ingest/android, the rules in core/output. No exception leaves it.
#include "api/geode_export_audio_api.h"

#include <unistd.h>

#include <exception>

#include "audio/ingest/TrackPump.hpp"
#include "audio/ingest/android/TrackDecoder.hpp"
#include "output/LoudnessAdvice.hpp"
#include "output/LoudnessScan.hpp"

namespace loudness = geode::output::loudness;

namespace {

void fill(const loudness::Reading& r, geode_loudness_reading* out) {
    out->sentence = r.sentence;
    out->lufs = r.lufs;
    out->true_peak_dbtp = r.truePeakDbtp;
    out->gain_db = r.gainDb;
    out->gain_for_level_db = r.gainForLevelDb;
    out->resulting_lufs = r.resultingLufs;
    out->resulting_true_peak_dbtp = r.resultingTruePeakDbtp;
    out->ceiling_dbtp = r.ceilingDbtp;
    out->quiet_lu = r.quietLu;
    out->peak_warning = r.peakWarning ? 1 : 0;
}

}  // namespace

extern "C" {

void geode_export_loudness_measure(int fd, int64_t offset, int64_t length, int target, geode_loudness_reading* out) {
    loudness::Reading reading;
    try {
        // The decoder reads its own duplicate of fd, so this one is done with once it is open.
        auto port = geode::audio::ingest::openPlatformDecoder({fd, offset, length}, 0);
        if (fd >= 0) ::close(fd);
        fd = -1;
        if (port) {
            const loudness::Scan scan = loudness::scan(*port);
            if (scan.end == loudness::ScanEnd::kMeasured) {
                reading = loudness::readingOf(target, scan.report.hasIntegrated, scan.report.integratedLufs,
                                              scan.report.truePeakDbtp);
            }
        }
    } catch (const std::exception&) {
        reading = {};
    }
    if (fd >= 0) ::close(fd);
    if (out) fill(reading, out);
}

}
