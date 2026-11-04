// Editor-only stand-in for the NDK's <android/log.h>, so clangd can read the native
// core on a host without the NDK (see compile_flags.txt). Never compiled into the app
// or the CI host tests: neither puts tools/clangd-stubs on the include path.
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

extern "C" int __android_log_print(int prio, const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 3, 4)));
