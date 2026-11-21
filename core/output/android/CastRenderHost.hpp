#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "api/geode_api.h"
#include "cast/CastLook.hpp"

// The cast's render thread (docs/agents/native-stage.md, Cast): it opens the encoders, makes an EGL context current
// on the encoder's input window (output/android/EncoderGl) and, once a frame slot (viz/frame/FixedRateClock),
// either blits the stage's newest copy from the tee (viz/frame/CastTee) or, while the stage draws nothing, draws a
// renderer of its own (geode_viz) on the look the shell handed in (cast/CastLook), then stamps the frame with its
// PTS, swaps it into the encoder and pumps the frame's sound (output/CastAudioPump). Every rule it applies is an
// existing pure one (geode_cast_tee_await_stage, geode_cast_fallback_release, geode_stage_run); this is the thread
// around them. The shell starts it, polls failed() and takeDirectorMoments(), and stops it. Android only.
namespace geode::output {

struct CastRenderSetup {
    int64_t server = 0;                           // geode_cast_server_open's handle, which outlives this thread
    int64_t webrtc = 0;                           // geode_webrtc_open's, likewise
    const geode_pcm_ring* music = nullptr;        // the player's ring: only its sound goes to the TV
    AAssetManager* assets = nullptr;              // for the fallback renderer's scenes
    std::string cacheDir;
};

class CastRender {
public:
    explicit CastRender(CastRenderSetup setup);
    // Stops and joins.
    ~CastRender();
    CastRender(const CastRender&) = delete;
    CastRender& operator=(const CastRender&) = delete;

    void start();
    // The look the fallback renderer draws, from any thread; the whole look each time.
    void setLook(const geode_viz_look& look) { look_.set(look); }
    // Asks the thread to end and waits up to timeoutMs for it to close the encoders; true once it has.
    bool stop(int64_t timeoutMs);
    // The thread ended on an error while it was not asked to stop: the cast should end.
    bool failed() const { return failed_.load(); }
    // The fallback renderer's director moments since the last call (Auto steps on them while the stage draws nothing).
    int takeDirectorMoments() { return moments_.exchange(0); }

    // Process-wide, kept across streams: what the shell last saw of the stage (GEODE_STAGE_* bits), and the ring of
    // the phone's output mix while the app hears other apps (null: the player's, native holds it).
    static void setStageFlags(unsigned flags);
    static void setDeviceRing(const geode_pcm_ring* ring);

private:
    void run();
    bool draw(geode_cast_encoder* encoder);
    geode_viz* makeFallback(uint64_t& seenLook);

    CastRenderSetup setup_;
    cast::CastLook look_;
    std::thread thread_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> failed_{false};
    std::atomic<int> moments_{0};
    std::mutex endMutex_;
    std::condition_variable endCv_;
    bool ended_ = false;
};

}  // namespace geode::output
