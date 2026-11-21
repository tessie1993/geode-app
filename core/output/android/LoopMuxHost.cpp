#include "output/android/LoopMuxHost.hpp"

#include <media/NdkMediaError.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>

#include "output/android/MediaCodecSink.hpp"

// Only moves calls across: every decision is LoopMux's (host-tested). Ownership, from the NDK headers: a format from
// AMediaExtractor_getTrackFormat is the caller's to delete; a string or buffer read out of a format lives as long as
// that format does, so it is copied out before the format goes.

namespace geode::output::loopmux {

namespace {

constexpr char kVideoPrefix[] = "video/";

std::vector<uint8_t> bufferOf(AMediaFormat* format, const char* key) {
    void* data = nullptr;
    size_t size = 0;
    if (!AMediaFormat_getBuffer(format, key, &data, &size) || !data) return {};
    const auto* bytes = static_cast<const uint8_t*>(data);
    return std::vector<uint8_t>(bytes, bytes + size);
}

// One rendered loop: its first video track, selected, read sample by sample.
class ExtractorLoop final : public LoopReader {
public:
    ExtractorLoop() = default;
    ~ExtractorLoop() override {
        if (extractor_) AMediaExtractor_delete(extractor_);
    }
    ExtractorLoop(const ExtractorLoop&) = delete;
    ExtractorLoop& operator=(const ExtractorLoop&) = delete;

    // Null when the file is open on its video track, else why not.
    const char* open(const std::string& path) {
        extractor_ = AMediaExtractor_new();
        if (!extractor_) return "out of memory";
        if (AMediaExtractor_setDataSource(extractor_, path.c_str()) != AMEDIA_OK) {
            return "The rendered loop could not be opened.";
        }
        const size_t tracks = AMediaExtractor_getTrackCount(extractor_);
        bool found = false;
        for (size_t i = 0; i < tracks && !found; ++i) {
            AMediaFormat* format = AMediaExtractor_getTrackFormat(extractor_, i);
            if (!format) continue;
            const char* mime = nullptr;
            if (AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime) && mime &&
                std::strncmp(mime, kVideoPrefix, sizeof(kVideoPrefix) - 1) == 0) {
                found = true;
                track_ = i;
                shape_.mime = mime;
                int32_t value = 0;
                if (AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &value)) shape_.width = value;
                value = 0;
                if (AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &value)) shape_.height = value;
                value = 0;
                if (AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, &value)) declared_ = value;
                shape_.csd0 = bufferOf(format, AMEDIAFORMAT_KEY_CSD_0);
                shape_.csd1 = bufferOf(format, AMEDIAFORMAT_KEY_CSD_1);
            }
            AMediaFormat_delete(format);
        }
        if (!found) return "The rendered loop has no video track.";
        if (AMediaExtractor_selectTrack(extractor_, track_) != AMEDIA_OK) return "The rendered loop's video could not be read.";
        return nullptr;
    }

    const Signature& signature() const override { return shape_; }
    int declaredBytes() const override { return declared_; }

    std::unique_ptr<sink::TrackFormat> trackFormat() override {
        return sink::trackFormatOf(AMediaExtractor_getTrackFormat(extractor_, track_));
    }

    bool scan(std::vector<int64_t>* timesUs, std::vector<int>* flags) override {
        while (AMediaExtractor_getSampleTime(extractor_) >= 0) {
            timesUs->push_back(AMediaExtractor_getSampleTime(extractor_));
            flags->push_back(static_cast<int>(AMediaExtractor_getSampleFlags(extractor_)));
            AMediaExtractor_advance(extractor_);
        }
        return true;
    }

    bool rewind() override {
        return AMediaExtractor_seekTo(extractor_, 0, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC) == AMEDIA_OK;
    }

    int next(uint8_t* out, size_t capacity) override {
        const ssize_t size = AMediaExtractor_readSampleData(extractor_, out, capacity);
        if (size < 0) return -1;
        AMediaExtractor_advance(extractor_);
        return static_cast<int>(size);
    }

private:
    AMediaExtractor* extractor_ = nullptr;
    size_t track_ = 0;
    int declared_ = 0;
    Signature shape_;
};

class AndroidHost final : public Host {
public:
    explicit AndroidHost(const Setup& setup) : setup_(setup) {}

    std::unique_ptr<sink::Codecs> codecs() override { return sink::mediaCodecs(setup_.outputFd); }

    std::unique_ptr<LoopReader> openLoop(int index, const char** why) override {
        if (index < 0 || static_cast<size_t>(index) >= setup_.loopPaths.size()) {
            *why = "The rendered loop is missing.";
            return nullptr;
        }
        auto loop = std::make_unique<ExtractorLoop>();
        if (const char* problem = loop->open(setup_.loopPaths[static_cast<size_t>(index)])) {
            *why = problem;
            return nullptr;
        }
        return loop;
    }

    // Every byte the sample names, or as many as the scratch holds.
    int readAudio(int clip, const videoexport::AudioSample& sample, uint8_t* out) override {
        if (clip < 0 || static_cast<size_t>(clip) >= setup_.clipFds.size()) return -1;
        const int fd = setup_.clipFds[static_cast<size_t>(clip)];
        if (fd < 0) return -1;
        int read = 0;
        while (read < sample.size) {
            const ssize_t n =
                ::pread(fd, out + read, static_cast<size_t>(sample.size - read), static_cast<off_t>(sample.offset + read));
            if (n < 0 && errno == EINTR) continue;
            if (n < 0) return -1;
            if (n == 0) break;
            read += static_cast<int>(n);
        }
        return read;
    }

private:
    const Setup& setup_;
};

}  // namespace

std::unique_ptr<Host> androidHost(const Setup& setup) { return std::make_unique<AndroidHost>(setup); }

}  // namespace geode::output::loopmux
