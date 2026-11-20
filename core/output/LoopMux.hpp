#pragma once

// A long-form loop extend's copy, start to finish: the rendered loops' encoded video samples read back and written
// into one MP4, once per repeat of the plan (output/LoopPlan) with each timestamp shifted (output/LoopExtend), the
// soundtrack's already-encoded AAC samples interleaved by timestamp as the repeats go in, the progress, and what a
// cancel or a failure leaves. The extractor and the scratch file's bytes are the Host's; the muxer is the encode
// sink's seam (output/EncodeSink.hpp), so the whole copy runs on the host under core/tests.
//
// Threads: run() blocks its thread. cancel(), progress() and state() are the only calls another thread may make
// while it runs; everything else is read after run() returns.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "output/EncodeSink.hpp"
#include "output/VideoExport.hpp"

namespace geode::output::loopmux {

enum class State { Idle = 0, Running = 1, Done = 2, Cancelled = 3, Failed = 4 };

// Why a copy failed.
enum class Failure {
    None = 0,
    Unusable = 1,  // the loops cannot make one file (no video track, not one encoding); detail() is the words
    Read = 2,      // a loop or a soundtrack sample could not be read
    Write = 3,     // the muxer would not open, start, take a sample or finish
};

// What identifies the encoder configuration a loop carries: all the loops share one track, so all must match.
struct Signature {
    std::string mime;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> csd0;
    std::vector<uint8_t> csd1;

    bool operator==(const Signature& other) const {
        return mime == other.mime && width == other.width && height == other.height && csd0 == other.csd0 &&
               csd1 == other.csd1;
    }
};

// One rendered loop's encoded video, read back (MediaExtractor on a phone, a fake on the host).
class LoopReader {
public:
    virtual ~LoopReader() = default;
    virtual const Signature& signature() const = 0;
    // The format's declared largest sample (0: it declares none).
    virtual int declaredBytes() const = 0;
    // The video format, for the muxer.
    virtual std::unique_ptr<sink::TrackFormat> trackFormat() = 0;
    // Every sample's time and extractor flags, in order; false when they cannot be read. Leaves the head at the end.
    virtual bool scan(std::vector<int64_t>* timesUs, std::vector<int>* flags) = 0;
    // The head back at the first sample.
    virtual bool rewind() = 0;
    // The sample under the head into out (capacity bytes) and the head on to the next: its size, < 0 at the end
    // or on an error.
    virtual int next(uint8_t* out, size_t capacity) = 0;
};

// One clip of the soundtrack, encoded: its samples (offsets into the Host's scratch for this clip) and its length.
struct Clip {
    std::vector<videoexport::AudioSample> samples;
    int64_t durationUs = 0;
};

// What the shell asks for: `loops` rendered loops (the Host opens each by index), each one loopDurationUs long,
// and the soundtrack's clips, played back to back, with the one AAC format they share.
struct Request {
    int loops = 0;
    int64_t loopDurationUs = 0;
    std::vector<Clip> clips;
    std::vector<sink::FormatEntry> audioFormat;
};

class Host {
public:
    virtual ~Host() = default;
    // The codecs, whose muxer writes the output file.
    virtual std::unique_ptr<sink::Codecs> codecs() = 0;
    // Loop `index`, or null with why (shown as is) it could not be opened.
    virtual std::unique_ptr<LoopReader> openLoop(int index, const char** why) = 0;
    // `sample`'s bytes of clip `clip` into out (sample.size of room): how many were read, < 0 on an error.
    virtual int readAudio(int clip, const videoexport::AudioSample& sample, uint8_t* out) = 0;
};

class Job {
public:
    Job(const Request& request, Host& host);
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    void run();
    void cancel() { cancelled_.store(true, std::memory_order_relaxed); }
    float progress() const { return progress_.load(std::memory_order_relaxed); }
    State state() const { return state_.load(std::memory_order_acquire); }

    Failure failure() const { return failure_; }
    const std::string& detail() const { return detail_; }

private:
    enum class End { Done, Cancelled, Failed };

    End copy();
    End fail(Failure failure, const char* detail);

    const Request request_;
    Host& host_;
    std::atomic<bool> cancelled_{false};
    std::atomic<float> progress_{0.0f};
    std::atomic<State> state_{State::Idle};
    Failure failure_ = Failure::None;
    std::string detail_;
};

}  // namespace geode::output::loopmux
