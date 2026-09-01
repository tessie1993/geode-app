# NDK build of the MilkDrop JNI bridge. AGP runs this once per ABI in
# abiFilters (app/build.gradle.kts) and packages the resulting
# libmilkdropjni.so; the engine it wraps stays a prebuilt blob.
cmake_minimum_required(VERSION 3.22.1)
project(milkdropjni C)

# libprojectM-4.so is STOCK projectM v4.1.7, built out-of-band by
# .github/workflows/native-libs.yml and committed under jniLibs/<abi>/ with a
# SHA256SUMS that records where it came from. It is imported here, not built:
# the engine is a 20-minute CMake build of a separate LGPL project, and the
# app build only needs to link against it.
set(PROJECTM_SO "${CMAKE_CURRENT_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libprojectM-4.so")
if(NOT EXISTS "${PROJECTM_SO}")
    message(FATAL_ERROR
        "Missing ${PROJECTM_SO}. Every ABI in abiFilters needs a committed "
        "libprojectM-4.so; see tools/build-projectm.md.")
endif()
add_library(projectM SHARED IMPORTED)
set_target_properties(projectM PROPERTIES IMPORTED_LOCATION "${PROJECTM_SO}")

# include/projectM-4 is the engine's public C API at the same v4.1.7 tag the
# committed .so was built from (src/api/include, plus the two headers that
# tag's CMake generates). The bridge's exported Java_dev_geode_* symbols are
# exactly what MilkdropEngine.kt declares as external fun.
add_library(milkdropjni SHARED milkdrop_jni.c)
target_include_directories(milkdropjni PRIVATE include)
target_compile_options(milkdropjni PRIVATE -Wall)
target_link_libraries(milkdropjni PRIVATE projectM log)

# Google Play requires 16 KB page-size support for apps targeting Android 15+.
# NDK r28 already aligns by default; the flags make it explicit so a toolchain
# bump cannot silently regress it. checkNativePageAlignment in
# app/build.gradle.kts re-checks every packaged .so on release builds.
target_link_options(milkdropjni PRIVATE
    -Wl,-z,max-page-size=16384
    -Wl,-z,common-page-size=16384)
