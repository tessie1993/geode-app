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
// through Activity.getWindowManager. The direct GameActivity host owns attach/detach for GeodeGameActivity and paces the
// app's live stage at fixed 30 FPS. Auto swap interval and auto pipeline mode are disabled. A stage that cannot have it
// (a second stage while the first one holds it; a device where Swappy is disabled or fails to initialise, which the docs
// allow for) keeps the seamless-only 30 FPS window vote and the existing one-shot Choreographer fallback (StageThread).
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
    // holds it until release. Before every new window Swappy is given the fixed 30 FPS interval and frame pacing is
    // enabled. `revote` is set on that new window so the owner's seamless 30 FPS vote follows Swappy's own vote.
    Swap swap(const void* owner, ANativeWindow* window, EGLDisplay display, EGLSurface surface, bool& revote);
    // The owner lets go (its pacing stopped, its window went, it ends): Swappy forgets the window. Nothing when it
    // does not hold Swappy.
    void release(const void* owner);
    bool holds(const void* owner) const;
    // Swappy is made and `owner` has successfully attached its window. A claimed owner whose
    // SwappyGL_setWindow failed is not available: StageCore must use Choreographer fallback.
    bool available(const void* owner) const;
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
    // Tracer data, written inside SwappyGL_swap on the owner's thread (lock_ held), read by it after.
    SwappyTimes times_;
    int64_t waitStartNs_ = 0;
};

}  // namespace geode::stage::platform
