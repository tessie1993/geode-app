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
#include "stage/StageLifecycle.hpp"
#include "stage/android/EglWindow.hpp"
#include "stage/android/Inbox.hpp"
#include "stage/android/VsyncThread.hpp"

// One surface's stage thread (docs/agents/native-stage.md): an ALooper thread that owns the EGL context and window
// surface, carries out what core/stage/StageLifecycle asks for, and draws a frame through its StageClient when the
// vsync thread asks. It decides nothing itself.
//
// The setters may be called from any thread and return at once, except setWindow, which waits (bounded) until the
// stage has let go of the old window, as surfaceDestroyed requires. The client is owned from start(): release is
// its last call, on this thread.
namespace geode::stage::platform {

class StageThread {
public:
    // How long a window hand-over waits for the stage, and how long destroy waits for the thread to finish.
    static constexpr std::chrono::milliseconds kWindowWait{1000};
    static constexpr std::chrono::milliseconds kQuitWait{200};

    static std::shared_ptr<StageThread> start(Kind kind, std::unique_ptr<StageClient> client, float capFps);

    StageThread(Kind kind, std::unique_ptr<StageClient> client, float capFps);
    StageThread(const StageThread&) = delete;
    StageThread& operator=(const StageThread&) = delete;
    ~StageThread();

    // A window (acquired here for the stage) or none. Returns once the stage has applied it, or after kWindowWait.
    void setWindow(ANativeWindow* window, int width, int height);
    void setSize(int width, int height);
    void setMode(RunMode mode);
    void setPaused(bool paused);
    void redraw();
    void setCap(float capFps);

    uint32_t state() const { return state_.load(std::memory_order_relaxed); }
    float pacedFps() const { return paced_.load(std::memory_order_relaxed); }
    uint32_t takeEvents() { return events_.exchange(0, std::memory_order_relaxed); }

    // Sends quit and waits up to kQuitWait: joined when the thread finished, else left to finish on its own.
    void quitAndWait();

private:
    void run();
    void loop();
    void apply(const Command& command);
    void runActions();
    Outcome execute(const Action& action);
    void draw(float pacedFps, float capFps);
    void restateRate();
    void acknowledge();
    void push(const Command& command, Merge merge);

    Lifecycle lifecycle_;
    std::unique_ptr<StageClient> client_;
    std::shared_ptr<Inbox> inbox_;
    EglWindow egl_;
    VsyncThread::Sink* sink_ = nullptr;
    ANativeWindow* window_ = nullptr;  // stage thread only
    float cap_;                        // stage thread only
    bool frameDue_ = false;            // stage thread only
    float dueFps_ = 0.0f;
    float dueCapFps_ = 0.0f;
    int64_t seenSeq_ = 0;

    std::atomic<uint32_t> state_{0};
    std::atomic<float> paced_;
    std::atomic<uint32_t> events_{0};
    std::atomic<int64_t> nextSeq_{0};

    std::mutex ackLock_;
    std::condition_variable ackCv_;
    int64_t appliedSeq_ = 0;  // under ackLock_
    bool finished_ = false;   // under ackLock_
    std::thread thread_;
};

}  // namespace geode::stage::platform
