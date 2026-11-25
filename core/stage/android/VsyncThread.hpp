#pragma once
#include <android/choreographer.h>

#include <atomic>
#include <memory>
#include <mutex>

#include "stage/StagePacing.hpp"
#include "stage/android/Inbox.hpp"

// The process's one vsync thread (docs/agents/native-stage.md): an ALooper thread with its own AChoreographer that
// wakes every stage on each vsync. It never draws, so one surface's slow frame never costs another surface a vsync.
// A vsync (unless that stage is still on the last frame, stage/VsyncRule) becomes a last-wins frame request in the
// stage's inbox.
//
// Started on first use and never quit: an idle looper costs nothing, and a thread torn down on every pause would
// race the next start. Named "GeodeVsync", at display priority. Each drawn vsync carries its frame time and its
// expected presentation time (the callback data, API 33) to the stage, which steps the picture by the one and stamps
// the swap for the other.
namespace geode::stage::platform {

class VsyncThread {
public:
    // One stage's callback state and where its frame requests go. Made by attach, freed on the vsync thread after detach,
    // once no callback is pending for it.
    struct Sink {
        StagePacing pacing;
        std::shared_ptr<Inbox> stage;
        bool detached = false;
        Sink* nextDetached = nullptr;  // under detachLock_ until the vsync thread takes it
        // A frame request is out and the stage has not finished it: set by the vsync thread before it pushes,
        // cleared by the stage thread (frameDone) after the frame. A vsync reached while set draws nothing.
        std::atomic<bool> stageBusy{false};
    };

    static VsyncThread& process();

    // Any thread. Nullptr when the vsync thread could not take it.
    Sink* attach(std::shared_ptr<Inbox> stage);
    void run(Sink* sink, bool on);
    // The last call for this sink. Never dropped: it does not go through the bounded queue.
    void detach(Sink* sink);
    // The stage thread finished the frame it was asked for (or dropped it): the next vsync may ask again.
    static void frameDone(Sink* sink) {
        if (sink) sink->stageBusy.store(false, std::memory_order_release);
    }

    VsyncThread(const VsyncThread&) = delete;
    VsyncThread& operator=(const VsyncThread&) = delete;

private:
    VsyncThread();
    void loop();
    void apply(const Command& command);
    Sink* takeDetached();
    static void retire(Sink* sink);
    static void onFrame(const AChoreographerFrameCallbackData* callbackData, void* data);

    std::shared_ptr<Inbox> inbox_;
    AChoreographer* choreographer_ = nullptr;  // vsync thread only
    std::mutex detachLock_;
    Sink* detached_ = nullptr;  // under detachLock_
};

}  // namespace geode::stage::platform
