#include "audio/player/Decoder.hpp"

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <unistd.h>

#include <algorithm>
#include <climits>
#include <cstring>

namespace geode::audio::player {

namespace {

constexpr int64_t kOutputTimeoutUs = 10'000;
constexpr int32_t kEncodingPcmFloat = 4;   // android.media.AudioFormat.ENCODING_PCM_FLOAT
// android.media.MediaFormat.KEY_PCM_ENCODING. libmediandk exports AMEDIAFORMAT_KEY_PCM_ENCODING only
// from API 28, so naming that symbol fails to link on minSdk 26; the key itself has been in the codec's
// output format since API 24.
constexpr const char* kKeyPcmEncoding = "pcm-encoding";
constexpr float kInt16Scale = 1.0f / 32768.0f;

bool isAudioMime(const char* mime) { return mime && std::strncmp(mime, "audio/", 6) == 0; }

int64_t framesToUs(int64_t frames, int rate) { return rate > 0 ? frames * 1'000'000 / rate : 0; }

int64_t usToFrames(int64_t us, int rate) { return us * rate / 1'000'000; }

}  // namespace

struct Decoder::Native {
    AMediaExtractor* extractor = nullptr;
    AMediaCodec* codec = nullptr;

    ~Native() {
        if (codec) {
            AMediaCodec_stop(codec);
            AMediaCodec_delete(codec);
        }
        if (extractor) AMediaExtractor_delete(extractor);
    }
};

std::unique_ptr<Decoder> Decoder::open(int fd, int64_t offset, int64_t length, std::string& error) {
    auto native = std::make_unique<Native>();
    native->extractor = AMediaExtractor_new();
    const int64_t span = length > 0 ? length : LLONG_MAX;
    if (!native->extractor || AMediaExtractor_setDataSourceFd(native->extractor, fd, offset, span) != AMEDIA_OK) {
        error = "extractor rejected the file";
        ::close(fd);
        return nullptr;
    }
    int sampleRate = 0;
    int channels = 0;
    int64_t durationUs = 0;
    const size_t tracks = AMediaExtractor_getTrackCount(native->extractor);
    for (size_t i = 0; i < tracks && !native->codec; ++i) {
        AMediaFormat* format = AMediaExtractor_getTrackFormat(native->extractor, i);
        if (!format) continue;
        const char* mime = nullptr;
        if (AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime) && isAudioMime(mime)) {
            AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sampleRate);
            AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
            AMediaFormat_getInt64(format, AMEDIAFORMAT_KEY_DURATION, &durationUs);
            AMediaCodec* codec = AMediaCodec_createDecoderByType(mime);
            if (codec && AMediaCodec_configure(codec, format, nullptr, nullptr, 0) == AMEDIA_OK &&
                AMediaExtractor_selectTrack(native->extractor, i) == AMEDIA_OK) {
                native->codec = codec;
            } else if (codec) {
                AMediaCodec_delete(codec);
            }
        }
        AMediaFormat_delete(format);
    }
    if (!native->codec || sampleRate <= 0 || channels <= 0) {
        error = "no decodable audio track";
        native.reset();
        ::close(fd);
        return nullptr;
    }
    if (AMediaCodec_start(native->codec) != AMEDIA_OK) {
        error = "decoder failed to start";
        native.reset();
        ::close(fd);
        return nullptr;
    }
    return std::unique_ptr<Decoder>(new Decoder(fd, std::move(native), sampleRate, channels, durationUs));
}

Decoder::Decoder(int fd, std::unique_ptr<Native> native, int sampleRate, int channels, int64_t durationUs)
    : native_(std::move(native)), fd_(fd), sampleRate_(sampleRate), channels_(channels), durationUs_(durationUs) {}

// The extractor may keep reading the descriptor until it is deleted, so it goes first.
Decoder::~Decoder() {
    releasePending();
    native_.reset();
    ::close(fd_);
}

size_t Decoder::frameBytes() const { return static_cast<size_t>(channels_) * (floatSamples_ ? 4 : 2); }

size_t Decoder::readStereo(float* out, size_t maxFrames) {
    size_t produced = 0;
    while (produced < maxFrames && error_.empty()) {
        if (pendingFrames_ == 0) {
            if (!fetchOutput()) break;
            continue;
        }
        if (nextPtsUs_ < skipUntilUs_) {
            const auto wanted = static_cast<size_t>(usToFrames(skipUntilUs_ - nextPtsUs_, sampleRate_));
            skipPending(std::max<size_t>(wanted, 1));
            continue;
        }
        produced += convertPending(out + produced * 2, maxFrames - produced);
    }
    return produced;
}

