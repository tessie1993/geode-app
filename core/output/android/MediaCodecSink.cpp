#include "output/android/MediaCodecSink.hpp"

#include <android/native_window.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaError.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>

#include <cstdint>

// Only moves calls across: every decision is EncodeSink's (host-tested). Every call here is in the NDK's
// media library at API 26 or lower (createInputSurface and signalEndOfInputStream are 26, the rest 21), under
// the app's minimum of 33. Ownership, from the NDK headers: a format from AMediaCodec_getOutputFormat is the
// caller's to delete; the window from AMediaCodec_createInputSurface is the caller's to ANativeWindow_release;
// AMediaMuxer_addTrack reads the format during the call; AMediaMuxer_new duplicates the fd.

namespace geode::output::sink {

namespace {

struct FormatDeleter {
    void operator()(AMediaFormat* format) const { AMediaFormat_delete(format); }
};
using FormatPtr = std::unique_ptr<AMediaFormat, FormatDeleter>;

class MediaTrackFormat final : public TrackFormat {
public:
    explicit MediaTrackFormat(AMediaFormat* format) : format_(format) {}
    const AMediaFormat* get() const { return format_.get(); }

private:
    FormatPtr format_;
};

class MediaEncoder final : public Encoder {
public:
    explicit MediaEncoder(AMediaCodec* codec) : codec_(codec) {}

    ~MediaEncoder() override {
        stop();
        AMediaCodec_delete(codec_);
        // The surface last: an EGL surface still on it holds its own reference.
        if (window_) ANativeWindow_release(window_);
    }

    MediaEncoder(const MediaEncoder&) = delete;
    MediaEncoder& operator=(const MediaEncoder&) = delete;

    bool configure(const VideoFormat& f) override {
        FormatPtr format(AMediaFormat_new());
        if (!format) return false;
        AMediaFormat_setString(format.get(), AMEDIAFORMAT_KEY_MIME, f.mime);
        AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_WIDTH, f.width);
        AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_HEIGHT, f.height);
        AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_COLOR_FORMAT, f.colorFormat);
        AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_BIT_RATE, f.bitRate);
        AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_FRAME_RATE, f.frameRate);
        AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, f.iFrameIntervalSec);
        return AMediaCodec_configure(codec_, format.get(), nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE) ==
               AMEDIA_OK;
    }

    bool configure(const AudioFormat& f) override {
        FormatPtr format(AMediaFormat_new());
        if (!format) return false;
        AMediaFormat_setString(format.get(), AMEDIAFORMAT_KEY_MIME, f.mime);
        AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_SAMPLE_RATE, f.sampleRateHz);
        AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_CHANNEL_COUNT, f.channels);
        AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_AAC_PROFILE, f.aacProfile);
        AMediaFormat_setInt32(format.get(), AMEDIAFORMAT_KEY_BIT_RATE, f.bitRate);
        return AMediaCodec_configure(codec_, format.get(), nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE) ==
               AMEDIA_OK;
    }

    bool useSurfaceInput() override {
        return AMediaCodec_createInputSurface(codec_, &window_) == AMEDIA_OK && window_ != nullptr;
    }

    void* inputWindow() const override { return window_; }

    bool start() override {
        running_ = AMediaCodec_start(codec_) == AMEDIA_OK;
        return running_;
    }

    void stop() override {
        if (running_) AMediaCodec_stop(codec_);
        running_ = false;
    }

    bool endSurfaceInput() override { return AMediaCodec_signalEndOfInputStream(codec_) == AMEDIA_OK; }

    int dequeueInput() override {
        const ssize_t index = AMediaCodec_dequeueInputBuffer(codec_, 0);
        if (index >= 0) return static_cast<int>(index);
        return index == AMEDIACODEC_INFO_TRY_AGAIN_LATER ? kNoInput : kInputError;
    }

    uint8_t* inputBuffer(int index, size_t* capacity) override {
        return AMediaCodec_getInputBuffer(codec_, static_cast<size_t>(index), capacity);
    }

    bool queueInput(int index, size_t bytes, int64_t ptsUs, uint32_t flags) override {
        return AMediaCodec_queueInputBuffer(codec_, static_cast<size_t>(index), 0, bytes, static_cast<uint64_t>(ptsUs),
                                            flags) == AMEDIA_OK;
    }

    Out dequeueOutput(int* index, BufferInfo* info) override {
        AMediaCodecBufferInfo got{};
        const ssize_t at = AMediaCodec_dequeueOutputBuffer(codec_, &got, 0);
        if (at >= 0) {
            *index = static_cast<int>(at);
            // The info's offset is dropped here: up to API level 35 it is not valid and the sample starts at the
            // buffer's pointer (NdkMediaCodec, AMediaCodec_dequeueOutputBuffer); later it is always 0.
            *info = {got.size, got.presentationTimeUs, got.flags};
            return Out::Sample;
        }
        if (at == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) return Out::FormatChanged;
        if (at == AMEDIACODEC_INFO_TRY_AGAIN_LATER || at == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) return Out::None;
        return Out::Error;
    }

    const uint8_t* outputBuffer(int index, size_t* capacity) override {
        return AMediaCodec_getOutputBuffer(codec_, static_cast<size_t>(index), capacity);
    }

    bool releaseOutput(int index) override {
        return AMediaCodec_releaseOutputBuffer(codec_, static_cast<size_t>(index), false) == AMEDIA_OK;
    }

    std::unique_ptr<TrackFormat> outputFormat() override {
        AMediaFormat* format = AMediaCodec_getOutputFormat(codec_);
        if (!format) return nullptr;
        return std::make_unique<MediaTrackFormat>(format);
    }

