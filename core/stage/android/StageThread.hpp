#pragma once
#include <android/native_window.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

#include "stage/StageClient.hpp"
#include "stage/StageCore.hpp"
#include "stage/android/EglWindow.hpp"
#include "stage/android/Inbox.hpp"
#include "stage/android/VsyncThread.hpp"

// One surface's stage thread (docs/agents/native-stage.md): an ALooper thread that owns the EGL context and window
// surface and carries out what core/stage/StageCore asks for through its port. It decides nothing itself: what a
// command means and when a frame is drawn is StageCore's, when EGL objects live is StageLifecycle's.
//
// The setters may be called from any thread and return at once, except setWindow, which waits (bounded) until the
// stage has let go of the old window, as surfaceDestroyed requires. The stage holds the client from start() until
// its release, which is the client's last call, on this thread.
namespace geode::stage::platform {

class StageThread final : private StagePort {
public:
    // How long a window hand-over waits for the stage to apply it, how long any command waits for room in a full
    // queue, and how long destroy waits, in all, for the thread to finish.
    static constexpr std::chrono::milliseconds kWindowWait{1000};
    static constexpr std::chrono::milliseconds kPushWait{100};
    static constexpr std::chrono::milliseconds kQuitWait{200};

    static std::shared_ptr<StageThread> start(Kind kind, std::shared_ptr<StageClient> client, float capFps);

    StageThread(Kind kind, std::shared_ptr<StageClient> client, float capFps);
    StageThread(const StageThread&) = delete;
    StageThread& operator=(const StageThread&) = delete;
    ~StageThread() override;

    // A window (acquired here for the stage) or none. Returns once the stage has applied it. Past kWindowWait
    // (plus kPushWait for a full queue) it returns anyway, a known degradation: with none, the EGL surface on the
    // old window may then still exist for a moment. The stage holds its own reference to that window, so the late
    // destroy is safe, and the state word's surface bit tells when it has happened.
    void setWindow(ANativeWindow* window, int width, int height);
    void setSize(int width, int height);
    void setMode(RunMode mode);
    void setPaused(bool paused);
    void redraw();
    void setCap(float capFps);
    // Has the stage thread call the client's service once, between frames.
    void service();

    uint32_t state() const { return core_.state(); }
    float pacedFps() const { return core_.pacedFps(); }
    uint32_t takeEvents() { return core_.takeEvents(); }

    // Asks the thread to quit and waits up to kQuitWait in all: joined when the thread finished, else left to
    // finish on its own.
    void quitAndWait();

private:
    void run();
    void loop();
    void push(const Command& command, Merge merge);

    Outcome createContext(bool& surfaceless) override;
    Outcome createSurface(void* window) override;
    Outcome makeCurrent() override;
    Outcome makeCurrentSurfaceless() override;
    void releaseCurrent() override;
    void destroySurface() override;
    void destroyContext() override;
    Outcome swap() override;
    void releaseWindow(void* window) override;
    void setFrameRate(void* window, float fps) override;
    void vsyncRun(bool on) override;
    void vsyncCap(float capFps) override;
    void windowApplied(int64_t sequence) override;

    std::shared_ptr<Inbox> inbox_;
    EglWindow egl_;                      // stage thread only
    VsyncThread::Sink* sink_ = nullptr;  // stage thread only
    float startCap_;
    StageCore core_;

    std::atomic<int64_t> nextSeq_{0};
    // Quit is this flag, which no full queue can lose; its command only wakes the thread.
    std::atomic<bool> quit_{false};

    std::mutex ackLock_;
    std::condition_variable ackCv_;
    int64_t appliedSeq_ = 0;  // under ackLock_
    bool finished_ = false;   // under ackLock_
    std::thread thread_;
};

}  // namespace geode::stage::platform
