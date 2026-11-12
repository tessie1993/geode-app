#pragma once
#include <android/looper.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "stage/CommandQueue.hpp"

// A looper thread's inbox: the commands other threads hand it (core/stage/CommandQueue; a stage's are listed in
// core/stage/StageCore.hpp) and the looper that sleeps
// on them, woken by ALooper_wake, which any thread may call. Shared (std::shared_ptr) between the thread and its
// senders, so a sender still holding it after the thread ends pushes into a queue nobody reads, never into freed
// memory.
namespace geode::stage::platform {

struct Inbox {
    static constexpr size_t kCapacity = 32;

    Inbox() { queue.setWake(&Inbox::wake, this); }
    Inbox(const Inbox&) = delete;
    Inbox& operator=(const Inbox&) = delete;
    ~Inbox() {
        if (ALooper* l = looper.exchange(nullptr)) ALooper_release(l);
    }

    // On the inbox's own thread, after ALooper_prepare. Pushes before this wake nothing: the thread drains once
    // after binding, before it first sleeps.
    void bind(ALooper* l) {
        ALooper_acquire(l);
        if (ALooper* old = looper.exchange(l)) ALooper_release(old);
    }

    // An ordered command must not be dropped: a full queue (only a burst of hand-overs fills it) is retried until
    // `within`. False when it never fit.
    bool pushWithin(const Command& command, Merge merge, std::chrono::milliseconds within) {
        const auto until = std::chrono::steady_clock::now() + within;
        for (;;) {
            if (queue.push(command, merge) != Pushed::Full) return true;
            if (std::chrono::steady_clock::now() >= until) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    static void wake(void* self) {
        if (ALooper* l = static_cast<Inbox*>(self)->looper.load()) ALooper_wake(l);
    }

    CommandQueue<kCapacity> queue;
    std::atomic<ALooper*> looper{nullptr};
};

}  // namespace geode::stage::platform
