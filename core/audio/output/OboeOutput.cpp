#include "audio/output/OboeOutput.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <thread>

#include <system_error>

#include <android/log.h>

namespace geode::audio::output {

namespace {

constexpr const char* kTag = "GeodeOboeOutput";

// How often the stream is asked for a timestamp; between two the position runs on by the clock.
constexpr int64_t kPollNs = 200'000'000;
// How long a start waits for a pause still in flight before it gives the stream up.
constexpr int64_t kSettleNs = 200'000'000;
// How long a release waits for a stopped output's last frames to leave the device (a Bluetooth
// link holds a few hundred ms) before it closes the stream anyway.
constexpr int64_t kDrainNs = 1'000'000'000;

int64_t nowNs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
}

}  // namespace

std::shared_ptr<OboeOutput> OboeOutput::open(const Config& config) {
    if (!OutputFifo::supports(config.encoding, config.channelCount, config.sampleRateHz)) return nullptr;
    std::shared_ptr<OboeOutput> output(new OboeOutput(config));
    std::lock_guard<std::mutex> lock(output->streamMutex_);
    if (!output->openStreamLocked()) return nullptr;
    // The stream's own buffer is part of what the sink asked for, so the fifo is that much shorter:
    // the audio written stays as far ahead of the speaker as it is with an AudioTrack.
    const int asked =
        OutputFifo::capacityFrames(config.sampleRateHz, config.channelCount, config.encoding, config.bufferBytes);
    const int least = config.sampleRateHz / 1000 * OutputFifo::kMinLatencyMs;
    const int capacity = std::max(least, asked - output->streamBufferFrames_.load(std::memory_order_relaxed));
    output->fifo_ = std::make_unique<OutputFifo>(config.sampleRateHz, config.channelCount, capacity);
    return output;
}

OboeOutput::OboeOutput(const Config& config) : config_(config) {
    sessionId_.store(config.sessionId > 0 ? config.sessionId : 0, std::memory_order_relaxed);
}

bool OboeOutput::openStreamLocked() {
    const std::shared_ptr<OboeOutput> self = shared_from_this();
    const int session = sessionId_.load(std::memory_order_relaxed);
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
        ->setSharingMode(oboe::SharingMode::Shared)
        ->setPerformanceMode(oboe::PerformanceMode::None)
        ->setFormat(oboe::AudioFormat::Float)
        ->setFormatConversionAllowed(true)
        ->setChannelCount(config_.channelCount)
        ->setChannelConversionAllowed(true)
        ->setSampleRate(config_.sampleRateHz)
        ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
        ->setUsage(oboe::Usage::Media)
        ->setContentType(oboe::ContentType::Music)
        // 0 is SessionId::Allocate, as it is AudioManager.AUDIO_SESSION_ID_GENERATE.
        ->setSessionId(static_cast<oboe::SessionId>(session))
        ->setDeviceId(deviceId_ > 0 ? deviceId_ : oboe::kUnspecified)
        ->setDataCallback(std::shared_ptr<oboe::AudioStreamDataCallback>(self))
        ->setErrorCallback(std::shared_ptr<oboe::AudioStreamErrorCallback>(self));

    std::shared_ptr<oboe::AudioStream> stream;
    const oboe::Result result = builder.openStream(stream);
    if (result != oboe::Result::OK || !stream) {
        __android_log_print(ANDROID_LOG_WARN, kTag, "open failed: %s", oboe::convertToText(result));
        return false;
    }
    const int streamSession = static_cast<int>(stream->getSessionId());
    const bool exact = stream->getSampleRate() == config_.sampleRateHz &&
                       stream->getChannelCount() == config_.channelCount &&
                       stream->getFormat() == oboe::AudioFormat::Float && (session <= 0 || streamSession == session);
    if (!exact) {
        __android_log_print(ANDROID_LOG_WARN, kTag, "stream is %d Hz, %d ch, session %d: not what was asked",
                            stream->getSampleRate(), stream->getChannelCount(), streamSession);
        stream->close();
        return false;
    }
    // A reopened stream stays in the session the first one got, so effects on it carry over.
    if (streamSession > 0) sessionId_.store(streamSession, std::memory_order_relaxed);
    streamBufferFrames_.store(std::max(stream->getBufferSizeInFrames(), 0), std::memory_order_relaxed);
    stream_ = std::move(stream);
    streamStarted_ = false;
    streamTime_ = StreamTime{};
    polledNs_ = 0;
    return true;
}

