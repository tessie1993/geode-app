/* Host-test stand-in for <android/log.h>: the logcat boundary core/util/Log.hpp writes through.
 * Declarations only; a test that links a file which logs (core/tests/transition_programs_test.cpp)
 * defines __android_log_print. */
#pragma once

enum android_LogPriority {
    ANDROID_LOG_UNKNOWN = 0,
    ANDROID_LOG_DEFAULT,
    ANDROID_LOG_VERBOSE,
    ANDROID_LOG_DEBUG,
    ANDROID_LOG_INFO,
    ANDROID_LOG_WARN,
    ANDROID_LOG_ERROR,
    ANDROID_LOG_FATAL,
    ANDROID_LOG_SILENT,
};

#ifdef __cplusplus
extern "C" {
#endif
int __android_log_print(int prio, const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
#ifdef __cplusplus
}
#endif
