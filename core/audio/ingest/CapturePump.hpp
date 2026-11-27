#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <utility>

// A capture the platform hands over only as an object to read from (another app's audio: an AudioRecord
// over a MediaProjection; the dream's output mix: a Visualizer), read on a native thread of its own and
// written straight into a ring. The shell makes the object and hands it over; no sample passes through the
// shell. The device side is audio/ingest/android/JavaCapture; a fake in
// core/tests/capture_pump_test.cpp.
//
// Threads: start, stop, active and quiet on the caller's (the main thread); the reader's begin, read and end on
// the pump's own thread; interrupt on the caller's, while stop waits.
namespace geode::audio::ingest {

class CaptureReader {
public:
    virtual ~CaptureReader() = default;
    // Before the first read and after the last, on the pump's thread (attach to and detach from the VM there;
    // end also lets go of the platform object). begin false: no read, end still runs.
    virtual bool begin() { return true; }
    virtual void end() {}
    // Up to `capacity` bytes into data, waiting for them as the platform does. The bytes read, 0 for none this
    // time, < 0 when the capture is over.
    virtual int read(void* data, int capacity) = 0;
    // A read waiting now returns soon. Any thread, also after end.
    virtual void interrupt() {}
};

class CapturePump {
public:
    // Writes `bytes` bytes as read into the ring and returns the largest magnitude stored. Pump thread.
    using Write = std::function<float(const void* data, size_t bytes)>;
    using Clock = int64_t (*)();
    // How long stop waits for a read the platform does not let go of; the thread is then left to end alone.
    static constexpr int64_t kStopWaitMs = 500;
    // How many threads left behind that way may be waiting at once before start refuses a new capture. Each one
    // holds its reader (the platform capture object and its JNI global references) and its buffer until its read
    // returns. One is a platform hiccup a fresh capture gets past; a second says this device's read is not letting
    // go, and every further start would only pin another, so two is where it stops.
    static constexpr int kMaxStuck = 2;
    // Threads left behind now, across the process; one leaves the count when its read returns and it ends.
    static int stuck();

    // A thread reading `reader` in blocks of readBytes into `write`. Null when the thread could not start (the
    // reader's end has then run). nowMs is a monotonic clock in milliseconds.
    static std::unique_ptr<CapturePump> start(std::unique_ptr<CaptureReader> reader, Write write, int readBytes,
                                              Clock nowMs);

    CapturePump(const CapturePump&) = delete;
    CapturePump& operator=(const CapturePump&) = delete;
    ~CapturePump() { stop(); }

    // When it returns, write is never called again.
    void stop();
    // False after stop, or once the reader ended the capture.
    bool active() const;
    // Running, and only digital zero for 4 s since the start or the last sound (sound/Silence): what Android
    // hands a capture the playing app refuses.
    bool quiet() const;

    struct Run;

private:
    explicit CapturePump(std::shared_ptr<Run> run) : run_(std::move(run)) {}
    std::shared_ptr<Run> run_;
    std::thread thread_;
};

}  // namespace geode::audio::ingest