void OboeOutput::closeStreamLocked() {
    if (!stream_) return;
    // close() also drops the stream's hold on this object (its callbacks).
    stream_->close();
    stream_.reset();
    streamStarted_ = false;
}

bool OboeOutput::startLocked() {
    if (!stream_) return false;
    if (streamStarted_) return true;
    oboe::Result result = stream_->requestStart();
    if (result != oboe::Result::OK && stream_->getState() == oboe::StreamState::Pausing) {
        // A pause asked for a moment ago is still in flight: let it land, then start.
        oboe::StreamState next = oboe::StreamState::Unknown;
        stream_->waitForStateChange(oboe::StreamState::Pausing, &next, kSettleNs);
        result = stream_->requestStart();
    }
    streamStarted_ = result == oboe::Result::OK;
    if (!streamStarted_) {
        __android_log_print(ANDROID_LOG_WARN, kTag, "start failed: %s", oboe::convertToText(result));
    }
    return streamStarted_;
}

bool OboeOutput::reopenLocked() {
    closeStreamLocked();
    if (!openStreamLocked()) return false;
    // No callback runs yet: the new stream is not started.
    fifo_->streamRestarted();
    return !wantPlaying_ || startLocked();
}

void OboeOutput::play() {
    std::lock_guard<std::mutex> lock(streamMutex_);
    if (!wantPlaying_) {
        wantPlaying_ = true;
        fifo_->play(nowNs());
        // A timestamp from before the pause says nothing about the stream from here on.
        streamTime_ = StreamTime{};
        polledNs_ = 0;
    }
    if (released_ || failed_.load(std::memory_order_relaxed)) return;
    // A stream that will not start was disconnected while it stood paused: open a new one, once.
    if (!startLocked() && !reopenLocked()) failed_.store(true, std::memory_order_relaxed);
}

void OboeOutput::pauseStreamLocked() {
    if (!stream_ || !streamStarted_) return;
    const oboe::Result result = stream_->requestPause();
    if (result == oboe::Result::OK) {
        streamStarted_ = false;
    } else {
        // Still started as far as anyone knows: the next play() must not think it has to start it.
        __android_log_print(ANDROID_LOG_WARN, kTag, "pause failed: %s", oboe::convertToText(result));
    }
}

void OboeOutput::pause() {
    std::lock_guard<std::mutex> lock(streamMutex_);
    // Playing out after stop(): the sink pauses the moment the position reaches the last frame, while
    // the stream's buffer and the device (Bluetooth above all) still hold the tail. Pausing the stream
    // now would hold that tail back and blip or drop it later, so the stream is left to stop itself
    // (onAudioReady), as an AudioTrack does after stop(). The play() that may follow finds the output
    // still playing and does nothing. A pause before stop() is an ordinary pause.
    if (wantPlaying_ && fifo_->stopped()) return;
    wantPlaying_ = false;
    // The position stands where it is now, not where it was last asked for.
    positionLocked();
    fifo_->pause();
    pauseStreamLocked();
}

void OboeOutput::flush() {
    std::lock_guard<std::mutex> lock(streamMutex_);
    fifo_->flush();
}

void OboeOutput::stop() {
    // The stream keeps pulling until the fifo is empty, then stops itself from its callback and plays
    // out what it holds. Paused, it does that once play() starts it again.
    fifo_->stop(nowNs());
}

int64_t OboeOutput::write(const void* data, size_t bytes) {
    if (failed_.load(std::memory_order_relaxed)) return -1;
    const int frames = fifo_->writeBytes(data, bytes, config_.encoding);
    return static_cast<int64_t>(frames) * OutputFifo::bytesPerFrame(config_.encoding, config_.channelCount);
}

int64_t OboeOutput::positionLocked() {
    const int64_t now = nowNs();
    if (stream_ && streamStarted_ && now - polledNs_ >= kPollNs) {
        polledNs_ = now;
        const oboe::ResultWithValue<oboe::FrameTimestamp> stamp = stream_->getTimestamp(CLOCK_MONOTONIC);
        streamTime_.valid = stamp.error() == oboe::Result::OK;
        if (streamTime_.valid) {
            streamTime_.framePosition = stamp.value().position;
            streamTime_.timeNs = stamp.value().timestamp;
        }
    }
    return fifo_->positionUs(now, streamTime_, streamBufferFrames_.load(std::memory_order_relaxed));
}

