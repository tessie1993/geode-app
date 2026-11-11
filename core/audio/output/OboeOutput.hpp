#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

#include <oboe/Oboe.h>

#include "audio/output/OutputFifo.hpp"

// The playback output's device stream (core/audio/output/README.md): an Oboe output stream whose data
// callback pulls from an OutputFifo. Media3's DefaultAudioSink keeps everything above it (the processor
// chain, the media position, gapless joins); this replaces only the AudioTrack at the end, for plain
// PCM. The only file in core/ that includes Oboe: it is compiled for Android alone, and everything a
// host test can reach is in OutputFifo.
//
// A music player's stream, not a synth's: shared, PerformanceMode::None (the deep-buffer path), float,
// Usage::Media and ContentType::Music, in the player's audio session.
//
// Threads:
//   - the control thread (Media3's playback thread) calls everything public but the two callbacks;
//   - Oboe's audio thread calls onAudioReady, which only reads the fifo: no lock, no allocation, no JNI;
//   - a thread Oboe makes calls onErrorAfterClose once it has closed a disconnected stream (headphones
//     unplugged, the route changed); the stream is opened again there, on the default device, and
//     started if the output was playing;
//   - release() hands the stream to a thread of its own to close, as Media3 does with an AudioTrack.
// streamMutex_ guards the stream pointer between the control, error and closing threads. The audio
// thread never takes it.
namespace geode::audio::output {

class OboeOutput : public oboe::AudioStreamDataCallback,
                   public oboe::AudioStreamErrorCallback,
                   public std::enable_shared_from_this<OboeOutput> {
public:
    // events()' bit beside OutputFifo's: the stream is gone and could not be opened again.
    static constexpr int kEventFailed = 4;

    struct Config {
        int sampleRateHz = 0;
        int channelCount = 0;
        int encoding = 0;     // of the bytes written: OutputFifo's Encoding
        int bufferBytes = 0;  // the sink's buffer size for this output, in bytes of `encoding`
        int sessionId = 0;    // the player's audio session; 0 lets the platform allocate one
    };

    // Null when the format is not one this plays, or the device gave no stream that is exactly it
    // (rate, channels, float, the session asked for): the caller then uses the platform's track.
    static std::shared_ptr<OboeOutput> open(const Config& config);

    void play();
    void pause();
    void flush();
    void stop();
    // Takes whole frames from `bytes` bytes of the config's encoding without blocking. Returns the
    // bytes taken, or -1 once the stream has failed for good.
    int64_t write(const void* data, size_t bytes);
    int64_t positionUs();
    void setVolume(float volume);
    // The device to play on (AudioDeviceInfo.getId), 0 for the default; a change reopens the stream.
    void setDevice(int deviceId);
    // OutputFifo's event bits since the last call, and kEventFailed while failed.
    int events();
    bool stalled();
    // Stops the sound and closes the stream on a thread of its own. Nothing else may be called after.
    void release();

    int sessionId() const { return sessionId_.load(std::memory_order_relaxed); }
    int sampleRateHz() const { return config_.sampleRateHz; }
    // What the sink asked for, about: the fifo plus the stream's own buffer.
    int64_t bufferFrames() const;

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData, int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

private:
    explicit OboeOutput(const Config& config);

    bool openStreamLocked();
    void closeStreamLocked();
    bool startLocked();
    // Closes the stream, opens a new one and starts it if the output is playing.
    bool reopenLocked();
    int64_t positionLocked();

    const Config config_;
    // Made once the first stream is open (its buffer comes off the fifo's length), before any callback.
    std::unique_ptr<OutputFifo> fifo_;

    std::mutex streamMutex_;
    std::shared_ptr<oboe::AudioStream> stream_;
    bool streamStarted_ = false;
    bool wantPlaying_ = false;
    bool released_ = false;
    int deviceId_ = 0;

    std::atomic<int> sessionId_{0};
    std::atomic<int> streamBufferFrames_{0};
    std::atomic<bool> failed_{false};

    // Under streamMutex_: the last timestamp read from the stream, read again every kPollNs.
    StreamTime streamTime_;
    int64_t polledNs_ = 0;
};

}  // namespace geode::audio::output
