#pragma once

#include <cmath>
#include <cstdint>

#if defined(__ANDROID__) && __ANDROID_API__ >= 33
#include <android/performance_hint.h>
#include <dlfcn.h>
#include <unistd.h>
#define GEODE_WORK_HINT 1
#endif

namespace geode::util {

#ifdef GEODE_WORK_HINT
// The Android 15 (API 35) WorkDuration calls, looked up once in libandroid: the app's minSdk is below 35, so they are
// not linked, and a device older than 15 simply has none (every pointer is null then).
struct WorkDurationApi {
    AWorkDuration* (*create)() = nullptr;
    void (*release)(AWorkDuration*) = nullptr;
    void (*setStart)(AWorkDuration*, int64_t) = nullptr;
    void (*setTotal)(AWorkDuration*, int64_t) = nullptr;
    void (*setCpu)(AWorkDuration*, int64_t) = nullptr;
    void (*setGpu)(AWorkDuration*, int64_t) = nullptr;
    int (*report)(APerformanceHintSession*, AWorkDuration*) = nullptr;

    bool complete() const { return create && release && setStart && setTotal && setCpu && setGpu && report; }

    static const WorkDurationApi& get() {
        static const WorkDurationApi api = load();
        return api;
    }

private:
    template <typename Fn>
    static void symbol(void* lib, const char* name, Fn& out) {
        out = reinterpret_cast<Fn>(dlsym(lib, name));
    }
    static WorkDurationApi load() {
        WorkDurationApi api;
        void* lib = dlopen("libandroid.so", RTLD_NOW);
        if (!lib) return api;
        symbol(lib, "AWorkDuration_create", api.create);
        symbol(lib, "AWorkDuration_release", api.release);
        symbol(lib, "AWorkDuration_setWorkPeriodStartTimestampNanos", api.setStart);
        symbol(lib, "AWorkDuration_setActualTotalDurationNanos", api.setTotal);
        symbol(lib, "AWorkDuration_setActualCpuDurationNanos", api.setCpu);
        symbol(lib, "AWorkDuration_setActualGpuDurationNanos", api.setGpu);
        symbol(lib, "APerformanceHint_reportActualWorkDuration2", api.report);
        if (!api.complete()) return WorkDurationApi{};
        return api;
    }
};
#endif

// ADPF (Android's performance hint session, NDK API 33): a thread tells the system how long each unit of its work took
// against how long it was allowed, and the system sizes the CPU for it. Nothing here changes what the thread computes or
// when; where the session cannot be made (a host build, API below 33, a device without the service) every call does
// nothing. One instance belongs to one thread: the session is made for the thread that first reports.
class WorkHint {
public:
    WorkHint() = default;
    WorkHint(const WorkHint&) = delete;
    WorkHint& operator=(const WorkHint&) = delete;
#ifdef GEODE_WORK_HINT
    ~WorkHint() {
        if (duration_) WorkDurationApi::get().release(duration_);
        if (session_) APerformanceHint_closeSession(session_);
    }
#endif

    // The time one frame at `fps` is allowed, in nanoseconds; 0 when fps is not a rate.
    static int64_t targetNs(float fps) {
        return std::isfinite(fps) && fps > 0.0f ? static_cast<int64_t>(1.0e9 / static_cast<double>(fps)) : 0;
    }

    // One unit of work took `actualNs` of the `targetNs` it was allowed. Ignored unless both are positive.
    void report(int64_t targetNs, int64_t actualNs) { report(targetNs, actualNs, 0, 0); }

    // The same, with the work's two sides told apart (Android 15, API 35: the system sizes the CPU and the GPU clocks
    // together): `cpuNs` is this thread's work, never the time it sat blocked in a swap; `gpuNs` the GPU's for the same
    // unit (0 or less: not known); `startNs` when the unit began, CLOCK_MONOTONIC (0: not given). Below API 35, or with no
    // GPU time, only the CPU time is told, as report(targetNs, cpuNs).
    void report(int64_t targetNs, int64_t cpuNs, int64_t gpuNs, int64_t startNs) {
        if (targetNs <= 0 || cpuNs <= 0) return;
#ifdef GEODE_WORK_HINT
        if (failed_) return;
        if (!session_) {
            APerformanceHintManager* manager = APerformanceHint_getManager();
            const int32_t tid = static_cast<int32_t>(gettid());
            session_ = manager ? APerformanceHint_createSession(manager, &tid, 1, targetNs) : nullptr;
            if (!session_) {
                failed_ = true;
                return;
            }
            target_ = targetNs;
        } else if (differs(targetNs, target_)) {
            // Only a real change of the rate: the call crosses to the system.
            APerformanceHint_updateTargetWorkDuration(session_, targetNs);
            target_ = targetNs;
        }
        if (gpuNs > 0 && reportSplit(cpuNs, gpuNs, startNs)) return;
        APerformanceHint_reportActualWorkDuration(session_, cpuNs);
#else
        static_cast<void>(gpuNs);
        static_cast<void>(startNs);
#endif
    }

private:
#ifdef GEODE_WORK_HINT
    static bool differs(int64_t a, int64_t b) {
        const int64_t gap = a > b ? a - b : b - a;
        return gap * 20 > b;  // more than 5 %
    }
    // The CPU and GPU durations as one WorkDuration; false where the platform has none (below API 35) or refused it.
    bool reportSplit(int64_t cpuNs, int64_t gpuNs, int64_t startNs) {
        const WorkDurationApi& api = WorkDurationApi::get();
        if (!api.complete()) return false;
        if (!duration_) duration_ = api.create();
        if (!duration_) return false;
        if (startNs > 0) api.setStart(duration_, startNs);
        api.setTotal(duration_, cpuNs > gpuNs ? cpuNs : gpuNs);
        api.setCpu(duration_, cpuNs);
        api.setGpu(duration_, gpuNs);
        return api.report(session_, duration_) == 0;
    }
    APerformanceHintSession* session_ = nullptr;
    AWorkDuration* duration_ = nullptr;
    int64_t target_ = 0;
    bool failed_ = false;
#endif
};

// For a worker thread that hands the hint to a layer that cannot include this one: the calling thread's own session.
inline void reportThreadWork(int64_t targetNs, int64_t actualNs) {
    thread_local WorkHint hint;
    hint.report(targetNs, actualNs);
}

}  // namespace geode::util