int64_t OboeOutput::positionUs() {
    std::unique_lock<std::mutex> lock(streamMutex_, std::try_to_lock);
    if (lock.owns_lock()) return positionLocked();
    // A reopen is in flight on the error thread. Never wait for it: the frames read serve meanwhile.
    return fifo_->positionUs(nowNs(), StreamTime{}, streamBufferFrames_.load(std::memory_order_relaxed));
}

void OboeOutput::setVolume(float volume) {
    fifo_->setVolume(volume);
}

void OboeOutput::setDevice(int deviceId) {
    std::lock_guard<std::mutex> lock(streamMutex_);
    const int wanted = deviceId > 0 ? deviceId : 0;
    if (wanted == deviceId_) return;
    deviceId_ = wanted;
    if (released_ || failed_.load(std::memory_order_relaxed)) return;
    if (!reopenLocked()) failed_.store(true, std::memory_order_relaxed);
}

int OboeOutput::events() {
    return fifo_->takeEvents() | (failed_.load(std::memory_order_relaxed) ? kEventFailed : 0);
}

bool OboeOutput::stalled() {
    return !failed_.load(std::memory_order_relaxed) && fifo_->stalled(nowNs());
}

int64_t OboeOutput::bufferFrames() const {
    return static_cast<int64_t>(fifo_->capacityFrames()) + streamBufferFrames_.load(std::memory_order_relaxed);
}

void OboeOutput::awaitDrain() {
    std::shared_ptr<oboe::AudioStream> stream;
    {
        std::lock_guard<std::mutex> lock(streamMutex_);
        stream = stream_;
    }
    if (!stream) return;
    const int64_t deadline = nowNs() + kDrainNs;
    while (nowNs() < deadline) {
        // Started while the fifo empties, Stopping while the device plays the rest; anything else is done.
        const oboe::StreamState state = stream->getState();
        const bool playingOut = state == oboe::StreamState::Starting || state == oboe::StreamState::Started ||
                                state == oboe::StreamState::Stopping;
        if (!playingOut) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void OboeOutput::release() {
    bool drain = false;
    {
        std::lock_guard<std::mutex> lock(streamMutex_);
        released_ = true;
        // After stop() the stream is playing the track's tail out: that is let finish (the sink releases
        // an output as soon as its position reaches the end, also between two formats). Otherwise
        // silence now, since a seek must not play the old position on.
        drain = wantPlaying_ && streamStarted_ && fifo_->stopped();
        wantPlaying_ = false;
        if (!drain) pauseStreamLocked();
    }
    // The wait and the close can take a while: on a thread of their own, never the playback thread.
    try {
        std::thread([self = shared_from_this(), drain] {
            if (drain) self->awaitDrain();
            std::lock_guard<std::mutex> lock(self->streamMutex_);
            self->closeStreamLocked();
        }).detach();
    } catch (const std::system_error&) {
        // No thread to be had. Nothing may throw through the C ABI, so close here, without the wait.
        std::lock_guard<std::mutex> lock(streamMutex_);
        closeStreamLocked();
    }
}

oboe::DataCallbackResult OboeOutput::onAudioReady(oboe::AudioStream*, void* audioData, int32_t numFrames) {
    fifo_->read(static_cast<float*>(audioData), numFrames);
    // After stop(), once the last frame is read: Stop lets the stream play out what it holds and stop
    // itself, as AudioTrack.stop() does. No request is made from this thread.
    return fifo_->drained() ? oboe::DataCallbackResult::Stop : oboe::DataCallbackResult::Continue;
}

void OboeOutput::onErrorAfterClose(oboe::AudioStream*, oboe::Result error) {
    std::lock_guard<std::mutex> lock(streamMutex_);
    if (released_ || !stream_) return;
    // Only for the stream this output holds now: one it already replaced reports late.
    const oboe::StreamState state = stream_->getState();
    if (state != oboe::StreamState::Closed && state != oboe::StreamState::Disconnected) return;
    __android_log_print(ANDROID_LOG_INFO, kTag, "stream lost (%s), reopening", oboe::convertToText(error));
    // The default device from here on: the one asked for may be the one that went away.
    deviceId_ = 0;
    if (!reopenLocked()) failed_.store(true, std::memory_order_relaxed);
}

}  // namespace geode::audio::output
