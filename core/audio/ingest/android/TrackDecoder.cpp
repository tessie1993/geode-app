#include "audio/ingest/android/TrackDecoder.hpp"

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <unistd.h>

#include <stdexcept>

// NDK media calls (developer.android.com/ndk/reference/group/media), every one available since API level 21.
// Ownership: AMediaExtractor_getTrackFormat and AMediaCodec_getOutputFormat return a format the caller frees;
// a string AMediaFormat_getString hands out lives as long as its format.
namespace geode::audio::ingest {

namespace {

// How long each dequeue waits for the codec, as the decode has always waited.
constexpr int64_t kTimeoutUs = 10'000;

struct FormatDelete {
    void operator()(AMediaFormat* f) const {
        if (f) AMediaFormat_delete(f);
    }
};
using Format = std::unique_ptr<AMediaFormat, FormatDelete>;

class MediaDecodePort final : public DecodePort {
public:
    explicit MediaDecodePort(int fd) : fd_(fd), extractor_(AMediaExtractor_new()) {}

    ~MediaDecodePort() override {
        if (codec_) {
            AMediaCodec_stop(codec_);
            AMediaCodec_delete(codec_);
        }
        if (extractor_) AMediaExtractor_delete(extractor_);
        if (fd_ >= 0) ::close(fd_);
    }

    MediaDecodePort(const MediaDecodePort&) = delete;
    MediaDecodePort& operator=(const MediaDecodePort&) = delete;

    // The span's first audio track, sought to startMs and a decoder started on it; false when there is none.
    bool open(const TrackSpan& span, int64_t startMs) {
        if (!extractor_) return false;
        const ExtractorSpan bytes = extractorSpan(span);
        if (AMediaExtractor_setDataSourceFd(extractor_, fd_, bytes.offset, bytes.length) != AMEDIA_OK) return false;
        const size_t tracks = AMediaExtractor_getTrackCount(extractor_);
        for (size_t i = 0; i < tracks; ++i) {
            Format format(AMediaExtractor_getTrackFormat(extractor_, i));
            const char* mime = nullptr;
            if (!format || !AMediaFormat_getString(format.get(), AMEDIAFORMAT_KEY_MIME, &mime) || !isAudioMime(mime)) {
                continue;
            }
            if (AMediaExtractor_selectTrack(extractor_, i) != AMEDIA_OK) return false;
            const int64_t seekUs = decodeSeekUs(startMs);
            if (seekUs >= 0) AMediaExtractor_seekTo(extractor_, seekUs, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
            codec_ = AMediaCodec_createDecoderByType(mime);
            if (!codec_) return false;
            if (AMediaCodec_configure(codec_, format.get(), nullptr, nullptr, 0) != AMEDIA_OK) return false;
            return AMediaCodec_start(codec_) == AMEDIA_OK;
        }
        return false;
    }

    bool feedInput() override {
        const ssize_t index = AMediaCodec_dequeueInputBuffer(codec_, kTimeoutUs);
        if (index < 0) return false;
        const auto slot = static_cast<size_t>(index);
        size_t capacity = 0;
        uint8_t* buffer = AMediaCodec_getInputBuffer(codec_, slot, &capacity);
        if (!buffer) throw std::runtime_error("decoder input buffer null (codec error state)");
        const ssize_t size = AMediaExtractor_readSampleData(extractor_, buffer, capacity);
        if (size < 0) {
            AMediaCodec_queueInputBuffer(codec_, slot, 0, 0, 0, static_cast<uint32_t>(AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM));
            return true;
        }
        const int64_t timeUs = AMediaExtractor_getSampleTime(extractor_);
        AMediaCodec_queueInputBuffer(codec_, slot, 0, static_cast<size_t>(size), static_cast<uint64_t>(timeUs), 0);
        AMediaExtractor_advance(extractor_);
        return false;
    }

    bool takeOutput(DecodedChunk& out) override {
        AMediaCodecBufferInfo info{};
        const ssize_t index = AMediaCodec_dequeueOutputBuffer(codec_, &info, kTimeoutUs);
        if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) layoutKnown_ = false;
        if (index < 0) return false;
        held_ = static_cast<size_t>(index);
        out.ptsUs = info.presentationTimeUs;
        out.endOfStream = (info.flags & static_cast<uint32_t>(AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)) != 0;
        if (info.size > 0) {
            if (!layoutKnown_) readLayout();
            size_t capacity = 0;
            // The buffer's samples start at the pointer: up to API level 35 the info's offset is not valid and is
            // ignored, its size is (NdkMediaCodec, AMediaCodec_dequeueOutputBuffer).
            const uint8_t* data = AMediaCodec_getOutputBuffer(codec_, held_, &capacity);
            if (!data) throw std::runtime_error("decoder output buffer null (codec error state)");
            out.data = data;
            out.bytes = static_cast<size_t>(info.size);
            out.isFloat = isFloat_;
            out.channels = channels_;
            out.sampleRate = sampleRate_;
        }
        return true;
    }

    void releaseOutput() override { AMediaCodec_releaseOutputBuffer(codec_, held_, false); }

private:
    // The output's sample layout, read once and again after each format change rather than per buffer: each
    // read allocates a format.
    void readLayout() {
        Format format(AMediaCodec_getOutputFormat(codec_));
        if (!format) throw std::runtime_error("decoder output format null");
        int32_t encoding = 0;
        const bool hasEncoding = AMediaFormat_getInt32(format.get(), AMEDIAFORMAT_KEY_PCM_ENCODING, &encoding);
        int32_t channels = 0;
        int32_t rate = 0;
        if (!AMediaFormat_getInt32(format.get(), AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels) ||
            !AMediaFormat_getInt32(format.get(), AMEDIAFORMAT_KEY_SAMPLE_RATE, &rate)) {
            throw std::runtime_error("decoder output format without channels or rate");
        }
        isFloat_ = isFloatPcm(hasEncoding, encoding);
        channels_ = channels;
        sampleRate_ = rate;
        layoutKnown_ = true;
    }

    int fd_;
    AMediaExtractor* extractor_;
    AMediaCodec* codec_ = nullptr;
    size_t held_ = 0;
    bool layoutKnown_ = false;
    bool isFloat_ = false;
    int channels_ = 0;
    int sampleRate_ = 0;
};

}  // namespace

std::unique_ptr<DecodePort> openPlatformDecoder(const TrackSpan& span, int64_t startMs) {
    // Its own descriptor, closed with the port, whether or not the extractor keeps one of its own.
    const int fd = ::dup(span.fd);
    if (fd < 0) return nullptr;
    auto port = std::make_unique<MediaDecodePort>(fd);
    if (!port->open(span, startMs)) return nullptr;
    return port;
}

}  // namespace geode::audio::ingest
