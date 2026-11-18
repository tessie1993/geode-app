// The C ABI over an export's platform audio work (geode_export_audio_api.h): the codecs opened through
// audio/ingest/android and output/android, the rules in core/output. No exception leaves it.
#include "api/geode_export_audio_api.h"

#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "audio/ingest/TrackPump.hpp"
#include "audio/ingest/android/TrackDecoder.hpp"
#include "output/AacTranscode.hpp"
#include "output/LoudnessAdvice.hpp"
#include "output/LoudnessScan.hpp"
#include "output/android/MediaCodecSink.hpp"

namespace loudness = geode::output::loudness;
namespace aac = geode::output::aac;
namespace sink = geode::output::sink;
namespace ingest = geode::audio::ingest;

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

// The transcode's phone: AAC encoders through the NDK, the scratch file at fd (owned, closed with the host) written
// through a buffer, and a short sleep when nothing moved, the wait each codec call used to block for.
class FileHost final : public aac::Host {
public:
    explicit FileHost(int fd) noexcept : fd_(fd) {}
    ~FileHost() override {
        if (fd_ >= 0) ::close(fd_);
    }
    FileHost(const FileHost&) = delete;
    FileHost& operator=(const FileHost&) = delete;

    std::unique_ptr<sink::Encoder> createEncoder() override {
        return sink::mediaCodecs(-1)->createEncoder(sink::kMimeAac);
    }

    bool write(const uint8_t* data, size_t bytes) override {
        if (buffer_.size() + bytes > kBufferBytes && !flush()) return false;
        if (bytes >= kBufferBytes) return writeAll(data, bytes);
        buffer_.insert(buffer_.end(), data, data + bytes);
        return true;
    }

    bool finish() override { return flush(); }

    void idleWait() override { std::this_thread::sleep_for(std::chrono::milliseconds(kIdleMs)); }

private:
    static constexpr size_t kBufferBytes = size_t{64} << 10;
    static constexpr int kIdleMs = 10;

    bool flush() {
        const bool ok = writeAll(buffer_.data(), buffer_.size());
        buffer_.clear();
        return ok;
    }

    bool writeAll(const uint8_t* data, size_t bytes) {
        while (bytes > 0) {
            const ssize_t n = ::write(fd_, data, bytes);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return false;
            data += n;
            bytes -= static_cast<size_t>(n);
        }
        return true;
    }

    int fd_;
    std::vector<uint8_t> buffer_;
};

}  // namespace

// The host first: the transcode's codecs are released before the scratch file is closed.
struct geode_aac_job {
    explicit geode_aac_job(int outFd) noexcept : host(outFd) {}
    FileHost host;
    std::unique_ptr<aac::Transcode> transcode;
    // A failure outside the transcode's own steps (a throw, an output format this cannot read), else 0.
    int failure = 0;
    std::string detail;
    int64_t track[GEODE_AAC_TRACK_VALUES] = {};
    std::vector<uint8_t> csd;
};

namespace {

void failJob(geode_aac_job* job, aac::Failure failure, const char* detail) {
    job->failure = static_cast<int>(failure);
    job->detail = detail ? detail : "";
}

// The finished track's shape, read once from the encoder's output format.
void describe(geode_aac_job* job) {
    const aac::Transcode& t = *job->transcode;
    int rate = 0;
    int channels = 0;
    if (!t.format() || !sink::audioTrackInfo(*t.format(), &rate, &channels, &job->csd)) {
        failJob(job, aac::Failure::NoFormat, "AAC encoder format names no rate or channels");
        return;
    }
    job->track[GEODE_AAC_TRACK_RATE] = rate;
    job->track[GEODE_AAC_TRACK_CHANNELS] = channels;
    job->track[GEODE_AAC_TRACK_BIT_RATE] = aac::kBitRate;
    job->track[GEODE_AAC_TRACK_DURATION_US] = t.durationUs();
    job->track[GEODE_AAC_TRACK_BYTES] = t.bytes();
    job->track[GEODE_AAC_TRACK_SAMPLES] = static_cast<int64_t>(t.samples().size());
}

bool done(const geode_aac_job* job) {
    return job && job->failure == 0 && job->transcode && job->transcode->state() == aac::State::Done;
}

}  // namespace

