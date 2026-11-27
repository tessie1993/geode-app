#pragma once
#include <EGL/egl.h>
#include <android/native_window.h>
#include <jni.h>

#include <cstdint>
#include <mutex>

// The process's one Android Frame Pacing (Swappy) instance, and which stage it paces
// (developer.android.com/games/sdk/frame-pacing/opengl/add-functions).
//
// Swappy is process-wide, paces one window, and is made from an Activity: SwappyGL_init reads the display's timings
// through Activity.getWindowManager. So it lives with MainActivity (attach on create, detach on destroy) and paces the
// app's live stage. A stage that cannot have it (the dream, whose DreamService is no Activity; a second stage while the
// first one holds it; a device where Swappy is disabled) swaps with plain eglSwapBuffers at the display's rate under
// the window's frame-rate vote (StageThread): the docs' alternative when SwappyGL_isEnabled is false.
//
// Every Swappy call is made under one lock, so an Activity that goes while a stage swaps waits for that swap (at most
// one frame) before Swappy is destroyed, and no stage swaps through a Swappy being remade.
namespace geode::stage::platform {

// What Swappy said about the last swap, through its tracer callbacks (SwappyTracer).
struct SwappyTimes {
    // The presentation time Swappy aims the next frame at (startFrame), CLOCK_MONOTONIC ns; 0: none yet.
    int64_t desiredNs = 0;
    // The last swap's wait for its slot (preWait to postWait), and the GPU time of the frame before it (postWait;
    // 0 or less: its fence had not signalled).
    int64_t waitNs = 0;
    int64_t gpuNs = 0;
};

class SwappyHost {
public:
    enum class Swap : uint8_t {
        Fallback,  // Swappy is not this stage's (not made, disabled, or another stage holds it): nothing was swapped
        Done,
        Failed,    // SwappyGL_swap failed: eglGetError says why
    };

    static SwappyHost& process();

    // Main thread. Swappy is made with this Activity (replacing one made with another), or let go with it.
    void attach(JNIEnv* env, jobject activity);
    void detach(JNIEnv* env, jobject activity);

    // Stage thread. `owner` swaps through Swappy when Swappy is made and enabled and no other stage holds it; it then
    // holds it until release. Swappy is given the owner's window and the swap interval for its cap first, when they
    // changed (0: no cap, Follow device, Swappy's pacing off). `revote` is set when Swappy may have voted a frame rate
    // of its own in this swap (its pacing was switched on or off), so the owner states its own vote again.
    Swap swap(const void* owner, ANativeWindow* window, int64_t swapNs, EGLDisplay display, EGLSurface surface, bool& revote);
    // The owner lets go (its pacing stopped, its window went, it ends): Swappy forgets the window. Nothing when it
    // does not hold Swappy.
    void release(const void* owner);
    bool holds(const void* owner) const;
    // The owner's last swap, as Swappy's tracer told it; zeros for a stage that does not hold Swappy.
    SwappyTimes times(const void* owner) const;
    // The active display mode's refresh period as Swappy knows it (its display timings, kept current by its own
    // refresh-rate callback), ns; 0 when Swappy is not made.
    int64_t refreshPeriodNs() const;

    SwappyHost(const SwappyHost&) = delete;
    SwappyHost& operator=(const SwappyHost&) = delete;

private:
    SwappyHost() = default;
    void destroyLocked(JNIEnv* env);
    static void onStartFrame(void* self, int frame, int64_t desiredNs);
    static void onPreWait(void* self);
    static void onPostWait(void* self, int64_t cpuNs, int64_t gpuNs);

    mutable std::mutex lock_;
    jobject activity_ = nullptr;       // a global reference
    bool made_ = false;                // SwappyGL_init succeeded and Swappy is enabled
    const void* owner_ = nullptr;
    ANativeWindow* window_ = nullptr;  // the window Swappy was given last (it holds its own reference)
    int64_t swapNs_ = -1;              // the interval Swappy was given last, -1 none
    bool pacing_ = true;               // whether Swappy's pacing is on (its default)
    // Tracer data, written inside SwappyGL_swap on the owner's thread (lock_ held), read by it after.
    SwappyTimes times_;
    int64_t waitStartNs_ = 0;
};

}  // namespace geode::stage::platform
