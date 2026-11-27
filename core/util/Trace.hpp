#pragma once
#include <cstdint>

#if defined(__ANDROID__) && __ANDROID_API__ >= 29
#include <android/trace.h>
#define GEODE_ATRACE 1
#endif

// Perfetto/systrace markers (NDK ATrace): sections and counters a system trace shows on the stage thread, so a phone's
// frames can be read where the visual's own SurfaceView hides them from View-level tools. Each call is a flag check
// while no trace is being taken, and nothing at all on the host. Names are string literals: ATrace keeps no copy.
namespace geode::util {

class TraceSection {
public:
    explicit TraceSection(const char* name) {
#ifdef GEODE_ATRACE
        ATrace_beginSection(name);
#else
        static_cast<void>(name);
#endif
    }
    ~TraceSection() {
#ifdef GEODE_ATRACE
        ATrace_endSection();
#endif
    }
    TraceSection(const TraceSection&) = delete;
    TraceSection& operator=(const TraceSection&) = delete;
};

inline void traceCounter(const char* name, int64_t value) {
#ifdef GEODE_ATRACE
    if (ATrace_isEnabled()) ATrace_setCounter(name, value);
#else
    static_cast<void>(name);
    static_cast<void>(value);
#endif
}

}  // namespace geode::util
