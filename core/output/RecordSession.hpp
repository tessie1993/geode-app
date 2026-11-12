#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "output/EncodeSink.hpp"
#include "output/Recorder.hpp"

namespace geode::output::record {

// One recording's encoder side (Visuals › Record): the H.264 and AAC encoders and the MP4 muxer on an
// EncodeSink, driven by its own thread, on the recorder's clock. The stage's GL thread draws into the video
// encoder's surface (inputWindow, through geode_viz_record_start) and feeds the recorder the sound; this
// thread takes the audio back out, drains both encoders into the file and tells the clock what came out and
// how much was written. The recorder decides every frame, every PTS and the end.
//
// The end, whoever asked for it (the user, a limit, a resize, a full file, a failure): the clock says
// stopped; the thread asks for the stage's copy to close (teeWanted(), which the shell polls; it answers
// with teeClosed() on the GL thread, once the surface is no longer drawn into) and waits up to kTeeWaitMs
// for it (the GL thread may be gone with its surface), then ends the video stream, takes the rest of the
// audio, ends that, and finishes the file. state() then says whether it holds a video.
//
// No JNI and no callback into the shell: the shell polls state(). The codecs come from a sink::Codecs, so
// the host test runs all of this with fakes (core/tests/encode_sink_test.cpp).
class Session {
public:
    enum class State : int {
        Running = 0,
        Closing = 1,  // stopped: the stage's copy should close (teeClosed)
        Saved = 2,    // finished: the file holds a video
        Failed = 3,   // finished: it does not (an encoder or the muxer failed, or no video came out)
    };

    static constexpr int64_t kTeeWaitMs = 1'000;
    static constexpr int kIdleMs = 5;

    // Opens the encoders for a stage of stageWidth x stageHeight at the first size the device takes of the
    // ones encoderAttempt offers for shortSide, the muxer, and starts the thread. Null when no encoder or
    // muxer would start; nothing runs then. now is the recorder's clock (geode_record_now_ns).
    static std::unique_ptr<Session> open(std::shared_ptr<Recorder> recorder, std::unique_ptr<sink::Codecs> codecs,
                                         int stageWidth, int stageHeight, int shortSide, int fps,
                                         std::function<int64_t()> now);

    // Stops a recording still running (as a failure) and joins the thread.
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    int width() const { return width_; }
    int height() const { return height_; }
    const std::shared_ptr<Recorder>& recorder() const { return recorder_; }

    // Calls use(window) with the video encoder's input surface while the encoders are still open, under the
    // lock the thread takes to release them, so the window cannot go mid-call; false once they are gone.
    // Whoever keeps the window past the call (an EGL surface) holds its own reference.
    bool withInputWindow(const std::function<bool(void* window)>& use);

    State state() const { return static_cast<State>(state_.load(std::memory_order_acquire)); }
    // The stage no longer draws into the surface. Any thread; idempotent.
    void teeClosed();

private:
    class Host;

    Session(std::shared_ptr<Recorder> recorder, std::unique_ptr<sink::Codecs> codecs, std::function<int64_t()> now);
    void run();
    void waitForTee();

    std::shared_ptr<Recorder> recorder_;
    std::function<int64_t()> now_;
    std::unique_ptr<sink::Codecs> codecs_;
    std::unique_ptr<sink::Muxer> muxer_;
    std::unique_ptr<Host> host_;
    std::unique_ptr<sink::EncodeSink> sink_;
    int width_ = 0;
    int height_ = 0;

    std::atomic<int> state_{static_cast<int>(State::Running)};
    std::mutex teeLock_;
    std::condition_variable teeChanged_;
    bool teeClosed_ = false;
    // Held while the thread releases the encoders, and by withInputWindow.
    std::mutex windowLock_;
    std::thread thread_;
};

}  // namespace geode::output::record
