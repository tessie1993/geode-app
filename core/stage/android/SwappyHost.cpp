#include "stage/android/SwappyHost.hpp"

#include <swappy/swappyGL.h>
#include <swappy/swappyGL_extra.h>

#include <chrono>

#include "util/Log.hpp"

namespace geode::stage::platform {

namespace {

constexpr const char* kTag = "GeodeStage";

int64_t steadyNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

SwappyHost& SwappyHost::process() {
    // Leaked on purpose: a stage thread may still swap while the process exits.
    static SwappyHost* const host = new SwappyHost;
    return *host;
}

void SwappyHost::attach(JNIEnv* env, jobject activity) {
    if (!env || !activity) return;
    std::lock_guard<std::mutex> guard(lock_);
    if (activity_ && env->IsSameObject(activity_, activity)) return;
    destroyLocked(env);
    const bool made = SwappyGL_init(env, activity);
    if (!made || !SwappyGL_isEnabled()) {
        GEODE_LOGW(kTag, "Android Frame Pacing is not available here: stages swap at the display's rate");
        SwappyGL_destroy();
        return;
    }
    // developer.android.com/games/sdk/frame-pacing#recommended-mode. Auto swap off: nothing lowers the rate on its
    // own, and with it off Swappy always pipelines.
    SwappyGL_setAutoSwapInterval(false);
    SwappyGL_setAutoPipelineMode(false);
    SwappyGL_enableStats(false);
    SwappyTracer tracer{};
    tracer.preWait = &SwappyHost::onPreWait;
    tracer.postWait = &SwappyHost::onPostWait;
    tracer.startFrame = &SwappyHost::onStartFrame;
    tracer.userData = this;
    SwappyGL_injectTracer(&tracer);
    activity_ = env->NewGlobalRef(activity);
    made_ = true;
    owner_ = nullptr;
    window_ = nullptr;
    swapNs_ = -1;
    pacing_ = true;
    times_ = {};
}

void SwappyHost::detach(JNIEnv* env, jobject activity) {
    if (!env || !activity) return;
    std::lock_guard<std::mutex> guard(lock_);
    if (activity_ && env->IsSameObject(activity_, activity)) destroyLocked(env);
}

void SwappyHost::destroyLocked(JNIEnv* env) {
    if (made_) SwappyGL_destroy();
    made_ = false;
    owner_ = nullptr;
    window_ = nullptr;
    times_ = {};
    if (activity_) env->DeleteGlobalRef(activity_);
    activity_ = nullptr;
}

SwappyHost::Swap SwappyHost::swap(const void* owner, ANativeWindow* window, int64_t swapNs, EGLDisplay display, EGLSurface surface,
                                  bool& revote) {
    revote = false;
    std::lock_guard<std::mutex> guard(lock_);
    if (!made_ || !window || (owner_ && owner_ != owner)) return Swap::Fallback;
    if (owner_ != owner) {
        owner_ = owner;
        window_ = nullptr;
        times_ = {};
    }
    // Swappy states a frame rate of its own on a new window, a new swap interval and a pacing toggle (1e9 / the interval, or
    // the display's starting rate); the owner's vote follows each of them so it is the one standing.
    if (window_ != window) {
        SwappyGL_setWindow(window);
        window_ = window;
        revote = true;
    }
    if (swapNs != swapNs_) {
        const bool pacing = swapNs > 0;
        if (pacing) SwappyGL_setSwapIntervalNS(static_cast<uint64_t>(swapNs));
        if (pacing != pacing_) {
            SwappyGL_enableFramePacing(pacing);
            pacing_ = pacing;
        }
        swapNs_ = swapNs;
        revote = true;
    }
    return SwappyGL_swap(display, surface) ? Swap::Done : Swap::Failed;
}

void SwappyHost::release(const void* owner) {
    std::lock_guard<std::mutex> guard(lock_);
    if (!owner || owner_ != owner) return;
    if (made_) SwappyGL_setWindow(nullptr);
    owner_ = nullptr;
    window_ = nullptr;
    times_ = {};
}

bool SwappyHost::available(const void* owner) const {
    std::lock_guard<std::mutex> guard(lock_);
    return made_ && owner && (!owner_ || owner_ == owner);
}

bool SwappyHost::holds(const void* owner) const {
    std::lock_guard<std::mutex> guard(lock_);
    return made_ && owner && owner_ == owner;
}

SwappyTimes SwappyHost::times(const void* owner) const {
    std::lock_guard<std::mutex> guard(lock_);
    return made_ && owner && owner_ == owner ? times_ : SwappyTimes{};
}

int64_t SwappyHost::refreshPeriodNs() const {
    std::lock_guard<std::mutex> guard(lock_);
    return made_ ? static_cast<int64_t>(SwappyGL_getRefreshPeriodNanos()) : 0;
}

// The tracer runs inside SwappyGL_swap, on the owner's thread, with lock_ held by swap().
void SwappyHost::onStartFrame(void* self, int, int64_t desiredNs) {
    // Swappy's header calls it milliseconds; it is steady_clock's count, nanoseconds on CLOCK_MONOTONIC.
    static_cast<SwappyHost*>(self)->times_.desiredNs = desiredNs;
}

void SwappyHost::onPreWait(void* self) { static_cast<SwappyHost*>(self)->waitStartNs_ = steadyNs(); }

void SwappyHost::onPostWait(void* self, int64_t, int64_t gpuNs) {
    auto* host = static_cast<SwappyHost*>(self);
    host->times_.waitNs = host->waitStartNs_ > 0 ? steadyNs() - host->waitStartNs_ : 0;
    host->times_.gpuNs = gpuNs;
}

}  // namespace geode::stage::platform