private:
    AMediaCodec* const codec_;
    ANativeWindow* window_ = nullptr;
    bool running_ = false;
};

class MediaMuxerSink final : public Muxer {
public:
    explicit MediaMuxerSink(AMediaMuxer* muxer) : muxer_(muxer) {}
    ~MediaMuxerSink() override { AMediaMuxer_delete(muxer_); }
    MediaMuxerSink(const MediaMuxerSink&) = delete;
    MediaMuxerSink& operator=(const MediaMuxerSink&) = delete;

    int addTrack(const TrackFormat& format) override {
        // Every format the sink hands over came out of a MediaEncoder above.
        const ssize_t track = AMediaMuxer_addTrack(muxer_, static_cast<const MediaTrackFormat&>(format).get());
        return track >= 0 ? static_cast<int>(track) : -1;
    }

    bool start() override { return AMediaMuxer_start(muxer_) == AMEDIA_OK; }

    bool writeSample(int track, const uint8_t* data, const BufferInfo& info) override {
        const AMediaCodecBufferInfo out{0, info.size, info.ptsUs, info.flags};
        return AMediaMuxer_writeSampleData(muxer_, static_cast<size_t>(track), data, &out) == AMEDIA_OK;
    }

    bool stop() override { return AMediaMuxer_stop(muxer_) == AMEDIA_OK; }

private:
    AMediaMuxer* const muxer_;
};

class MediaCodecs final : public Codecs {
public:
    explicit MediaCodecs(int fd) : fd_(fd) {}

    std::unique_ptr<Encoder> createEncoder(const char* mime) override {
        AMediaCodec* codec = mime ? AMediaCodec_createEncoderByType(mime) : nullptr;
        if (!codec) return nullptr;
        return std::make_unique<MediaEncoder>(codec);
    }

    std::unique_ptr<Muxer> createMuxer() override {
        AMediaMuxer* muxer = fd_ >= 0 ? AMediaMuxer_new(fd_, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4) : nullptr;
        if (!muxer) return nullptr;
        return std::make_unique<MediaMuxerSink>(muxer);
    }

private:
    const int fd_;
};

}  // namespace

std::unique_ptr<Codecs> mediaCodecs(int fd) { return std::make_unique<MediaCodecs>(fd); }

}  // namespace geode::output::sink
