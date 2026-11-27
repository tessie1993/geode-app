#pragma once

#ifdef __ANDROID__
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace geode::util {

// Linux nice values for the native threads. A new thread takes its spawner's nice, so one started from the stage
// thread would draw level with it and one started from the main thread would rank with app threads: every native
// thread that loops names its own. core/sound sets the analysis thread's (-2) itself, being a module of its own.

// android.os.Process.THREAD_PRIORITY_URGENT_DISPLAY: the threads that draw a frame (the stage's, the cast's). The docs warn
// that a thread set too high interrupts the UI thread and RenderThread (about -10 here), so none of ours ranks above them.
inline constexpr int kDisplayNice = -8;
// One step below: threads a frame or a live stream waits on (an encoder's drain, the capture read).
inline constexpr int kFeedNice = -6;
// A default app thread: work no frame waits on.
inline constexpr int kDefaultNice = 0;
// threadNice() before the thread set one.
inline constexpr int kNiceInherited = 100;

namespace detail {
inline int& threadNiceSlot() {
    thread_local int nice = kNiceInherited;
    return nice;
}
}  // namespace detail

// Gives the calling thread `nice`; false when the system refused it (the thread keeps the one it had). Off Android
// it only records the nice, so a host test sees what a thread asked for without the privilege a raise needs.
inline bool setThreadNice(int nice) {
#ifdef __ANDROID__
    if (setpriority(PRIO_PROCESS, static_cast<id_t>(gettid()), nice) != 0) return false;
#endif
    detail::threadNiceSlot() = nice;
    return true;
}

// The nice the calling thread last set through setThreadNice; kNiceInherited until it did.
inline int threadNice() { return detail::threadNiceSlot(); }

}  // namespace geode::util
