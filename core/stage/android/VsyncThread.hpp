#pragma once
#include <android/choreographer.h>

#include <memory>

#include "stage/StagePacing.hpp"
#include "stage/android/Inbox.hpp"

// The process's one vsync thread (docs/agents/native-stage.md): an ALooper thread with its own AChoreographer that
// paces every stage, as the Kotlin pacer thread paced every view. It never draws, so one surface's slow frame never
// costs another surface a vsync, and each pacer hears every vsync to measure the panel by. A drawn vsync becomes a
// last-wins frame request in the stage's inbox; a moved cap becomes a frame-rate hint there.
//
// Started on first use and never quit: an idle looper costs nothing, and a thread torn down on every pause would
// race the next start. Named and prioritised like the thread it replaces ("GeodeFramePacer", display priority).
namespace geode::stage::platform {

class VsyncThread {
public:
    // One stage's pacer and where its frame requests go. Made by attach, freed on the vsync thread after detach,
    // once no callback is pending for it.
    struct Sink {
        StagePacing pacing;
        std::shared_ptr<Inbox> stage;
        bool detached = false;
    };

    static VsyncThread& process();

    // Any thread. Nullptr when the vsync thread could not take it.
    Sink* attach(std::shared_ptr<Inbox> stage, float capFps);
    void run(Sink* sink, bool on);
    void setCap(Sink* sink, float capFps);
    // The last call for this sink.
    void detach(Sink* sink);

    VsyncThread(const VsyncThread&) = delete;
    VsyncThread& operator=(const VsyncThread&) = delete;

private:
    VsyncThread();
    void loop();
    void apply(const Command& command);
    static void onFrame(int64_t frameTimeNanos, void* data);

    std::shared_ptr<Inbox> inbox_;
    AChoreographer* choreographer_ = nullptr;  // vsync thread only
};

}  // namespace geode::stage::platform