extern "C" {

geode_aac_job* geode_aac_job_create(int fd, int64_t offset, int64_t length, int out_fd, int64_t start_ms,
                                    int64_t max_duration_ms, int output_rate_hz) {
    auto* job = new (std::nothrow) geode_aac_job(out_fd);
    if (!job) {
        if (out_fd >= 0) ::close(out_fd);
        if (fd >= 0) ::close(fd);
        return nullptr;
    }
    try {
        const ingest::TrackSpan span{fd, offset, length};
        aac::Request request;
        request.startMs = start_ms;
        request.maxDurationMs = max_duration_ms;
        request.outputRateHz = output_rate_hz;
        std::unique_ptr<ingest::AiffReader> aiff;
        std::unique_ptr<ingest::DecodePort> port;
        bool unreadable = fd < 0;
        if (fd >= 0) {
            try {
                aiff = ingest::openAiff(span);
            } catch (const std::runtime_error&) {
                // Its duplicate of the descriptor failed: nothing of the source can be read.
                unreadable = true;
            }
        }
        // The decoder reads its own duplicate of fd.
        if (!unreadable && !aiff) port = ingest::openPlatformDecoderReporting(span, start_ms, &unreadable);
        if (aiff) {
            job->transcode = std::make_unique<aac::Transcode>(request, std::move(aiff), job->host);
        } else {
            job->transcode = std::make_unique<aac::Transcode>(request, std::move(port), job->host, unreadable);
        }
    } catch (const std::exception& e) {
        failJob(job, aac::Failure::Codec, e.what());
    }
    if (fd >= 0) ::close(fd);
    return job;
}

void geode_aac_job_destroy(geode_aac_job* job) { delete job; }

int geode_aac_job_run(geode_aac_job* job, int slice_ms) {
    if (!job || job->failure != 0 || !job->transcode) return 0;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(slice_ms, 1));
    try {
        while (job->transcode->step()) {
            if (std::chrono::steady_clock::now() >= until) return 1;
        }
        if (job->transcode->state() == aac::State::Done) describe(job);
    } catch (const std::exception& e) {
        failJob(job, aac::Failure::Codec, e.what());
    }
    return 0;
}

int geode_aac_job_state(const geode_aac_job* job) {
    if (!job || job->failure != 0 || !job->transcode) return GEODE_AAC_JOB_FAILED;
    switch (job->transcode->state()) {
        case aac::State::Running:
            return GEODE_AAC_JOB_RUNNING;
        case aac::State::Done:
            return GEODE_AAC_JOB_DONE;
        case aac::State::Failed:
            break;
    }
    return GEODE_AAC_JOB_FAILED;
}

int geode_aac_job_failure(const geode_aac_job* job) {
    if (!job) return static_cast<int>(aac::Failure::Codec);
    if (job->failure != 0) return job->failure;
    return job->transcode ? static_cast<int>(job->transcode->failure()) : static_cast<int>(aac::Failure::NoAudio);
}

const char* geode_aac_job_detail(const geode_aac_job* job) {
    if (!job) return "";
    if (job->failure != 0 || !job->transcode) return job->detail.c_str();
    return job->transcode->detail().c_str();
}

float geode_aac_job_progress(const geode_aac_job* job) {
    return job && job->transcode ? job->transcode->progress() : 0.0f;
}

void geode_aac_job_track(const geode_aac_job* job, int64_t* out) {
    if (!out) return;
    for (int i = 0; i < GEODE_AAC_TRACK_VALUES; ++i) out[i] = done(job) ? job->track[i] : 0;
}

int geode_aac_job_samples(const geode_aac_job* job, int first, int count, int64_t* out) {
    if (!done(job) || !out || first < 0 || count <= 0) return 0;
    const auto& samples = job->transcode->samples();
    if (static_cast<size_t>(first) >= samples.size()) return 0;
    const int n = static_cast<int>(std::min(samples.size() - static_cast<size_t>(first), static_cast<size_t>(count)));
    for (int i = 0; i < n; ++i) {
        const aac::Sample& s = samples[static_cast<size_t>(first + i)];
        int64_t* row = out + static_cast<ptrdiff_t>(i) * GEODE_AAC_SAMPLE_VALUES;
        row[0] = s.offset;
        row[1] = s.size;
        row[2] = s.ptsUs;
        row[3] = s.flags;
    }
    return n;
}

const uint8_t* geode_aac_job_csd(const geode_aac_job* job, int* size) {
    if (size) *size = 0;
    if (!done(job) || job->csd.empty()) return nullptr;
    if (size) *size = static_cast<int>(job->csd.size());
    return job->csd.data();
}


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
