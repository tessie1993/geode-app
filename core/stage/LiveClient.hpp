#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

#include "stage/StageClient.hpp"

// A live stage's client: the renderer behind the app's view.
// It runs on the stage thread what must run there (frames, a transition's warm-up, Record's tee), keeps
// what a frame reported until the shell polls it, and holds the renderer alive for calls from other threads.
// Pure: the renderer is a LiveViz, which core/api/geode_stage_api.cpp binds to a geode_viz and the host test
// (core/tests/live_client_test.cpp) to a model.
namespace geode::stage {

// The frame's event bits. Values are the ABI (GEODE_FRAME_* in geode_api.h).
inline constexpr uint32_t kFrameLiquidChanged = 0x4;
inline constexpr uint32_t kFrameErrorChanged = 0x8;
inline constexpr uint32_t kFrameDirectorMoment = 0x20;
inline constexpr uint32_t kFrameStillReady = 0x40;

// One recording's tee: the recorder and its session, and the mark drawn over each copied frame (no tile, none).
struct RecordAsk {
    void* recorder = nullptr;
    void* session = nullptr;
    std::vector<uint8_t> tile;  // premultiplied RGBA8, top row first
    int tileWidth = 0;
    int tileHeight = 0;
    int placement = 0;
    int corner = 0;
};

struct Still {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

// The renderer, on the stage thread unless said otherwise. Destroying it frees the renderer.
class LiveViz {
public:
    virtual ~LiveViz() = default;
    virtual void surfaceCreated() = 0;
    virtual void surfaceChanged(int width, int height) = 0;
    virtual uint32_t frame(const void* ring, float pacedFps, float capFps) = 0;
    // The frame's expected presentation time, 0: none (StageClient::framePresent).
    virtual void framePresent(int64_t presentNs) { static_cast<void>(presentNs); }
    // The next frame steps by 0 (StageClient::restartClock).
    virtual void restartClock() {}
    virtual void trimSpares() = 0;
    // Drawing stopped: the renderer stops listening to its ring, so the ring's analysis thread runs only while a
    // surface still draws from it. The next frame listens again.
    virtual void letSoundGo() = 0;
    virtual void releaseScenes() = 0;
    virtual void warmTransition(const std::string& id) = 0;
    // Context current: builds the mark and opens the tee. False when either would not.
    virtual bool recordStart(const RecordAsk& ask) = 0;
    // The recording ends as failed.
    virtual void recordFailed(const RecordAsk& ask) = 0;
    // Closes the tee (safe when none opened), frees the mark and tells the session its tee is closed.
    virtual void recordStop(const RecordAsk& ask) = 0;
    virtual std::string error() = 0;
    virtual int takeDirectorMoments() = 0;
    virtual int liquidMode() = 0;
    virtual bool readStill(Still& out) = 0;
    virtual void requestStill() = 0;
};

class LiveClient final : public StageClient {
public:
    explicit LiveClient(std::unique_ptr<LiveViz> viz);

    // Stage thread.
    void surfaceCreated() override;
    void surfaceChanged(int width, int height) override;
    uint32_t frame(float pacedFps, float capFps) override;
    void framePresent(int64_t presentNs) override;
    void restartClock() override;
    void trimSpares() override;
    void contextLost() override;
    void service(bool ready) override;
    void release(bool glCurrent) override;

    // Any thread from here on.

    // The ring frames hear, or none. Never waits for a frame: one in flight keeps the ring it began with and the
    // next frame hears this one. The client holds each ring while a frame may read it, so its owner may drop its
    // own hold at any time: a frame slower than the stage's bounded quit wait still reads a live ring.
    void setSound(std::shared_ptr<const void> ring);
    // Each queues work for the stage thread; the caller then asks the stage for a service turn. False once the
    // stage released its renderer: nothing was queued, and for a recording no tee will ever open or close.
    bool warm(std::string id);
    bool recordStart(RecordAsk ask);
    bool recordStop();
    // No tee is open or waiting to open: the recorder and its session may be freed.
    bool recordClosed() const { return recordsOpen_.load() == 0; }

    std::string error() const;
    int takeDirectorMoments() { return moments_.exchange(0); }
    int liquidMode() const { return liquid_.load(); }
    bool takeStill(Still& out);

    // Holds the renderer for one call from another thread; false (and nothing held) once it is released or while it
    // is being, never waiting for either.
    bool lockViz();
    void unlockViz();

private:
    enum class JobKind : uint8_t { Warm, RecordStart, RecordStop };
    struct Job {
        JobKind kind;
        std::string id;
        RecordAsk ask;
    };

    bool queue(Job job);
    void runJobs(bool ready);
    void closeRecord();

    std::unique_ptr<LiveViz> viz_;  // written under vizLock_, on the stage thread
    std::shared_mutex vizLock_;

    std::mutex soundLock_;  // held only to swap rings, never across a draw
    std::shared_ptr<const void> ring_;     // the ring the next, or the frame in flight, hears
    std::shared_ptr<const void> pending_;  // set while a frame is in flight; becomes ring_ as it ends
    bool pendingSet_ = false;
    bool inFrame_ = false;

    std::mutex jobsLock_;
    std::vector<Job> jobs_;
    bool released_ = false;           // under jobsLock_
    std::optional<RecordAsk> record_;  // stage thread: the tee asked for, opened or failed, until its stop
    std::atomic<int> recordsOpen_{0};

    mutable std::mutex stateLock_;
    std::string error_;
    std::optional<Still> still_;
    std::atomic<int> moments_{0};
    std::atomic<int> liquid_{0};
};

}  // namespace geode::stage
