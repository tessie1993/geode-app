#pragma once

#include <cmath>
#include <cstdint>

#if defined(__ANDROID__) && __ANDROID_API__ >= 33
#include <android/performance_hint.h>
#include <unistd.h>
#define GEODE_WORK_HINT 1
#endif

namespace geode::util {

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
        if (session_) APerformanceHint_closeSession(session_);
    }
#endif

    // The time one frame at `fps` is allowed, in nanoseconds; 0 when fps is not a rate.
    static int64_t targetNs(float fps) {
        return std::isfinite(fps) && fps > 0.0f ? static_cast<int64_t>(1.0e9 / static_cast<double>(fps)) : 0;
    }

    // One unit of work took `actualNs` of the `targetNs` it was allowed. Ignored unless both are positive.
    void report(int64_t targetNs, int64_t actualNs) {
        if (targetNs <= 0 || actualNs <= 0) return;
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
        APerformanceHint_reportActualWorkDuration(session_, actualNs);
#endif
    }

private:
#ifdef GEODE_WORK_HINT
    static bool differs(int64_t a, int64_t b) {
        const int64_t gap = a > b ? a - b : b - a;
        return gap * 20 > b;  // more than 5 %
    }
    APerformanceHintSession* session_ = nullptr;
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
