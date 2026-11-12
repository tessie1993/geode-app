#include "audio/ingest/android/OboeMic.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

#include <android/log.h>
#include <oboe/Oboe.h>

namespace geode::audio::ingest {

namespace {

constexpr const char* kTag = "GeodeMic";

int64_t nowMs() {
    using std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::milliseconds>(steady_clock::now().time_since_epoch()).count();
}

class OboeMicStreams;

class OboeMicStream final : public MicStream {
public:
    OboeMicStream(std::shared_ptr<oboe::AudioStream> stream, OboeMicStreams& owner)
        : stream_(std::move(stream)), owner_(owner) {}
    ~OboeMicStream() override;

    bool start() override {
        const oboe::Result result = stream_->requestStart();
        if (result != oboe::Result::OK) {
            __android_log_print(ANDROID_LOG_WARN, kTag, "start refused: %s", oboe::convertToText(result));
        }
        return result == oboe::Result::OK;
    }

    const void* id() const override { return stream_.get(); }

private:
    std::shared_ptr<oboe::AudioStream> stream_;
    OboeMicStreams& owner_;
};

// Static, so it outlives every stream it is the callback of (Oboe's advice for callback objects).
class OboeMicStreams final : public MicStreams,
                             public oboe::AudioStreamDataCallback,
                             public oboe::AudioStreamErrorCallback {
public:
    std::unique_ptr<MicStream> open(const MicRequest& request, MicInput& input) override {
        input_.store(&input, std::memory_order_release);
        const oboe::AudioFormat format = request.format.asFloat ? oboe::AudioFormat::Float : oboe::AudioFormat::I16;
        const oboe::InputPreset preset =
            request.unprocessed ? oboe::InputPreset::Unprocessed : oboe::InputPreset::VoiceRecognition;
        oboe::AudioStreamBuilder builder;
        builder.setDirection(oboe::Direction::Input)
            ->setSharingMode(oboe::SharingMode::Shared)
            ->setPerformanceMode(oboe::PerformanceMode::None)
            ->setInputPreset(preset)
            ->setFormat(format)
            ->setChannelCount(request.format.channels)
            ->setSampleRate(request.format.rateHz)
            ->setBufferCapacityInFrames(request.capacityFrames)
            ->setDataCallback(this)
            ->setErrorCallback(this);
        std::shared_ptr<oboe::AudioStream> stream;
        const oboe::Result result = builder.openStream(stream);
        if (result != oboe::Result::OK || !stream) {
            __android_log_print(ANDROID_LOG_INFO, kTag, "%d Hz x%d %s refused: %s", request.format.rateHz,
                                request.format.channels, request.format.asFloat ? "float" : "16-bit",
                                oboe::convertToText(result));
            return nullptr;
        }
        if (stream->getSampleRate() != request.format.rateHz ||
            stream->getChannelCount() != request.format.channels || stream->getFormat() != format) {
            stream->close();
            retire(std::move(stream));
            return nullptr;
        }
        return std::make_unique<OboeMicStream>(std::move(stream), *this);
    }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream*, void* audioData, int32_t numFrames) override {
        MicInput* input = input_.load(std::memory_order_acquire);
        return input && input->onAudio(audioData, numFrames) ? oboe::DataCallbackResult::Continue
                                                             : oboe::DataCallbackResult::Stop;
    }

    // On Oboe's own thread, which holds the stream until this returns; a new stream may be opened here.
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override {
        __android_log_print(ANDROID_LOG_INFO, kTag, "stream closed: %s", oboe::convertToText(error));
        MicInput* input = input_.load(std::memory_order_acquire);
        if (input) input->onClosed(stream, error == oboe::Result::ErrorDisconnected, nowMs());
    }

    // A closed stream is kept until the next one closes (Oboe's advice), never freed under a late callback.
    void retire(std::shared_ptr<oboe::AudioStream> stream) {
        std::shared_ptr<oboe::AudioStream> previous;
        std::lock_guard<std::mutex> lock(retiredLock_);
        previous = std::exchange(retired_, std::move(stream));
    }

private:
    std::atomic<MicInput*> input_{nullptr};
    std::mutex retiredLock_;
    std::shared_ptr<oboe::AudioStream> retired_;
};

OboeMicStream::~OboeMicStream() {
    // stop() waits until the stream has stopped, so no callback writes after it. One the platform closed on an
    // error is closed already.
    if (stream_->getState() != oboe::StreamState::Closed) {
        stream_->stop();
        stream_->close();
    }
    owner_.retire(std::move(stream_));
}

}  // namespace

MicStreams& oboeMicStreams() {
    static OboeMicStreams streams;
    return streams;
}

}  // namespace geode::audio::ingest
