#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "cast/Fmp4Writer.hpp"
#include "output/EncodeSink.hpp"
#include "viz/frame/CastAudio.hpp"

namespace geode::cast {

// What waits for an audio encoder's input buffers: the render thread pushes each frame's block, the encoder
// thread takes it buffer by buffer. How much is kept, what a stalled codec drops (the oldest) and each
// buffer's PTS are viz/frame/CastAudio's CastAudioFeed; this holds the samples beside its counts. While
// inactive, push keeps nothing; start() begins a fresh feed (its PTS from 0). Any thread: one mutex.
class PcmBacklog {
public:
    PcmBacklog(int rateHz, int channels);

    void start();
    void stop();
    bool active() const;
    void push(const int16_t* pcm, int frames);
    // Up to capacityFrames from the head into out, stamped *ptsUs; how many (0: none wait).
    int take(int16_t* out, int capacityFrames, int64_t* ptsUs);
    int64_t pending() const;

private:
    const int rateHz_;
    const int channels_;
    mutable std::mutex lock_;
    viz::frame::CastAudioFeed feed_;
    std::vector<int16_t> pcm_;   // feed_.pending frames, oldest first
    bool active_ = false;
};

// The cast's encoders: H.264 from the input surface the cast's render thread draws into, AAC from the PCM it
// pushes (queuePcm), written as fragmented MP4 (Fmp4Writer) to Outputs::stream (the server's LiveSegments);
// the H.264 buffers, codec config included, also go as they are to Outputs::video (WebRTC for a browser).
// While Outputs::peers says a browser listens, an Opus encoder runs on the same PCM for it (Outputs::opus);
// it is made when the first comes and freed kOpusIdleNs after the last left (a reload comes back). An Opus
// encoder that cannot be made or fails is not tried again: the browser gets the picture alone.
//
// The codecs come through output/EncodeSink's seam (MediaCodec on a phone, fakes in
// core/tests/cast_encoder_test.cpp). A video or AAC codec error ends the stream: failed() turns true and the
// thread stops; the shell ends the cast.
//
// Threads: open(), inputWindow() and the destructor on the cast's render thread; queuePcm() on it too;
// step() on the encoder's own thread (start()), or the test's. Outputs are called on the encoder thread.
class CastEncoder {
public:
    struct Format {
        int width = 0;
        int height = 0;
        int fps = 0;
        int keyframeSec = 1;
        int videoBitRate = 0;
        int audioBitRate = 0;
        int rateHz = 0;
        int channels = 0;
    };
    struct Outputs {
        std::function<void(const uint8_t*, size_t)> stream;
        std::function<void(const uint8_t*, size_t)> videoConfig;
        std::function<void(const uint8_t*, size_t, int64_t ptsUs, bool key)> video;
        std::function<void(const uint8_t*, size_t, int64_t ptsUs)> opus;
        std::function<int()> peers;
    };

    static constexpr char kMimeOpus[] = "audio/opus";
    static constexpr int64_t kOpusIdleNs = 10'000'000'000;
    static constexpr int kIdleMs = 5;
    // Output buffers taken from one codec in one step, so neither track starves the other.
    static constexpr int kDrainPerStep = 8;

    // Both encoders configured and started, the video one reading a surface; null when either refused.
    static std::unique_ptr<CastEncoder> open(std::unique_ptr<output::sink::Codecs> codecs, const Format& format,
                                             Outputs outputs);
    // Stops the thread, then releases every codec.
    ~CastEncoder();
    CastEncoder(const CastEncoder&) = delete;
    CastEncoder& operator=(const CastEncoder&) = delete;

    // The video encoder's input surface (an ANativeWindow on a phone), the encoder's own.
    void* inputWindow() const;
    // One frame's interleaved s16 block, for the AAC and, while one runs, the Opus encoder.
    void queuePcm(const int16_t* pcm, int frames);

    // Runs step() on a thread of its own until the destructor.
    void start();
    // One pass at nowNs (a monotonic clock): audio input, the video and AAC output, Opus. True when anything moved.
    bool step(int64_t nowNs);
    bool failed() const { return failed_.load(std::memory_order_acquire); }
    bool opusRunning() const { return opus_ != nullptr; }
    const Fmp4Writer& writer() const { return writer_; }

private:
    CastEncoder(std::unique_ptr<output::sink::Codecs> codecs, const Format& format, Outputs outputs);
    // Each: -1 failed, 0 nothing moved, 1 something did.
    int feed(output::sink::Encoder& codec, PcmBacklog& backlog);
    int drain(output::sink::Encoder& codec, int which);
    void runOpus(int64_t nowNs, bool& moved);
    void closeOpus();
    void run();

    static constexpr int kVideo = 0;
    static constexpr int kAac = 1;
    static constexpr int kOpus = 2;

    std::unique_ptr<output::sink::Codecs> codecs_;
    const Format format_;
    Outputs outputs_;
    Fmp4Writer writer_;
    PcmBacklog aacPcm_;
    PcmBacklog opusPcm_;
    std::vector<int16_t> scratch_;
    std::unique_ptr<output::sink::Encoder> video_;
    std::unique_ptr<output::sink::Encoder> aac_;
    std::unique_ptr<output::sink::Encoder> opus_;
    bool opusMissing_ = false;
    int64_t opusIdleSinceNs_ = 0;
    std::atomic<bool> failed_{false};
    std::atomic<bool> stopping_{false};
    std::thread thread_;
};

}  // namespace geode::cast
