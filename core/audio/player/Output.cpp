#include "audio/player/Output.hpp"

#include <algorithm>

namespace geode::audio::player {

namespace {
constexpr int64_t kStateTimeoutNs = 500'000'000;
}

bool Output::open(std::string& error) {
    close();
    disconnected_.store(false, std::memory_order_release);
    if (!openWith(oboe::SharingMode::Exclusive) && !openWith(oboe::SharingMode::Shared)) {
        error = "audio output failed to open";
        return false;
    }
    sampleRate_ = stream_->getSampleRate();
    stream_->setBufferSizeInFrames(stream_->getFramesPerBurst() * 2);
    return true;
}

bool Output::openWith(oboe::SharingMode mode) {
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
        ->setSharingMode(mode)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setFormat(oboe::AudioFormat::Float)
        ->setFormatConversionAllowed(true)
        ->setChannelCount(Mixer::kChannels)
        ->setChannelConversionAllowed(true)
        ->setUsage(oboe::Usage::Media)
        ->setContentType(oboe::ContentType::Music)
        ->setDataCallback(this)
        ->setErrorCallback(this);
    std::shared_ptr<oboe::AudioStream> stream;
    if (builder.openStream(stream) != oboe::Result::OK || !stream) return false;
    stream_ = std::move(stream);
    return true;
}

void Output::close() {
    if (!stream_) return;
    stream_->requestStop();
    stream_->close();
    stream_.reset();
    running_.store(false, std::memory_order_release);
}

bool Output::start() {
    if (!stream_ || stream_->requestStart() != oboe::Result::OK) return false;
    const bool started = settle(oboe::StreamState::Starting, oboe::StreamState::Started);
    running_.store(started, std::memory_order_release);
    return started;
}

bool Output::pause() {
    if (!stream_) return false;
    if (stream_->requestPause() != oboe::Result::OK) return false;
    const bool paused = settle(oboe::StreamState::Pausing, oboe::StreamState::Paused);
    if (paused) running_.store(false, std::memory_order_release);
    return paused;
}

bool Output::settle(oboe::StreamState leaving, oboe::StreamState wanted) {
    oboe::StreamState next = oboe::StreamState::Unknown;
    stream_->waitForStateChange(leaving, &next, kStateTimeoutNs);
    return stream_->getState() == wanted;
}

oboe::DataCallbackResult Output::onAudioReady(oboe::AudioStream*, void* audioData, int32_t numFrames) {
    auto* out = static_cast<float*>(audioData);
    size_t remaining = numFrames > 0 ? static_cast<size_t>(numFrames) : 0;
    while (remaining > 0) {
        const size_t n = std::min(remaining, Mixer::kMaxRenderFrames);
        mixer_.render(out, n);
        out += n * Mixer::kChannels;
        remaining -= n;
    }
    return oboe::DataCallbackResult::Continue;
}

void Output::onErrorAfterClose(oboe::AudioStream*, oboe::Result) {
    running_.store(false, std::memory_order_release);
    disconnected_.store(true, std::memory_order_release);
}

}  // namespace geode::audio::player