bool Decoder::seek(int64_t positionUs) {
    releasePending();
    if (AMediaCodec_flush(native_->codec) != AMEDIA_OK) {
        error_ = "decoder flush failed";
        return false;
    }
    const int64_t target = std::max<int64_t>(positionUs, 0);
    if (AMediaExtractor_seekTo(native_->extractor, target, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC) != AMEDIA_OK) {
        error_ = "seek failed";
        return false;
    }
    inputDone_ = false;
    outputDone_ = false;
    skipUntilUs_ = target;
    nextPtsUs_ = target;
    return true;
}

bool Decoder::feedInput() {
    const ssize_t index = AMediaCodec_dequeueInputBuffer(native_->codec, 0);
    if (index < 0) return false;
    size_t capacity = 0;
    uint8_t* buffer = AMediaCodec_getInputBuffer(native_->codec, static_cast<size_t>(index), &capacity);
    const ssize_t read = buffer ? AMediaExtractor_readSampleData(native_->extractor, buffer, capacity) : -1;
    if (read < 0) {
        inputDone_ = true;
        AMediaCodec_queueInputBuffer(native_->codec, static_cast<size_t>(index), 0, 0, 0,
                                     AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        return true;
    }
    const int64_t pts = AMediaExtractor_getSampleTime(native_->extractor);
    AMediaCodec_queueInputBuffer(native_->codec, static_cast<size_t>(index), 0, static_cast<size_t>(read),
                                 static_cast<uint64_t>(std::max<int64_t>(pts, 0)), 0);
    AMediaExtractor_advance(native_->extractor);
    return true;
}

// True when a buffer or a format change arrived; false when the codec needs more time or has ended.
bool Decoder::fetchOutput() {
    if (outputDone_) return false;
    for (int i = 0; i < 4 && !inputDone_ && feedInput(); ++i) {
    }
    AMediaCodecBufferInfo info{};
    const ssize_t index = AMediaCodec_dequeueOutputBuffer(native_->codec, &info, kOutputTimeoutUs);
    if (index >= 0) {
        size_t size = 0;
        const uint8_t* data = AMediaCodec_getOutputBuffer(native_->codec, static_cast<size_t>(index), &size);
        if (!data) {
            AMediaCodec_releaseOutputBuffer(native_->codec, static_cast<size_t>(index), false);
            error_ = "decoder output unavailable";
            return false;
        }
        pendingIndex_ = index;
        pendingData_ = data + info.offset;
        pendingFrames_ = static_cast<size_t>(info.size) / frameBytes();
        nextPtsUs_ = info.presentationTimeUs;
        if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) outputDone_ = true;
        if (pendingFrames_ == 0) releasePending();
        return true;
    }
    if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        applyOutputFormat();
        return true;
    }
    return false;
}

void Decoder::applyOutputFormat() {
    AMediaFormat* format = AMediaCodec_getOutputFormat(native_->codec);
    if (!format) return;
    int32_t value = 0;
    if (AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &value) && value > 0) sampleRate_ = value;
    if (AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &value) && value > 0) channels_ = value;
    if (AMediaFormat_getInt32(format, kKeyPcmEncoding, &value)) floatSamples_ = value == kEncodingPcmFloat;
    AMediaFormat_delete(format);
}

void Decoder::releasePending() {
    if (pendingIndex_ >= 0) AMediaCodec_releaseOutputBuffer(native_->codec, static_cast<size_t>(pendingIndex_), false);
    pendingIndex_ = -1;
    pendingData_ = nullptr;
    pendingFrames_ = 0;
}

size_t Decoder::skipPending(size_t frames) {
    const size_t n = std::min(frames, pendingFrames_);
    pendingData_ += n * frameBytes();
    pendingFrames_ -= n;
    nextPtsUs_ += framesToUs(static_cast<int64_t>(n), sampleRate_);
    if (pendingFrames_ == 0) releasePending();
    return n;
}

// Mono is doubled, anything wider keeps its first two channels.
size_t Decoder::convertPending(float* out, size_t maxFrames) {
    const size_t n = std::min(maxFrames, pendingFrames_);
    const int right = channels_ > 1 ? 1 : 0;
    if (floatSamples_) {
        const auto* samples = reinterpret_cast<const float*>(pendingData_);
        for (size_t i = 0; i < n; ++i) {
            out[i * 2] = samples[i * channels_];
            out[i * 2 + 1] = samples[i * channels_ + right];
        }
    } else {
        const auto* samples = reinterpret_cast<const int16_t*>(pendingData_);
        for (size_t i = 0; i < n; ++i) {
            out[i * 2] = samples[i * channels_] * kInt16Scale;
            out[i * 2 + 1] = samples[i * channels_ + right] * kInt16Scale;
        }
    }
    skipPending(n);
    return n;
}

}  // namespace geode::audio::player
